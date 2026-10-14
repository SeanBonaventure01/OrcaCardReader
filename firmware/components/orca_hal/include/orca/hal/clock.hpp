#pragma once

#include <cstdint>

namespace orca::hal {

// Milliseconds on a monotonic clock that starts at boot. Never use it for wall-clock time.
using TimeMs = std::uint64_t;

constexpr TimeMs seconds(std::uint64_t s) { return s * 1000; }
constexpr TimeMs minutes(std::uint64_t m) { return m * 60 * 1000; }

class Clock {
 public:
  virtual ~Clock() = default;
  virtual TimeMs now_ms() const = 0;
};

}  // namespace orca::hal
