#pragma once

#include <cstdint>

#include "orca/hal/random.hpp"

namespace orca::fakes {

// Deterministic SplitMix64 generator: the same seed always gives the same run.
class SeededRandom : public hal::Random {
 public:
  explicit SeededRandom(std::uint64_t seed = 1) : state_(seed) {}

  std::uint32_t next_u32() override {
    std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
  }

 private:
  std::uint64_t state_;
};

}  // namespace orca::fakes
