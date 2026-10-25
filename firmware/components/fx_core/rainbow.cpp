#include "orca/fx/rainbow.hpp"

#include <cmath>

namespace orca::fx {

std::uint32_t hsv(float h, float s, float v) {
  h -= std::floor(h);
  const float c = v * s, x = c * (1 - std::fabs(std::fmod(h * 6, 2.0f) - 1)), m = v - c;
  float r = 0, g = 0, b = 0;
  switch (static_cast<int>(h * 6) % 6) {
    case 0: r = c, g = x; break;
    case 1: r = x, g = c; break;
    case 2: g = c, b = x; break;
    case 3: g = x, b = c; break;
    case 4: r = x, b = c; break;
    default: r = c, b = x; break;
  }
  auto byte = [](float f) { return static_cast<std::uint32_t>(std::lround(std::fmin(1.0f, std::fmax(0.0f, f)) * 255)); };
  return byte(r + m) << 16 | byte(g + m) << 8 | byte(b + m);
}

std::vector<std::uint32_t> rainbow_ring(std::size_t n, std::int64_t t_ms, std::int64_t period_ms) {
  std::vector<std::uint32_t> out(n);
  const float spin = static_cast<float>(t_ms % period_ms) / static_cast<float>(period_ms);
  for (std::size_t i = 0; i < n; ++i) {
    const float pos = static_cast<float>(i) / static_cast<float>(n);
    // A bright spot runs backwards around the ring three times as fast.
    const float pulse = 0.5f + 0.5f * std::cos(2 * 3.14159265f * (pos + 3 * spin));
    out[i] = hsv(pos - spin, 1.0f, 0.55f + 0.45f * pulse);
  }
  return out;
}

}  // namespace orca::fx
