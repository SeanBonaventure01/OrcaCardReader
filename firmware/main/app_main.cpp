// ORCA reader costume firmware. Wires the portable core (engine, games, API) to the
// device: display, LEDs, audio, storage, Wi-Fi/HTTP and the serial console.
// The PN532 reader isn't fitted yet; taps come from the phone UI and the console.

#include <memory>

#include "console.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "game_task.hpp"
#include "nvs_flash.h"
#include "orca/api/api.hpp"
#include "orca/audio/audio.hpp"
#include "orca/board/board.h"
#include "orca/config/config.hpp"
#include "orca/display/display.hpp"
#include "orca/game/engine.hpp"
#include "orca/games/registry.hpp"
#include "orca/leds/leds.hpp"
#include "orca/net/net.hpp"
#include "orca/storage/storage.hpp"

using namespace orca;
using json = nlohmann::json;

namespace {

constexpr const char* TAG = "orca";

class EspClock : public hal::Clock {
 public:
  hal::TimeMs now_ms() const override { return esp_timer_get_time() / 1000; }
};

class EspRandom : public hal::Random {
 public:
  std::uint32_t next_u32() override { return esp_random(); }
};

// Engine output -> the costume's screen, speaker and LEDs.
class DeviceOutputs : public hal::Outputs {
 public:
  void screen(const hal::ScreenCmd& c) override { display::show(c); }
  void sound(const hal::SoundCmd& c) override { audio::play(c); }
  void leds(const hal::LedCmd& c) override { leds::play(c); }
};

// Runtime settings: applied to the hardware immediately and kept in /data/settings.json.
class DeviceSettings : public api::SettingsStore {
 public:
  explicit DeviceSettings(config::Settings s) : s_(s) {}
  config::Settings get() override { return s_; }
  void set(const config::Settings& s) override {
    s_ = s;
    apply();
    storage::data().write("/settings.json", config::to_json(s_).dump());
  }
  void apply() {
    board_backlight_set(s_.brightness);
    audio::set_volume(s_.volume);
    leds::apply_settings(s_);
  }

 private:
  config::Settings s_;
};

config::DeviceConfig load_config() {
  hal::FileSystem& fs = storage::data();
  auto text = fs.read("/config.json");
  if (!text) {
    // First boot: write the defaults so they're easy to find and edit.
    config::DeviceConfig defaults;
    fs.write("/config.json", config::to_json(defaults, /*include_secrets=*/true).dump(2));
    ESP_LOGI(TAG, "wrote default /data/config.json");
    return defaults;
  }
  auto r = config::parse_config(*text);
  for (const auto& w : r.warnings) ESP_LOGW(TAG, "config.json: %s", w.c_str());
  // Settings changed from the phone override config.json.
  if (auto s = fs.read("/settings.json")) {
    auto p = config::apply_patch(r.config.settings, json::parse(*s, nullptr, false));
    if (p.ok) r.config.settings = p.settings;
  }
  return r.config;
}

// The status bar clock follows the engine's wall clock (set from the phone).
void update_clock(const json& msg) {
  const std::string type = msg.value("type", "");
  if (type != "state" && type != "hello") return;
  const json& st = msg["status"];
  if (st.contains("wall_clock_ms") && st["wall_clock_ms"].is_number())
    display::set_wall_clock(st["wall_clock_ms"].get<std::int64_t>(), st.value("utc_offset_min", 0));
}

void update_idle_board(const json& msg) {
  const json* board = nullptr;
  if (msg.value("type", "") == "scores") board = &msg;
  else if (msg.value("type", "") == "hello") board = &msg["board"];
  if (!board || !board->contains("board")) return;
  std::vector<display::BoardLine> top;
  for (const auto& row : (*board)["board"]) {
    if (top.size() == 5) break;
    top.push_back({row["rank"].get<int>(), row["name"].get<std::string>(), row["points"].get<std::int64_t>()});
  }
  display::set_board(std::move(top));
}

}  // namespace

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "ORCA reader booting");
  esp_err_t nvs = nvs_flash_init();
  if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs);

  // Display first, so there's something on screen while the rest boots.
  esp_lcd_panel_handle_t panel = nullptr;
  ESP_ERROR_CHECK(board_display_init(&panel));
  ESP_ERROR_CHECK(display::start(panel));
  ESP_ERROR_CHECK(board_backlight_set(80));
  display::show({hal::ScreenKind::Idle, hal::Tone::Neutral, "", "Starting up", "", 0, ""});

  ESP_ERROR_CHECK(storage::mount());
  const config::DeviceConfig cfg = load_config();
  static DeviceSettings settings(cfg.settings);

  leds::start(BOARD_PIN_LED_DATA, cfg.settings, [](std::uint8_t r, std::uint8_t g, std::uint8_t b) { display::set_led_preview(r, g, b); });
  audio::start(cfg.settings.volume);
  settings.apply();

  static EspClock clock;
  static EspRandom rng;
  static DeviceOutputs outputs;
  static game::GameRegistry registry;
  games::register_builtin_games(registry);
  static game::Engine engine(clock, rng, outputs, registry);
  engine.set_open_enrollment(cfg.open_enrollment);

  static app::GameTask game_task(engine, storage::data(), [](const json& msg) {
    net::broadcast(msg.dump());
    update_idle_board(msg);
    update_clock(msg);
  });
  game_task.start();

  api::ApiOptions options;
  options.admin_pin = cfg.admin_pin;
  static api::Api api(game_task, settings, clock, rng, options);

  net::start_wifi(cfg.wifi, cfg.hostname);
  display::set_footer(net::summary());
  net::start_http(api, [] { return api.hello().dump(); });
  update_idle_board(api.hello());

  app::start_console(api, cfg.admin_pin);
  ESP_LOGI(TAG, "READY %s, internal heap %u KB, PSRAM %u KB free", net::summary().c_str(),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}
