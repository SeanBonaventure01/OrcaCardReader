#include "game_task.hpp"

#include <variant>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "orca/device/mailbox.hpp"
#include "orca/game/snapshot_store.hpp"

namespace orca::app {

namespace {

constexpr const char* TAG = "game";
constexpr int kTickMs = 100;
constexpr std::int64_t kSaveDebounceMs = 5000;
constexpr const char* kSnapshotPath = "/snapshot.json";

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }

// A function to run on game_task, with a slot for its result and a semaphore to wake the caller.
struct Call {
  const std::function<json(game::Engine&)>* fn;
  json* result;
  int* error_status;
  std::string* error_message;
  SemaphoreHandle_t done;
};
struct Tap {
  std::string uid;
};
using Msg = std::variant<Call, Tap>;

using Queue = device::Mailbox<Msg>;
using Saver = device::Mailbox<std::string>;

// Writes snapshots off the game task so slow flash writes never delay a tap.
void storage_task(void* arg) {
  auto* args = static_cast<std::pair<Saver*, hal::FileSystem*>*>(arg);
  game::SnapshotStore store(*args->second, kSnapshotPath);
  while (true) {
    auto text = args->first->take(portMAX_DELAY);
    // Only the newest snapshot matters; skip any that queued up behind it.
    while (auto newer = args->first->take(0)) text = std::move(newer);
    const std::int64_t t0 = now_ms();
    if (store.save(json::parse(*text)))
      ESP_LOGI(TAG, "snapshot saved (%u bytes, %lld ms)", static_cast<unsigned>(text->size()), now_ms() - t0);
    else
      ESP_LOGE(TAG, "snapshot save failed");
  }
}

}  // namespace

GameTask::GameTask(game::Engine& engine, hal::FileSystem& fs, std::function<void(const json&)> listener)
    : engine_(engine), fs_(fs), listener_(std::move(listener)) {}

void GameTask::start() {
  engine_.set_listener(listener_);
  game::SnapshotStore store(fs_, kSnapshotPath);
  if (auto snap = store.load()) {
    try {
      engine_.restore(*snap);
      ESP_LOGI(TAG, "restored snapshot from %s: state %s, %u players", store.loaded_from().c_str(),
               game::to_string(engine_.state()), static_cast<unsigned>(engine_.players().size()));
    } catch (const game::EngineError& e) {
      ESP_LOGE(TAG, "ignoring bad snapshot: %s", e.what());
    }
  }
  saved_changes_ = engine_.change_count();
  queue_ = new Queue(16);
  saver_ = new Saver(2);
  static std::pair<Saver*, hal::FileSystem*> storage_args{static_cast<Saver*>(saver_), &fs_};
  xTaskCreatePinnedToCore(storage_task, "storage", 6144, &storage_args, 2, nullptr, 0);
  xTaskCreatePinnedToCore(run, "game", 16384, this, 5, reinterpret_cast<TaskHandle_t*>(&task_), 0);
}

void GameTask::run(void* self) { static_cast<GameTask*>(self)->loop(); }

void GameTask::loop() {
  auto* queue = static_cast<Queue*>(queue_);
  std::int64_t next_tick = now_ms() + kTickMs;
  while (true) {
    const std::int64_t wait = std::max<std::int64_t>(0, next_tick - now_ms());
    if (auto msg = queue->take(pdMS_TO_TICKS(wait))) {
      if (auto* call = std::get_if<Call>(msg.get())) {
        try {
          *call->result = (*call->fn)(engine_);
        } catch (const game::EngineError& e) {
          *call->error_status = e.status();
          *call->error_message = e.what();
        } catch (const std::exception& e) {
          *call->error_status = 500;
          *call->error_message = e.what();
        }
        xSemaphoreGive(call->done);
      } else if (auto* tap = std::get_if<Tap>(msg.get())) {
        try {
          const auto r = engine_.tap(tap->uid);
          ESP_LOGI(TAG, "TAP %s -> %s %+lld", r.uid.c_str(), game::to_string(r.outcome), static_cast<long long>(r.points));
        } catch (const game::EngineError& e) {
          ESP_LOGW(TAG, "tap rejected: %s", e.what());
        }
      }
    }
    if (now_ms() >= next_tick) {
      engine_.tick();
      next_tick += kTickMs;
      if (next_tick < now_ms()) next_tick = now_ms() + kTickMs;  // fell behind; don't burst
      maybe_save(false);
    }
  }
}

void GameTask::maybe_save(bool force) {
  const auto changes = engine_.change_count();
  if (changes == saved_changes_) return;
  if (!force && now_ms() - last_save_ms_ < kSaveDebounceMs) return;
  static_cast<Saver*>(saver_)->post(engine_.snapshot().dump());
  saved_changes_ = changes;
  last_save_ms_ = now_ms();
}

json GameTask::with_engine(const std::function<json(game::Engine&)>& fn) {
  if (xTaskGetCurrentTaskHandle() == static_cast<TaskHandle_t>(task_)) return fn(engine_);
  json result;
  int status = 0;
  std::string message;
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  if (!static_cast<Queue*>(queue_)->post(Call{&fn, &result, &status, &message, done}, pdMS_TO_TICKS(500))) {
    vSemaphoreDelete(done);
    throw game::EngineError(503, "costume is busy, try again");
  }
  // game_task always gives the semaphore once it has run the call, so it's safe to wait for
  // it; the timeout just guards against a wedged task.
  if (xSemaphoreTake(done, pdMS_TO_TICKS(5000)) != pdTRUE) {
    ESP_LOGE(TAG, "game_task did not answer within 5 s");
    // Leak `done` and the locals deliberately: game_task may still write to them.
    abort();
  }
  vSemaphoreDelete(done);
  if (status) throw game::EngineError(status, message);
  return result;
}

void GameTask::tap(const std::string& uid) {
  if (!static_cast<Queue*>(queue_)->post(Tap{uid})) ESP_LOGW(TAG, "game queue full, tap dropped");
}

}  // namespace orca::app
