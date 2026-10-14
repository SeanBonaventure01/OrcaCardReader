#pragma once

#include <cstdint>

namespace orca::hal {

// Milliseconds on a monotonic clock. Signed so that differences never wrap.
using TimeMs = std::int64_t;

constexpr TimeMs seconds(std::int64_t s) { return s * 1000; }
constexpr TimeMs minutes(std::int64_t m) { return m * 60 * 1000; }

class Clock {
 public:
  virtual ~Clock() = default;
  virtual TimeMs now_ms() const = 0;
};

}  // namespace orca::hal
