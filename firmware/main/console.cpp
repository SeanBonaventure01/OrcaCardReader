// Serial console (UART0, 115200). Lets the HIL tests and you drive the costume over USB
// without Wi-Fi:
//   api GET /api/status
//   api POST /api/game/select {"id":"fare_paid"}
//   tap 04A1B2C3D4E5F6
//   heap
// Responses are single lines starting with "API <status> " so scripts can parse them.

#include <cstdio>
#include <string>

#include "console.hpp"
#include "esp_console.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

namespace orca::app {

namespace {

api::Api* g_api = nullptr;
std::string g_token;

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
  req.token = g_token;
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

}  // namespace

void start_console(api::Api& api, const std::string& admin_pin) {
  g_api = &api;
  // The console is trusted (it needs a USB cable), so it logs in with the configured PIN.
  const auto login = call("POST", "/api/login", "{\"pin\":\"" + admin_pin + "\"}");
  if (login.status == 200) g_token = nlohmann::json::parse(login.body)["token"];

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
      {.command = "heap", .help = "show free memory", .hint = nullptr, .func = cmd_heap, .argtable = nullptr, .func_w_context = nullptr, .context = nullptr},
  };
  for (const auto& c : cmds) ESP_ERROR_CHECK(esp_console_cmd_register(&c));
  ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

}  // namespace orca::app
