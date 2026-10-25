#pragma once

#include <functional>
#include <string>

#include "esp_err.h"
#include "orca/api/api.hpp"
#include "orca/config/config.hpp"

namespace orca::net {

// Brings up Wi-Fi, mDNS (<hostname>.local) and, while our own network is up, a captive-portal
// DNS that answers every name with us. Mode "sta" joins a phone hotspot without blocking boot;
// if it isn't found within 20 s the costume opens its own network as well and keeps retrying,
// closing it again once the hotspot is back. Mode "ap" is only the costume's own network.
esp_err_t start_wifi(const config::WifiConfig& wifi, const std::string& hostname);

// Called (from Wi-Fi/timer tasks) whenever summary() changes. Set before start_wifi.
void on_status(std::function<void(const std::string&)> fn);

// "192.168.4.1" etc., or "" before an address is assigned.
std::string ip();
// Human summary for the costume's idle screen, e.g. "http://orca.local - 172.20.10.3".
std::string summary();

// Serves the embedded phone UI, /api/* through `api`, and /ws push. `hello` produces the
// first message for each new WebSocket client.
esp_err_t start_http(api::Api& api, std::function<std::string()> hello);

// Sends a text message to every WebSocket client. Thread-safe.
void broadcast(const std::string& text);

}  // namespace orca::net
