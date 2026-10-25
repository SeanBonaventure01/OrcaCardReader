#pragma once

// Colours for the on-screen "LED ring" during Rush Hour: a rainbow chasing around the border.

#include <cstdint>
#include <vector>

namespace orca::fx {

// hue in turns (0-1, wraps), saturation and value 0-1 -> 0xRRGGBB.
std::uint32_t hsv(float hue, float saturation, float value);

// `segments` colours around a ring at `t_ms`: one full rainbow around the ring, rotating
// once every `period_ms`, with a brightness pulse running the other way so it sparkles.
std::vector<std::uint32_t> rainbow_ring(std::size_t segments, std::int64_t t_ms, std::int64_t period_ms = 2000);

}  // namespace orca::fx
