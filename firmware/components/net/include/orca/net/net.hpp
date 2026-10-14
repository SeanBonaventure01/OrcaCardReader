#pragma once

#include <functional>
#include <string>

#include "esp_err.h"
#include "orca/api/api.hpp"
#include "orca/config/config.hpp"

namespace orca::net {

// Brings up Wi-Fi (station mode falls back to an access point after 20 s), mDNS
// (<hostname>.local) and, in AP mode, a captive-portal DNS that answers every name with us.
esp_err_t start_wifi(const config::WifiConfig& wifi, const std::string& hostname);

// "192.168.4.1" etc., or "" before an address is assigned.
std::string ip();
// Human summary for the costume's idle screen, e.g. "Wi-Fi ORCA-READER - http://orca.local".
std::string summary();

// Serves the embedded phone UI, /api/* through `api`, and /ws push. `hello` produces the
// first message for each new WebSocket client.
esp_err_t start_http(api::Api& api, std::function<std::string()> hello);

// Sends a text message to every WebSocket client. Thread-safe.
void broadcast(const std::string& text);

}  // namespace orca::net
