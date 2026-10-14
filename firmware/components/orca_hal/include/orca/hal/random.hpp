#pragma once

#include <cstdint>

namespace orca::hal {

class Random {
 public:
  virtual ~Random() = default;
  virtual std::uint32_t next_u32() = 0;

  // Uniform integer in [lo, hi]. Returns lo if hi <= lo.
  std::int64_t uniform(std::int64_t lo, std::int64_t hi) {
    if (hi <= lo) return lo;
    const auto span = static_cast<std::uint64_t>(hi - lo) + 1;
    const std::uint64_t r = (static_cast<std::uint64_t>(next_u32()) << 32) | next_u32();
    return lo + static_cast<std::int64_t>(r % span);
  }

  // Uniform double in [0, 1).
  double unit() { return next_u32() / 4294967296.0; }
};

}  // namespace orca::hal
