#pragma once

#include "orca/hal/clock.hpp"

namespace orca::fakes {

// Clock under test control. Time only moves when advance() or set() is called.
class FakeClock : public hal::Clock {
 public:
  explicit FakeClock(hal::TimeMs start = 0) : now_(start) {}

  hal::TimeMs now_ms() const override { return now_; }
  void advance(hal::TimeMs delta) { now_ += delta; }
  void set(hal::TimeMs t) { now_ = t; }

 private:
  hal::TimeMs now_;
};

}  // namespace orca::fakes
