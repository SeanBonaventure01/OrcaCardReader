#include "orca/fx/particles.hpp"

#include <algorithm>
#include <cmath>

namespace orca::fx {

namespace {

constexpr float kPi = 3.14159265f;
// Halloween-ish party colours plus ORCA teal.
constexpr std::uint32_t kColors[] = {0xFF7A00, 0x9B30FF, 0x39FF14, 0x00C2C7, 0xFFD400, 0xFF2D6F, 0xFFFFFF};

}  // namespace

float Particles::uniform(float lo, float hi) {
  rng_ ^= rng_ << 13;  // xorshift32
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return lo + (hi - lo) * static_cast<float>(rng_ % 10000) / 10000.0f;
}

void Particles::add(const Particle& p) {
  if (p_.size() < kMax) p_.push_back(p);
}

void Particles::confetti(std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    Particle p{};
    p.x = uniform(0, static_cast<float>(w_));
    p.y = uniform(-0.4f * h_, -10);  // start above the screen, so it arrives in a wave
    p.vx = uniform(-60, 60);
    p.vy = uniform(60, 160);
    p.w = uniform(8, 14);
    p.h = uniform(12, 20);
    p.color = kColors[static_cast<std::size_t>(uniform(0, 7)) % 7];
    p.life = uniform(2.0f, 2.6f);
    p.confetti = true;
    p.phase = uniform(0, 2 * kPi);
    add(p);
  }
}

void Particles::fireworks(int bursts, std::size_t sparks) {
  for (int b = 0; b < bursts; ++b) {
    const float cx = uniform(0.2f * w_, 0.8f * w_), cy = uniform(0.15f * h_, 0.45f * h_);
    const std::uint32_t color = kColors[static_cast<std::size_t>(uniform(0, 6)) % 6];
    const float delay = 0.35f * static_cast<float>(b);
    for (std::size_t i = 0; i < sparks; ++i) {
      const float a = 2 * kPi * static_cast<float>(i) / static_cast<float>(sparks) + uniform(-0.1f, 0.1f);
      const float speed = uniform(160, 300);
      Particle p{};
      p.x = cx;
      p.y = cy;
      p.vx = std::cos(a) * speed;
      p.vy = std::sin(a) * speed;
      p.w = p.h = uniform(5, 8);
      p.color = color;
      p.life = uniform(1.0f, 1.4f);
      p.delay = delay;
      p.confetti = false;
      add(p);
    }
  }
}

void Particles::step(float dt) {
  for (auto& p : p_) {
    p.age += dt;
    if (p.age < p.delay) continue;
    if (p.confetti) {
      // Paper: light gravity, strong air drag, swaying sideways while it turns.
      p.vy += 220 * dt;
      p.vy = std::min(p.vy, 260.0f);
      p.vx *= std::pow(0.4f, dt);
      p.phase += 9 * dt;
      p.x += (p.vx + 35 * std::sin(p.phase * 0.5f)) * dt;
    } else {
      // Sparks: fast out, drag, then gravity pulls them into an arc.
      p.vx *= std::pow(0.15f, dt);
      p.vy = p.vy * std::pow(0.15f, dt) + 160 * dt;
      p.x += p.vx * dt;
    }
    p.y += p.vy * dt;
  }
  p_.erase(std::remove_if(p_.begin(), p_.end(),
                          [&](const Particle& p) {
                            return p.age - p.delay >= p.life || p.y > h_ + 30 || p.x < -40 || p.x > w_ + 40;
                          }),
           p_.end());
}

std::uint8_t Particles::opacity(const Particle& p) {
  const float t = (p.age - p.delay) / p.life;  // 0..1 through its life
  const float fade_from = p.confetti ? 0.75f : 0.35f;
  if (t <= fade_from) return 255;
  return static_cast<std::uint8_t>(std::clamp(255.0f * (1 - (t - fade_from) / (1 - fade_from)), 0.0f, 255.0f));
}

float Particles::drawn_width(const Particle& p) {
  return p.confetti ? std::max(2.0f, p.w * std::fabs(std::cos(p.phase))) : p.w;
}

}  // namespace orca::fx
