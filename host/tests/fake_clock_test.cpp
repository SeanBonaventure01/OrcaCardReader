#include "orca/fakes/fake_clock.hpp"

#include <gtest/gtest.h>

using orca::fakes::FakeClock;
using orca::hal::minutes;
using orca::hal::seconds;

TEST(FakeClock, StartsAtGivenTime) {
  FakeClock clock(1234);
  EXPECT_EQ(clock.now_ms(), 1234u);
}

TEST(FakeClock, OnlyMovesWhenAdvanced) {
  FakeClock clock;
  EXPECT_EQ(clock.now_ms(), 0u);
  clock.advance(minutes(5));
  EXPECT_EQ(clock.now_ms(), 300000u);
  clock.advance(seconds(1));
  EXPECT_EQ(clock.now_ms(), 301000u);
}

TEST(FakeClock, UsableThroughInterface) {
  FakeClock fake;
  const orca::hal::Clock& clock = fake;
  fake.set(42);
  EXPECT_EQ(clock.now_ms(), 42u);
}
