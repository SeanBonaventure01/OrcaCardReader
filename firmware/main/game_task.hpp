#pragma once

// game_task owns the Engine (docs/ARCHITECTURE.md §3, §5). Everything else reaches it through
// GameTask::with_engine(), which runs a function on game_task and waits for the result.

#include <functional>
#include <optional>
#include <string>

#include "orca/api/api.hpp"
#include "orca/game/engine.hpp"
#include "orca/hal/file_system.hpp"

namespace orca::app {

using json = nlohmann::json;

class GameTask : public api::EngineGateway {
 public:
  // `fs` is where the snapshot lives. `listener` gets every engine push message (on game_task).
  GameTask(game::Engine& engine, hal::FileSystem& fs, std::function<void(const json&)> listener);

  // Restores the last snapshot (if any) and starts the task. Call once.
  void start();

  // Runs `fn` on game_task. Rethrows EngineError; throws EngineError(503) if game_task is stuck.
  json with_engine(const std::function<json(game::Engine&)>& fn) override;

  // A card the reader saw (from nfc_task): an ordinary tap, or while programming cards, the
  // player number to write onto it. See Engine::card_seen.
  std::optional<int> card_seen(const std::string& uid, std::optional<int> card_player);
  void card_written(const std::string& uid, int number, game::WriteResult result);

 private:
  static void run(void* self);
  void loop();
  void maybe_save(bool force);

  game::Engine& engine_;
  hal::FileSystem& fs_;
  std::function<void(const json&)> listener_;
  void* queue_ = nullptr;     // Mailbox<Msg>*
  void* saver_ = nullptr;     // Mailbox<std::string>*
  void* task_ = nullptr;      // TaskHandle_t of game_task
  std::uint64_t saved_changes_ = 0;
  std::int64_t last_save_ms_ = 0;
};

}  // namespace orca::app
