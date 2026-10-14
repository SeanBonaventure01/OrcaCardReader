#pragma once

// The costume's screen: LVGL 9 on the RGB panel. All functions are thread-safe; they post
// to the UI task, which owns every LVGL object.

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
// Footer text on the idle screen, e.g. "Wi-Fi ORCA-READER - orca.local".
void set_footer(const std::string& text);
// Color of the on-screen LED preview bar (mirrors the WS2812 strip).
void set_led_preview(std::uint8_t r, std::uint8_t g, std::uint8_t b);

// Replaces characters the built-in fonts can't draw ("·" -> "-", other non-ASCII -> "?").
std::string ascii_only(const std::string& s);

}  // namespace orca::display
