// Serial console (UART0, 115200). Lets the HIL tests and you drive the costume over USB
// without Wi-Fi:
//   api GET /api/status
//   api POST /api/game/select {"id":"fare_paid"}
//   tap 04A1B2C3D4E5F6
//   heap
//   wifi hotspot "My iPhone" "hotspot password"   (then restarts; `wifi own` undoes it)
// Responses are single lines starting with "API <status> " so scripts can parse them.

#include <cstdio>
#include <functional>
#include <string>

#include "console.hpp"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "orca/config/config.hpp"
#include "orca/storage/storage.hpp"

namespace orca::app {

namespace {

api::Api* g_api = nullptr;

std::string join(int argc, char** argv, int from) {
  std::string s;
  for (int i = from; i < argc; ++i) {
    if (i > from) s += ' ';
    s += argv[i];
  }
  return s;
}

api::ApiResponse call(const std::string& method, const std::string& target, const std::string& body) {
  api::ApiRequest req;
  req.method = method;
  const auto q = target.find('?');
  req.path = target.substr(0, q);
  if (q != std::string::npos) req.query = target.substr(q + 1);
  req.body = body;
  return g_api->handle(req);
}

int cmd_api(int argc, char** argv) {
  if (argc < 3) {
    std::printf("usage: api <METHOD> <path> [json]\n");
    return 1;
  }
  const auto r = call(argv[1], argv[2], join(argc, argv, 3));
  std::printf("API %d %s\n", r.status, r.body.c_str());
  return 0;
}

int cmd_tap(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: tap <uid>\n");
    return 1;
  }
  const auto r = call("POST", "/api/sim/tap", std::string("{\"uid\":\"") + argv[1] + "\"}");
  std::printf("API %d %s\n", r.status, r.body.c_str());
  return 0;
}

int cmd_heap(int, char**) {
  std::printf("HEAP internal %u KB free (min %u KB), psram %u KB free\n",
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
              static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024),
              static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
  return 0;
}

nlohmann::json read_config_json() {
  const auto text = storage::data().read("/config.json");
  auto j = nlohmann::json::parse(text.value_or("{}"), nullptr, false);
  return j.is_discarded() || !j.is_object() ? nlohmann::json::object() : j;
}

// Saves `j` as /data/config.json if validation keeps what was asked for (`kept`), then
// restarts so it takes effect. Settings here never go in git.
int save_config_and_restart(const char* what, const nlohmann::json& j,
                            const std::function<bool(const config::DeviceConfig&)>& kept) {
  const auto r = config::parse_config(j.dump());
  if (!kept(r.config)) {
    std::printf("%s not saved:", what);
    for (const auto& warning : r.warnings) std::printf(" %s;", warning.c_str());
    std::printf("\n");
    return 1;
  }
  if (!storage::data().write("/config.json", config::to_json(r.config, /*include_secrets=*/true).dump(2))) {
    std::printf("%s not saved: storage write failed\n", what);
    return 1;
  }
  std::printf("%s saved, restarting\n", what);
  vTaskDelay(pdMS_TO_TICKS(200));
  esp_restart();
}

int cmd_wifi(int argc, char** argv) {
  auto j = read_config_json();
  const std::string sub = argc > 1 ? argv[1] : "";
  if (sub.empty()) {
    const auto c = config::parse_config(j.dump()).config.wifi;
    std::printf("WIFI mode %s%s\n", c.mode == "sta" ? "hotspot " : "own network ",
                (c.mode == "sta" ? c.sta_ssid : c.ap_ssid).c_str());
    return 0;
  }
  if (sub == "hotspot" && argc == 4) {
    j["wifi"]["mode"] = "sta";
    j["wifi"]["sta_ssid"] = argv[2];
    j["wifi"]["sta_password"] = argv[3];
    const std::string ssid = argv[2], password = argv[3];
    return save_config_and_restart("WIFI", j, [&](const config::DeviceConfig& c) {
      return c.wifi.mode == "sta" && c.wifi.sta_ssid == ssid && c.wifi.sta_password == password;
    });
  }
  if (sub == "own" && argc == 2) {
    j["wifi"]["mode"] = "ap";
    return save_config_and_restart("WIFI", j, [](const config::DeviceConfig& c) { return c.wifi.mode == "ap"; });
  }
  std::printf("usage: wifi | wifi hotspot <ssid> <password> | wifi own\n");
  return 1;
}

}  // namespace

void start_console(api::Api& api) {
  g_api = &api;

  esp_console_repl_t* repl = nullptr;
  esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
  repl_cfg.prompt = "orca> ";
  repl_cfg.max_cmdline_length = 512;
  repl_cfg.task_stack_size = 8192;
  esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl));

  const esp_console_cmd_t cmds[] = {
      {.command = "api", .help = "api <METHOD> <path> [json]: call the web API", .hint = nullptr, .func = cmd_api, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr},
      {.command = "tap", .help = "tap <uid>: simulate a card tap", .hint = nullptr, .func = cmd_tap, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr},
      {.command = "wifi", .help = "wifi | wifi hotspot <ssid> <password> | wifi own: show or set Wi-Fi (restarts)", .hint = nullptr, .func = cmd_wifi, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr},
      {.command = "heap", .help = "show free memory", .hint = nullptr, .func = cmd_heap, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr},
  };
  for (const auto& c : cmds) ESP_ERROR_CHECK(esp_console_cmd_register(&c));
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

}  // namespace orca::app
