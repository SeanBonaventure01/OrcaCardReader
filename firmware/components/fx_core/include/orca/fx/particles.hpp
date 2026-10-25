#pragma once

// Confetti and fireworks for paid fares: a small particle simulation with no drawing, so the
// display only has to put a rectangle at each particle (and it's host-tested).

#include <cstdint>
#include <vector>

namespace orca::fx {

struct Particle {
  float x, y;     // centre, px
  float vx, vy;   // px/s (y grows downward)
  float w, h;     // px (confetti flutters by narrowing w)
  std::uint32_t color;  // 0xRRGGBB
  float age = 0, life;  // s
  float delay = 0;      // s before it appears (staggered firework bursts)
  bool confetti;        // flutters and falls slowly; sparks fade out instead
  float phase = 0;      // flutter
};

class Particles {
 public:
  static constexpr std::size_t kMax = 96;  // the display keeps one object per slot

  Particles(int width, int height, std::uint32_t seed) : w_(width), h_(height), rng_(seed ? seed : 1) {}

  // Confetti raining from the top of the screen.
  void confetti(std::size_t count = 60);
  // A few bursts in the upper part of the screen, staggered over ~0.7 s.
  void fireworks(int bursts = 3, std::size_t sparks_per_burst = 24);

  void step(float dt);  // advance; drops particles that are done or off-screen
  void clear() { p_.clear(); }

  const std::vector<Particle>& particles() const { return p_; }
  bool active() const { return !p_.empty(); }
  // Visible now (not still waiting on its delay), and how opaque (0-255).
  static bool visible(const Particle& p) { return p.age >= p.delay; }
  static std::uint8_t opacity(const Particle& p);
  // Current drawn width (confetti turning over).
  static float drawn_width(const Particle& p);

 private:
  float uniform(float lo, float hi);
  void add(const Particle& p);

  int w_, h_;
  std::uint32_t rng_;
  std::vector<Particle> p_;
};

}  // namespace orca::fx
