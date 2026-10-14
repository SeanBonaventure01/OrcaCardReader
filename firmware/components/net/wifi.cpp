#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
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
constexpr int kStaTimeoutMs = 20000;
constexpr EventBits_t kGotIp = BIT0;

EventGroupHandle_t g_events;
esp_netif_t* g_ap = nullptr;
esp_netif_t* g_sta = nullptr;
std::string g_summary;
std::string g_hostname;
bool g_ap_mode = false;

void on_event(void*, esp_event_base_t base, int32_t id, void*) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(g_events, kGotIp);
    esp_wifi_connect();
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(g_events, kGotIp);
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

void start_ap(const config::WifiConfig& w) {
  wifi_config_t cfg = {};
  std::strncpy(reinterpret_cast<char*>(cfg.ap.ssid), w.ap_ssid.c_str(), sizeof cfg.ap.ssid);
  cfg.ap.ssid_len = w.ap_ssid.size();
  std::strncpy(reinterpret_cast<char*>(cfg.ap.password), w.ap_password.c_str(), sizeof cfg.ap.password);
  cfg.ap.channel = 6;
  cfg.ap.max_connection = 8;
  cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
  ESP_ERROR_CHECK(esp_wifi_start());
  g_ap_mode = true;
  xTaskCreate(captive_dns_task, "dns", 3072, nullptr, 3, nullptr);
  g_summary = "Wi-Fi " + w.ap_ssid + " - http://" + ip();
  ESP_LOGI(TAG, "access point %s up at %s", w.ap_ssid.c_str(), ip().c_str());
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
    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), w.sta_ssid.c_str(), sizeof cfg.sta.ssid);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), w.sta_password.c_str(), sizeof cfg.sta.password);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_netif_set_hostname(g_sta, hostname.c_str());
    esp_wifi_connect();
    if (xEventGroupWaitBits(g_events, kGotIp, pdFALSE, pdTRUE, pdMS_TO_TICKS(kStaTimeoutMs)) & kGotIp) {
      g_summary = "Wi-Fi " + w.sta_ssid + " - http://" + hostname + ".local";
      ESP_LOGI(TAG, "joined %s as %s", w.sta_ssid.c_str(), ip().c_str());
    } else {
      ESP_LOGW(TAG, "could not join %s; starting access point instead", w.sta_ssid.c_str());
      esp_wifi_stop();
      start_ap(w);
    }
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

std::string summary() { return g_summary; }

}  // namespace orca::net
