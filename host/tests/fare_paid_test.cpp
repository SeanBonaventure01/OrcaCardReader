// Rules of Fare Paid and Rush Hour, written as tests.
#include "game_harness.hpp"

using namespace orca::test;
using orca::game::EventKind;

class FarePaidTest : public GameHarness {
 protected:
  void SetUp() override {
    add_player("Ghost");
    add_player("Witch");
  }
};

TEST_F(FarePaidTest, FirstTapPaysOnePoint) {
  start("fare_paid");
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Accepted);
  EXPECT_EQ(r.points, 1);
  EXPECT_EQ(points("Ghost"), 1);
}

TEST_F(FarePaidTest, TapDuringCooldownIsATransfer) {
  start("fare_paid");
  tap("Ghost");
  advance(seconds(30));
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Cooldown);
  EXPECT_EQ(r.headline, "TRANSFER");
  EXPECT_EQ(r.detail, "Next fare in 4:30");
  EXPECT_EQ(points("Ghost"), 1);
}

TEST_F(FarePaidTest, CooldownBoundaryIsExact) {
  start("fare_paid");
  tap("Ghost");
  advance(minutes(5) - 1, 1000);
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Cooldown);  // 4:59.999
  advance(1);
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);  // 5:00.000
  EXPECT_EQ(points("Ghost"), 2);
}

TEST_F(FarePaidTest, CooldownTapDoesNotRestartTheTimer) {
  start("fare_paid");
  tap("Ghost");
  advance(minutes(4));
  tap("Ghost");  // rejected
  advance(minutes(1));
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);
}

TEST_F(FarePaidTest, CooldownsArePerPlayer) {
  start("fare_paid");
  tap("Ghost");
  EXPECT_EQ(tap("Witch").outcome, Outcome::Accepted);
}

TEST_F(FarePaidTest, ConfigurableCooldownAndPoints) {
  start("fare_paid", {{"cooldown", seconds(10)}, {"points", 5}});
  tap("Ghost");
  advance(seconds(10));
  tap("Ghost");
  EXPECT_EQ(points("Ghost"), 10);
}

TEST_F(FarePaidTest, ZeroCooldownAcceptsEveryTap) {
  start("fare_paid", {{"cooldown", 0}});
  for (int i = 0; i < 5; ++i) EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);
}

TEST_F(FarePaidTest, CooldownWaiverLetsYouTapAgain) {
  start("fare_paid");
  tap("Ghost");
  engine.trigger_event(EventKind::CooldownWaiver, "FREE RIDE", minutes(1), 1);
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);
  advance(minutes(1));
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Cooldown);
}

TEST_F(FarePaidTest, RejectsTooManyPoints) {
  EXPECT_THROW(engine.select_game("fare_paid", {{"points", 101}}), orca::game::EngineError);
}

// ---------------------------------------------------------------- Rush Hour

class RushHourTest : public FarePaidTest {};

TEST_F(RushHourTest, RushHoursHappenRandomlyAndDoublePoints) {
  start("rush_hour", {{"every", minutes(10)}, {"length", minutes(2)}, {"cooldown", 0}});
  const auto next = engine.events().next_random_at();
  ASSERT_TRUE(next);
  EXPECT_GE(*next - engine.now(), minutes(5));
  EXPECT_LE(*next - engine.now(), minutes(15));

  advance(*next - engine.now());
  ASSERT_EQ(engine.events().active(engine.now()).size(), 1u);
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Bonus);
  EXPECT_EQ(r.points, 2);

  advance(minutes(2));
  EXPECT_TRUE(engine.events().active(engine.now()).empty());
  EXPECT_EQ(tap("Ghost").points, 1);
}

TEST_F(RushHourTest, CustomMultiplier) {
  start("rush_hour", {{"multiplier", 3.0}, {"cooldown", 0}});
  advance(*engine.events().next_random_at() - engine.now());
  EXPECT_EQ(tap("Ghost").points, 3);
}

TEST_F(RushHourTest, NoRushHoursBeforeStartOrAfterStop) {
  engine.select_game("rush_hour", {{"every", minutes(1)}});
  advance(minutes(10));
  EXPECT_EQ(count_messages("event"), 0);
  engine.start();
  engine.stop();
  advance(minutes(10));
  EXPECT_EQ(count_messages("event"), 0);
}

TEST_F(RushHourTest, PausingHoldsRushHourUntilResume) {
  start("rush_hour", {{"every", minutes(1)}, {"length", seconds(10)}});
  engine.pause();
  advance(minutes(5));
  EXPECT_EQ(count_messages("event"), 0);
  engine.resume();
  advance(100);
  EXPECT_EQ(count_messages("event"), 1);  // the overdue rush hour starts on resume
}
