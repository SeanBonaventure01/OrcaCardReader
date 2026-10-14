#pragma once

#include <functional>

#include "esp_err.h"
#include "orca/config/config.hpp"
#include "orca/hal/outputs.hpp"

namespace orca::leds {

// Starts the LED task on `gpio` (WS2812 over RMT). `preview` receives the first pixel's color
// every frame (the on-screen LED bar). Failure to create the strip is not fatal: effects still
// render to the preview.
esp_err_t start(int gpio, const config::Settings& settings, std::function<void(std::uint8_t, std::uint8_t, std::uint8_t)> preview);

void play(const hal::LedCmd& cmd);  // thread-safe
void apply_settings(const config::Settings& settings);

}  // namespace orca::leds
