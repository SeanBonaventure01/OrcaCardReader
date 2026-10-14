#include <gtest/gtest.h>

#include "orca/config/config.hpp"

using namespace orca::config;

namespace {
bool has_warning(const ConfigResult& r, const std::string& needle) {
  for (const auto& w : r.warnings)
    if (w.find(needle) != std::string::npos) return true;
  return false;
}
}  // namespace

TEST(Config, EmptyObjectGivesDefaultsWithoutWarnings) {
  auto r = parse_config("{}");
  EXPECT_TRUE(r.warnings.empty());
  EXPECT_EQ(r.config.wifi.mode, "ap");
  EXPECT_EQ(r.config.wifi.ap_ssid, "ORCA-READER");
  EXPECT_EQ(r.config.admin_pin, "1031");
  EXPECT_EQ(r.config.settings.volume, 70);
}

TEST(Config, GarbageGivesDefaultsWithWarning) {
  for (const char* text : {"", "not json", "[1,2]", "{\"a\": "}) {
    auto r = parse_config(text);
    EXPECT_TRUE(has_warning(r, "not a JSON object")) << text;
    EXPECT_EQ(r.config.hostname, "orca");
  }
}

TEST(Config, ParsesEverything) {
  auto r = parse_config(R"({
    // comments are allowed
    "wifi": {"mode": "sta", "sta_ssid": "Party", "sta_password": "boo", "ap_password": "spooky123"},
    "hostname": "orca-2",
    "admin_pin": "666999",
    "open_enrollment": false,
    "settings": {"volume": 40, "brightness": 100, "led_count": 60, "led_max_ma": 900}
  })");
  EXPECT_TRUE(r.warnings.empty()) << r.warnings.front();
  EXPECT_EQ(r.config.wifi.mode, "sta");
  EXPECT_EQ(r.config.wifi.sta_ssid, "Party");
  EXPECT_EQ(r.config.wifi.ap_password, "spooky123");
  EXPECT_EQ(r.config.hostname, "orca-2");
  EXPECT_EQ(r.config.admin_pin, "666999");
  EXPECT_FALSE(r.config.open_enrollment);
  EXPECT_EQ(r.config.settings.volume, 40);
  EXPECT_EQ(r.config.settings.led_count, 60);
  EXPECT_EQ(r.config.settings.led_brightness, 25);  // untouched default
}

TEST(Config, InvalidValuesFallBackIndividually) {
  auto r = parse_config(R"({
    "wifi": {"mode": "mesh", "ap_password": "short"},
    "hostname": "bad host!",
    "admin_pin": "12ab",
    "open_enrollment": "yes",
    "settings": {"volume": 150, "brightness": 50, "leds": 3}
  })");
  EXPECT_EQ(r.config.wifi.mode, "ap");
  EXPECT_EQ(r.config.wifi.ap_password, "fareplease");
  EXPECT_EQ(r.config.hostname, "orca");
  EXPECT_EQ(r.config.admin_pin, "1031");
  EXPECT_TRUE(r.config.open_enrollment);
  EXPECT_EQ(r.config.settings.volume, 70);
  EXPECT_EQ(r.config.settings.brightness, 50);  // good value alongside bad ones still applies
  EXPECT_TRUE(has_warning(r, "mode"));
  EXPECT_TRUE(has_warning(r, "ap_password"));
  EXPECT_TRUE(has_warning(r, "hostname"));
  EXPECT_TRUE(has_warning(r, "admin_pin"));
  EXPECT_TRUE(has_warning(r, "open_enrollment"));
  EXPECT_TRUE(has_warning(r, "volume"));
  EXPECT_TRUE(has_warning(r, "unknown setting: leds"));
}

TEST(Config, StaWithoutSsidFallsBackToAp) {
  auto r = parse_config(R"({"wifi": {"mode": "sta"}})");
  EXPECT_EQ(r.config.wifi.mode, "ap");
  EXPECT_TRUE(has_warning(r, "sta_ssid is empty"));
}

TEST(Config, UnknownTopLevelKeysWarn) {
  auto r = parse_config(R"({"colour": "red"})");
  EXPECT_TRUE(has_warning(r, "unknown key ignored: colour"));
}

TEST(Config, SecretsOnlyWhenAsked) {
  DeviceConfig c;
  c.wifi.sta_password = "pw";
  EXPECT_FALSE(to_json(c, false).dump().find("pw\"") != std::string::npos);
  EXPECT_FALSE(to_json(c, false).contains("admin_pin"));
  EXPECT_EQ(to_json(c, true)["wifi"]["sta_password"], "pw");
}

TEST(SettingsPatch, AppliesValidKeys) {
  auto r = apply_patch(Settings{}, {{"volume", 10}, {"brightness", 5}});
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.settings.volume, 10);
  EXPECT_EQ(r.settings.brightness, 5);
}

TEST(SettingsPatch, AnyBadKeyRejectsWholePatch) {
  Settings s;
  for (const auto& bad : {json{{"volume", 10}, {"brightness", 0}}, json{{"volume", "loud"}}, json{{"volume", 1.5}},
                          json{{"nope", 1}}, json::array(), json{{"led_max_ma", 5000}}}) {
    auto r = apply_patch(s, bad);
    EXPECT_FALSE(r.ok) << bad.dump();
    EXPECT_EQ(r.settings.volume, 70) << bad.dump();
  }
}
