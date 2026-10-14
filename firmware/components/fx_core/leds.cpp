#include "orca/fx/leds.hpp"

#include <algorithm>
#include <cmath>

namespace orca::fx {

namespace {

constexpr double kPi = 3.14159265358979323846;

// ORCA palette.
constexpr Rgb kTeal{0, 150, 160};
constexpr Rgb kGreen{0, 220, 60};
constexpr Rgb kRed{230, 0, 0};
constexpr Rgb kAmber{255, 140, 0};
constexpr Rgb kGold{255, 200, 40};
constexpr Rgb kWhite{255, 255, 255};

Rgb scale(Rgb c, double k) {
  k = std::clamp(k, 0.0, 1.0);
  return {static_cast<std::uint8_t>(std::lround(c.r * k)), static_cast<std::uint8_t>(std::lround(c.g * k)),
          static_cast<std::uint8_t>(std::lround(c.b * k))};
}

void fill(Frame& f, Rgb c) { std::fill(f.begin(), f.end(), c); }

Rgb wheel(int pos) {  // 0-255 rainbow
  pos &= 255;
  if (pos < 85) return {static_cast<std::uint8_t>(255 - pos * 3), static_cast<std::uint8_t>(pos * 3), 0};
  if (pos < 170) {
    pos -= 85;
    return {0, static_cast<std::uint8_t>(255 - pos * 3), static_cast<std::uint8_t>(pos * 3)};
  }
  pos -= 170;
  return {static_cast<std::uint8_t>(pos * 3), 0, static_cast<std::uint8_t>(255 - pos * 3)};
}

}  // namespace

bool is_known_effect(const std::string& id) {
  static const char* known[] = {"off",          "idle_breathe", "accept_flash", "reject_flash",
                                "cooldown_pulse", "bonus_chase",  "event_strobe", "fault_blink"};
  return std::any_of(std::begin(known), std::end(known), [&](const char* k) { return id == k; });
}

void render(const std::string& effect, std::int64_t t, Frame& f) {
  if (t < 0) t = 0;
  if (effect == "idle_breathe") {
    // 4 s breath between 15% and 60%.
    const double k = 0.15 + 0.45 * (0.5 - 0.5 * std::cos(2 * kPi * (t % 4000) / 4000.0));
    fill(f, scale(kTeal, k));
  } else if (effect == "accept_flash") {
    // Full green, fading out over 1.5 s.
    fill(f, scale(kGreen, 1.0 - std::min<std::int64_t>(t, 1500) / 1500.0));
  } else if (effect == "reject_flash") {
    // Two 150 ms red blinks.
    const bool on = (t < 150) || (t >= 300 && t < 450);
    fill(f, on ? kRed : Rgb{});
  } else if (effect == "cooldown_pulse") {
    const double k = 0.5 - 0.5 * std::cos(2 * kPi * (t % 800) / 800.0);
    fill(f, scale(kAmber, k));
  } else if (effect == "bonus_chase") {
    const int n = static_cast<int>(f.size());
    for (int i = 0; i < n; ++i) f[i] = wheel(static_cast<int>(i * 256 / std::max(n, 1) + t / 4));
  } else if (effect == "event_strobe") {
    fill(f, (t / 80) % 2 == 0 ? kWhite : kGold);
  } else if (effect == "fault_blink") {
    fill(f, (t / 500) % 2 == 0 ? kRed : Rgb{});
  } else {
    fill(f, Rgb{});
  }
}

void apply_brightness(Frame& f, int percent) {
  const double k = std::clamp(percent, 0, 100) / 100.0;
  for (auto& p : f) p = scale(p, k);
}

int estimate_ma(const Frame& f) {
  long sum = 0;
  for (const auto& p : f) sum += p.r + p.g + p.b;
  return static_cast<int>(f.size() + (sum * 20 + 254) / 255);
}

void limit_current(Frame& f, int max_ma) {
  const int idle = static_cast<int>(f.size());
  const int est = estimate_ma(f);
  if (est <= max_ma) return;
  const double budget = std::max(0, max_ma - idle);
  const double k = budget / static_cast<double>(est - idle);
  for (auto& p : f) {
    p = {static_cast<std::uint8_t>(p.r * k), static_cast<std::uint8_t>(p.g * k), static_cast<std::uint8_t>(p.b * k)};
  }
}

void LedAnimator::set_base(const std::string& effect, std::int64_t now) {
  if (effect == base_) return;
  base_ = effect;
  base_start_ = now;
}

void LedAnimator::play(const std::string& effect, std::uint32_t duration_ms, std::int64_t now) {
  if (duration_ms == 0) {
    overlay_.clear();
    base_ = effect;
    base_start_ = now;
    return;
  }
  overlay_ = effect;
  overlay_start_ = now;
  overlay_end_ = now + duration_ms;
}

const std::string& LedAnimator::current(std::int64_t now) const {
  return (!overlay_.empty() && now < overlay_end_) ? overlay_ : base_;
}

void LedAnimator::render_at(std::int64_t now, Frame& frame) {
  if (!overlay_.empty() && now >= overlay_end_) overlay_.clear();
  if (!overlay_.empty())
    render(overlay_, now - overlay_start_, frame);
  else
    render(base_, now - base_start_, frame);
}

bool LedAnimator::animating(std::int64_t now) const { return current(now) != "off"; }

}  // namespace orca::fx
