#include <gtest/gtest.h>

#include "orca/fakes/seeded_random.hpp"
#include "orca/game/event_scheduler.hpp"

using namespace orca::game;
using orca::fakes::SeededRandom;
using orca::hal::minutes;
using orca::hal::seconds;

namespace {

RandomSchedule rush(TimeMs every = minutes(10), TimeMs length = minutes(2)) {
  RandomSchedule s;
  s.enabled = true;
  s.mean_interval = every;
  s.duration = length;
  s.multiplier = 2;
  return s;
}

// Runs the scheduler at 1 s resolution and returns every window it started.
std::vector<EventWindow> run(EventScheduler& ev, SeededRandom& rng, TimeMs from, TimeMs to) {
  std::vector<EventWindow> started;
  for (TimeMs t = from; t <= to; t += 1000) {
    auto ch = ev.tick(t, rng);
    started.insert(started.end(), ch.started.begin(), ch.started.end());
  }
  return started;
}

}  // namespace

TEST(EventScheduler, ManualTriggerIsActiveForItsDuration) {
  EventScheduler ev;
  SeededRandom rng;
  auto w = ev.trigger(EventKind::Multiplier, "RUSH", seconds(30), 3, 1000);
  EXPECT_DOUBLE_EQ(ev.multiplier(1000), 3);
  EXPECT_DOUBLE_EQ(ev.multiplier(30'999), 3);
  EXPECT_DOUBLE_EQ(ev.multiplier(31'000), 1);  // end is exclusive
  auto ch = ev.tick(31'000, rng);
  ASSERT_EQ(ch.ended.size(), 1u);
  EXPECT_EQ(ch.ended[0].id, w.id);
  EXPECT_TRUE(ev.active(31'000).empty());
}

TEST(EventScheduler, MultipliersStack) {
  EventScheduler ev;
  ev.trigger(EventKind::Multiplier, "A", minutes(1), 2, 0);
  ev.trigger(EventKind::Multiplier, "B", minutes(1), 1.5, 0);
  EXPECT_DOUBLE_EQ(ev.multiplier(10), 3);
}

TEST(EventScheduler, CooldownWaiverDoesNotMultiply) {
  EventScheduler ev;
  ev.trigger(EventKind::CooldownWaiver, "FREE", minutes(1), 5, 0);
  EXPECT_TRUE(ev.cooldown_waived(10));
  EXPECT_DOUBLE_EQ(ev.multiplier(10), 1);
  EXPECT_FALSE(ev.cooldown_waived(minutes(1)));
}

TEST(EventScheduler, RandomScheduleIsReproducibleForASeed) {
  auto starts = [](std::uint64_t seed) {
    EventScheduler ev;
    SeededRandom rng(seed);
    ev.configure_random(rush(), 0, rng);
    std::vector<TimeMs> out;
    for (const auto& w : run(ev, rng, 0, minutes(120))) out.push_back(w.start);
    return out;
  };
  EXPECT_EQ(starts(7), starts(7));
  EXPECT_NE(starts(7), starts(8));
}

TEST(EventScheduler, RandomWindowsRespectIntervalBoundsAndNeverOverlap) {
  EventScheduler ev;
  SeededRandom rng(3);
  ev.configure_random(rush(minutes(10), minutes(2)), 0, rng);
  auto ws = run(ev, rng, 0, minutes(8 * 60));
  ASSERT_GT(ws.size(), 20u);
  TimeMs prev_end = 0;
  for (const auto& w : ws) {
    EXPECT_TRUE(w.random);
    EXPECT_EQ(w.end - w.start, minutes(2));
    const TimeMs gap = w.start - prev_end;
    // jitter 0.5 -> 5..15 min after the previous window ended (1 s tick resolution)
    EXPECT_GE(gap, minutes(5)) << "window " << w.id;
    EXPECT_LE(gap, minutes(15) + 1000) << "window " << w.id;
    prev_end = w.end;
  }
}

TEST(EventScheduler, DisableStopsRandomWindows) {
  EventScheduler ev;
  SeededRandom rng(1);
  ev.configure_random(rush(minutes(1), seconds(10)), 0, rng);
  ev.disable_random();
  EXPECT_TRUE(run(ev, rng, 0, minutes(30)).empty());
  EXPECT_FALSE(ev.next_random_at());
}

TEST(EventScheduler, EndAllReportsOnNextTick) {
  EventScheduler ev;
  SeededRandom rng;
  ev.trigger(EventKind::Multiplier, "A", minutes(5), 2, 0);
  ev.end_all(1000);
  EXPECT_DOUBLE_EQ(ev.multiplier(1000), 1);
  EXPECT_EQ(ev.tick(1000, rng).ended.size(), 1u);
}

TEST(EventScheduler, SaveRestoreMidWindow) {
  EventScheduler ev;
  SeededRandom rng(9);
  ev.configure_random(rush(minutes(10), minutes(2)), 0, rng);
  auto ws = run(ev, rng, 0, minutes(30));
  ASSERT_FALSE(ws.empty());
  const TimeMs mid = ws.back().start + seconds(30);
  run(ev, rng, minutes(30), mid);

  EventScheduler copy;
  copy.restore(ev.save());
  EXPECT_EQ(copy.active(mid).size(), ev.active(mid).size());
  EXPECT_DOUBLE_EQ(copy.multiplier(mid), ev.multiplier(mid));
  EXPECT_EQ(copy.next_random_at(), ev.next_random_at());
  EXPECT_EQ(copy.random_schedule().mean_interval, minutes(10));

  // Both continue identically from here with identical RNG streams.
  SeededRandom r1(100), r2(100);
  auto a = run(ev, r1, mid, minutes(180));
  auto b = run(copy, r2, mid, minutes(180));
  ASSERT_EQ(a.size(), b.size());
  for (std::size_t i = 0; i < a.size(); ++i) EXPECT_EQ(a[i].start, b[i].start);
}

TEST(EventScheduler, KindStrings) {
  for (auto k : {EventKind::Multiplier, EventKind::CooldownWaiver, EventKind::Custom})
    EXPECT_EQ(event_kind_from_string(to_string(k)), k);
  EXPECT_FALSE(event_kind_from_string("nope"));
}
