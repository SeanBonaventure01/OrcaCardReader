#pragma once

// LED effects as pure functions of time, plus a current limiter. The `leds` ESP-IDF
// component pushes rendered frames to WS2812s over RMT.

#include <cstdint>
#include <string>
#include <vector>

namespace orca::fx {

struct Rgb {
  std::uint8_t r = 0, g = 0, b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

using Frame = std::vector<Rgb>;

// Known effect ids (unknown ids render as "off").
//   off, idle_breathe, accept_flash, reject_flash, cooldown_pulse, bonus_chase,
//   event_strobe, fault_blink
bool is_known_effect(const std::string& id);

// Renders `effect` at `t_ms` since it started into `frame` (size = pixel count).
void render(const std::string& effect, std::int64_t t_ms, Frame& frame);

// Scales the whole frame by brightness (0-100 %).
void apply_brightness(Frame& frame, int percent);

// Estimated strip current: ~20 mA per fully-lit channel plus ~1 mA idle per pixel.
int estimate_ma(const Frame& frame);

// Scales the frame down (never up) so estimate_ma(frame) <= max_ma.
void limit_current(Frame& frame, int max_ma);

// Plays temporary effects over a base effect: tap flashes run for their duration and then
// the strip returns to the base (e.g. idle_breathe).
class LedAnimator {
 public:
  void set_base(const std::string& effect, std::int64_t now);
  // duration 0 = becomes the new base.
  void play(const std::string& effect, std::uint32_t duration_ms, std::int64_t now);
  void render_at(std::int64_t now, Frame& frame);
  const std::string& current(std::int64_t now) const;
  // True while anything is moving (the LED task can sleep otherwise).
  bool animating(std::int64_t now) const;

 private:
  std::string base_ = "off";
  std::int64_t base_start_ = 0;
  std::string overlay_;
  std::int64_t overlay_start_ = 0;
  std::int64_t overlay_end_ = 0;
};

}  // namespace orca::fx
