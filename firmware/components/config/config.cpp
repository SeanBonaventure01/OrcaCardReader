#include "orca/config/config.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>

namespace orca::config {

namespace {

struct IntField {
  int Settings::*member;
  int min;
  int max;
};

const std::map<std::string, IntField>& settings_fields() {
  static const std::map<std::string, IntField> fields{
      {"volume", {&Settings::volume, 0, 100}},
      {"brightness", {&Settings::brightness, 5, 100}},
      {"led_brightness", {&Settings::led_brightness, 0, 100}},
      {"led_count", {&Settings::led_count, 0, 300}},
      {"led_max_ma", {&Settings::led_max_ma, 50, 2000}},
  };
  return fields;
}

bool is_integral(const json& v) {
  return v.is_number_integer() || (v.is_number_float() && v.get<double>() == static_cast<double>(static_cast<long long>(v.get<double>())));
}

bool valid_hostname(const std::string& h) {
  if (h.empty() || h.size() > 32 || h.front() == '-' || h.back() == '-') return false;
  for (char c : h)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-')) return false;
  return true;
}

}  // namespace

const std::vector<std::string>& voice_packs() {
  static const std::vector<std::string> packs{"off", "profanity"};
  return packs;
}

json to_json(const Settings& s) {
  json j;
  for (const auto& [key, f] : settings_fields()) j[key] = s.*(f.member);
  j["voice"] = s.voice;
  j["effects"] = s.effects;
  return j;
}

PatchResult apply_patch(const Settings& current, const json& patch) {
  PatchResult r;
  r.settings = current;
  if (!patch.is_object()) {
    r.error = "settings patch must be an object";
    return r;
  }
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.key() == "voice") {
      const auto& packs = voice_packs();
      if (!it->is_string() || std::find(packs.begin(), packs.end(), it->get<std::string>()) == packs.end()) {
        std::string list;
        for (const auto& p : packs) list += (list.empty() ? "" : ", ") + p;
        r.error = "voice must be one of: " + list;
        r.settings = current;
        return r;
      }
      r.settings.voice = it->get<std::string>();
      continue;
    }
    if (it.key() == "effects") {
      if (!it->is_boolean()) {
        r.error = "effects must be true or false";
        r.settings = current;
        return r;
      }
      r.settings.effects = it->get<bool>();
      continue;
    }
    auto f = settings_fields().find(it.key());
    if (f == settings_fields().end()) {
      r.error = "unknown setting: " + it.key();
      r.settings = current;
      return r;
    }
    if (!is_integral(*it) || it->get<double>() < f->second.min || it->get<double>() > f->second.max) {
      r.error = it.key() + " must be an integer between " + std::to_string(f->second.min) + " and " +
                std::to_string(f->second.max);
      r.settings = current;
      return r;
    }
    r.settings.*(f->second.member) = static_cast<int>(it->get<double>());
  }
  r.ok = true;
  return r;
}

ConfigResult parse_config(const std::string& text) {
  ConfigResult r;
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
  if (j.is_discarded() || !j.is_object()) {
    r.warnings.push_back("config.json is not a JSON object; using defaults");
    return r;
  }
  DeviceConfig& c = r.config;
  auto warn = [&](const std::string& w) { r.warnings.push_back(w); };

  auto get_string = [&](const json& obj, const char* key, std::string& out, const std::function<bool(const std::string&)>& ok,
                        const std::string& what) {
    if (!obj.contains(key)) return;
    const json& v = obj.at(key);
    if (!v.is_string() || !ok(v.get<std::string>())) {
      warn(std::string(key) + ": " + what + "; using default");
      return;
    }
    out = v.get<std::string>();
  };

  static const char* kKnownTop[] = {"wifi", "hostname", "open_enrollment", "settings"};
  for (auto it = j.begin(); it != j.end(); ++it) {
    bool known = false;
    for (const char* k : kKnownTop) known |= it.key() == k;
    if (!known) warn("unknown key ignored: " + it.key());
  }

  if (j.contains("wifi")) {
    const json& w = j.at("wifi");
    if (!w.is_object()) {
      warn("wifi must be an object; using defaults");
    } else {
      get_string(w, "mode", c.wifi.mode, [](const std::string& m) { return m == "ap" || m == "sta"; },
                 "must be \"ap\" or \"sta\"");
      get_string(w, "ap_ssid", c.wifi.ap_ssid, [](const std::string& s) { return !s.empty() && s.size() <= 32; },
                 "must be 1-32 characters");
      get_string(w, "ap_password", c.wifi.ap_password,
                 [](const std::string& s) { return s.size() >= 8 && s.size() <= 63; }, "must be 8-63 characters");
      get_string(w, "sta_ssid", c.wifi.sta_ssid, [](const std::string& s) { return s.size() <= 32; },
                 "must be at most 32 characters");
      get_string(w, "sta_password", c.wifi.sta_password, [](const std::string& s) { return s.size() <= 63; },
                 "must be at most 63 characters");
      if (c.wifi.mode == "sta" && c.wifi.sta_ssid.empty()) {
        warn("wifi.mode is sta but sta_ssid is empty; using ap");
        c.wifi.mode = "ap";
      }
    }
  }
  get_string(j, "hostname", c.hostname, valid_hostname, "must be letters, digits and dashes");
  if (j.contains("open_enrollment")) {
    if (j.at("open_enrollment").is_boolean())
      c.open_enrollment = j.at("open_enrollment").get<bool>();
    else
      warn("open_enrollment must be true or false; using default");
  }
  if (j.contains("settings")) {
    const json& s = j.at("settings");
    if (!s.is_object()) {
      warn("settings must be an object; using defaults");
    } else {
      // Apply each key on its own so one bad value doesn't discard the rest.
      for (auto it = s.begin(); it != s.end(); ++it) {
        PatchResult p = apply_patch(c.settings, json{{it.key(), *it}});
        if (p.ok)
          c.settings = p.settings;
        else
          warn("settings." + p.error + "; using default");
      }
    }
  }
  return r;
}

json to_json(const DeviceConfig& c, bool include_secrets) {
  json wifi{{"mode", c.wifi.mode}, {"ap_ssid", c.wifi.ap_ssid}, {"sta_ssid", c.wifi.sta_ssid}};
  if (include_secrets) {
    wifi["ap_password"] = c.wifi.ap_password;
    wifi["sta_password"] = c.wifi.sta_password;
  }
  json j{{"wifi", wifi}, {"hostname", c.hostname}, {"open_enrollment", c.open_enrollment}, {"settings", to_json(c.settings)}};
  return j;
}

}  // namespace orca::config
