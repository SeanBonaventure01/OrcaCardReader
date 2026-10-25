#pragma once

// The costume's screen, styled after the real ORCA reader: black, with a status bar (date,
// label, time), the orca/payment artwork and a yellow arrow pointing down at the card reader.
// All functions are thread-safe; they post to the UI task, which owns every LVGL object.

#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "orca/hal/outputs.hpp"

namespace orca::display {

struct BoardLine {
  int rank;
  std::string name;
  std::int64_t points;
};

esp_err_t start(esp_lcd_panel_handle_t panel);

void show(const hal::ScreenCmd& cmd);
void set_board(std::vector<BoardLine> top);
// Small print on the top-riders page, e.g. "Wi-Fi ORCA-READER - http://192.168.4.1".
void set_footer(const std::string& text);
// Confetti / fireworks over paid-fare screens.
void set_effects(bool on);
// Wall clock for the status bar (the device has no RTC; the phone UI provides it).
void set_wall_clock(std::int64_t epoch_ms, int utc_offset_min);
// Color of the optional on-screen LED preview bar (CONFIG_ORCA_LED_PREVIEW).
void set_led_preview(std::uint8_t r, std::uint8_t g, std::uint8_t b);

// Replaces characters the built-in fonts can't draw ("·" -> "-", other non-ASCII -> "?").
std::string ascii_only(const std::string& s);

// "Oct 31, 2026" and "1:07 pm" for a local time in seconds since the epoch.
std::string format_date(std::int64_t local_seconds);
std::string format_time(std::int64_t local_seconds);

}  // namespace orca::display
