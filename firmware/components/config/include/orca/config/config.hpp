#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace orca::config {

using json = nlohmann::json;

// Settings the phone can change at runtime (PATCH /api/settings). Persisted by the device.
struct Settings {
  int volume = 70;           // 0-100 %
  int brightness = 80;       // display backlight, 5-100 %
  int led_brightness = 25;   // 0-100 %, further capped by led_max_ma
  int led_count = 30;        // 0-300 pixels
  int led_max_ma = 450;      // 50-2000 mA budget for the strip
  std::string voice = "off"; // voice lines after the tap sound: "off" or "profanity"
  bool effects = true;       // confetti / fireworks on paid fares
};

// The voice packs a costume can speak (Settings::voice), "off" first.
const std::vector<std::string>& voice_packs();

json to_json(const Settings& s);

struct PatchResult {
  bool ok = false;
  Settings settings;
  std::string error;
};

// Applies a partial update. Unknown keys and out-of-range values are errors (nothing changes).
PatchResult apply_patch(const Settings& current, const json& patch);

struct WifiConfig {
  std::string mode = "ap";  // "ap" or "sta" (sta falls back to ap)
  std::string ap_ssid = "ORCA-READER";
  std::string ap_password = "fareplease";  // WPA2 needs 8-63 chars
  std::string sta_ssid;
  std::string sta_password;
};

struct DeviceConfig {
  WifiConfig wifi;
  std::string hostname = "orca";  // orca.local
  bool open_enrollment = false;  // only programmed cards (orca-card-game-NN) play
  Settings settings;
};

struct ConfigResult {
  DeviceConfig config;
  // Problems found. Invalid values fall back to defaults so the costume always boots.
  std::vector<std::string> warnings;
};

// Parses config.json text. Never fails: bad input yields defaults plus warnings.
ConfigResult parse_config(const std::string& text);

json to_json(const DeviceConfig& c, bool include_secrets);

}  // namespace orca::config
