#include <algorithm>
#include <cstring>
#include <mutex>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "lwip/sockets.h"
#include "mdns.h"
#include "orca/net/net.hpp"

namespace orca::net {

namespace {

constexpr const char* TAG = "wifi";
constexpr std::int64_t kRescueAfterUs = 20 * 1000 * 1000;   // no hotspot for this long: open our own network
constexpr std::int64_t kRescueRetryUs = 10 * 1000 * 1000;   // while it's open, retry the hotspot this often
constexpr EventBits_t kGotIp = BIT0;

EventGroupHandle_t g_events;
esp_netif_t* g_ap = nullptr;
esp_netif_t* g_sta = nullptr;
std::mutex g_summary_mutex;
std::string g_summary;
std::function<void(const std::string&)> g_on_status;
std::string g_hostname;
config::WifiConfig g_wifi;
bool g_ap_mode = false;     // the costume's own network is up (AP mode, or rescue in STA mode)
bool g_dns_started = false;
esp_timer_handle_t g_rescue_timer = nullptr;
esp_timer_handle_t g_retry_timer = nullptr;

// Wi-Fi SSIDs/passwords are fixed-size byte fields that needn't be NUL-terminated when full.
template <std::size_t N>
void copy_field(std::uint8_t (&dst)[N], const std::string& src) {
  std::memset(dst, 0, N);
  std::memcpy(dst, src.data(), std::min(src.size(), N));
}

void set_summary(const std::string& s) {
  {
    std::lock_guard<std::mutex> lock(g_summary_mutex);
    g_summary = s;
  }
  ESP_LOGI(TAG, "%s", s.c_str());
  if (g_on_status) g_on_status(s);
}

void start_rescue_ap();
void stop_rescue_ap();

const char* disconnect_reason(std::uint8_t r) {
  switch (r) {
    case WIFI_REASON_NO_AP_FOUND: return "hotspot not found (off, out of range, or 5 GHz only)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT: return "wrong password?";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD: return "hotspot security not supported (try WPA2)";
    default: return "disconnected";
  }
}

void on_event(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    // Log why, but only when the reason changes (retries repeat every few seconds).
    static std::uint8_t last_reason = 0;
    const std::uint8_t reason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
    if (reason != last_reason)
      ESP_LOGW(TAG, "hotspot %s: %s (reason %u)", g_wifi.sta_ssid.c_str(), disconnect_reason(reason), reason);
    last_reason = reason;
    const bool was_connected = xEventGroupGetBits(g_events) & kGotIp;
    xEventGroupClearBits(g_events, kGotIp);
    if (was_connected) {
      set_summary("Lost hotspot " + g_wifi.sta_ssid + ", reconnecting...");
      esp_timer_start_once(g_rescue_timer, kRescueAfterUs);
    }
    // With our own network up, retry less often: each attempt scans away from its channel.
    if (g_ap_mode) esp_timer_start_once(g_retry_timer, kRescueRetryUs);
    else esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(g_events, kGotIp);
    esp_timer_stop(g_rescue_timer);
    if (g_ap_mode) stop_rescue_ap();
    set_summary("http://" + g_hostname + ".local - " + ip());
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
    ESP_LOGI(TAG, "phone joined the costume's Wi-Fi");
  }
}

// Answers every DNS A query with the AP's address, so phones show a "sign in" page that
// opens the operator UI.
void captive_dns_task(void*) {
  const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(53);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    ESP_LOGW(TAG, "captive DNS bind failed");
    close(sock);
    vTaskDelete(nullptr);
  }
  esp_netif_ip_info_t info;
  esp_netif_get_ip_info(g_ap, &info);
  std::uint8_t buf[512];
  while (true) {
    sockaddr_in from = {};
    socklen_t from_len = sizeof from;
    const int n = recvfrom(sock, buf, sizeof buf - 16, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < 12) continue;
    // Find the end of the question name.
    int q = 12;
    while (q < n && buf[q] != 0) q += buf[q] + 1;
    q += 5;  // zero byte + QTYPE + QCLASS
    if (q > n) continue;
    buf[2] = 0x84;  // response, authoritative
    buf[3] = 0x00;
    buf[6] = 0;  // ANCOUNT = 1
    buf[7] = 1;
    buf[8] = buf[9] = buf[10] = buf[11] = 0;
    const std::uint8_t answer[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4};
    std::memcpy(buf + q, answer, sizeof answer);
    std::memcpy(buf + q + sizeof answer, &info.ip.addr, 4);
    sendto(sock, buf, q + sizeof answer + 4, 0, reinterpret_cast<sockaddr*>(&from), from_len);
  }
}

wifi_config_t ap_config(const config::WifiConfig& w) {
  wifi_config_t cfg = {};
  copy_field(cfg.ap.ssid, w.ap_ssid);
  cfg.ap.ssid_len = w.ap_ssid.size();
  copy_field(cfg.ap.password, w.ap_password);
  cfg.ap.channel = 6;
  cfg.ap.max_connection = 8;
  cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  return cfg;
}

void start_dns_once() {
  if (g_dns_started) return;
  g_dns_started = true;
  xTaskCreate(captive_dns_task, "dns", 3072, nullptr, 3, nullptr);
}

void start_ap(const config::WifiConfig& w) {
  wifi_config_t cfg = ap_config(w);
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  g_ap_mode = true;
  start_dns_once();
  set_summary("Wi-Fi " + w.ap_ssid + " - http://" + ip());
}

// Hotspot mode, but the hotspot isn't there: open our own network as well, so the costume is
// still reachable, and keep looking for the hotspot.
void start_rescue_ap() {
  if (g_ap_mode) return;
  wifi_config_t cfg = ap_config(g_wifi);
  esp_wifi_set_mode(WIFI_MODE_APSTA);
  esp_wifi_set_config(WIFI_IF_AP, &cfg);
  g_ap_mode = true;
  start_dns_once();
  set_summary("No hotspot " + g_wifi.sta_ssid + ". Wi-Fi " + g_wifi.ap_ssid + " - http://" + ip());
}

void stop_rescue_ap() {
  esp_timer_stop(g_retry_timer);
  esp_wifi_set_mode(WIFI_MODE_STA);
  g_ap_mode = false;
}

}  // namespace

esp_err_t start_wifi(const config::WifiConfig& w, const std::string& hostname) {
  g_hostname = hostname;
  g_events = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  g_ap = esp_netif_create_default_wifi_ap();
  g_sta = esp_netif_create_default_wifi_sta();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&init));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, nullptr));

  if (w.mode == "sta") {
    g_wifi = w;
    const esp_timer_create_args_t rescue = {.callback = [](void*) { start_rescue_ap(); },
                                            .arg = nullptr, .dispatch_method = ESP_TIMER_TASK, .name = "wifi_rescue",
                                            .skip_unhandled_events = true};
    const esp_timer_create_args_t retry = {.callback = [](void*) { esp_wifi_connect(); },
                                           .arg = nullptr, .dispatch_method = ESP_TIMER_TASK, .name = "wifi_retry",
                                           .skip_unhandled_events = true};
    esp_timer_create(&rescue, &g_rescue_timer);
    esp_timer_create(&retry, &g_retry_timer);
    wifi_config_t cfg = {};
    copy_field(cfg.sta.ssid, w.sta_ssid);
    copy_field(cfg.sta.password, w.sta_password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_set_hostname(g_sta, hostname.c_str());
    // Don't hold up boot: the hotspot often comes up after the costume does.
    set_summary("Joining hotspot " + w.sta_ssid + "...");
    esp_timer_start_once(g_rescue_timer, kRescueAfterUs);
    esp_wifi_connect();
  } else {
    start_ap(w);
  }

  if (mdns_init() == ESP_OK) {
    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set("ORCA Reader costume");
    mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
  }
  return ESP_OK;
}

std::string ip() {
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(g_ap_mode ? g_ap : g_sta, &info) != ESP_OK || info.ip.addr == 0) return "";
  char buf[16];
  esp_ip4addr_ntoa(&info.ip, buf, sizeof buf);
  return buf;
}

std::string summary() {
  std::lock_guard<std::mutex> lock(g_summary_mutex);
  return g_summary;
}

void on_status(std::function<void(const std::string&)> fn) { g_on_status = std::move(fn); }

}  // namespace orca::net
