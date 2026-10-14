// Property tests: random input streams against every registered game, checking invariants
// that must always hold. Failures print the seed; replay with ORCA_PROP_SEED=<seed>.
// ORCA_PROP_SEEDS=<n> runs more seeds (make props-long sets it high).

#include <gtest/gtest.h>

#include <cstdlib>
#include <functional>
#include <set>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/recording_outputs.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "orca/game/engine.hpp"
#include "orca/games/fare_paid.hpp"
#include "orca/games/registry.hpp"

namespace {

using namespace orca::game;
using orca::fakes::FakeClock;
using orca::fakes::RecordingOutputs;
using orca::fakes::SeededRandom;
using orca::hal::minutes;
using orca::hal::seconds;

const GameRegistry& registry() {
  static GameRegistry reg = [] {
    GameRegistry r;
    orca::games::register_builtin_games(r);
    return r;
  }();
  return reg;
}

// One scripted input. Generated up front so the same stream can be replayed into two engines.
struct Step {
  enum Kind { Tap, BoundaryTap, Advance, Pause, Resume, Trigger, Adjust, Ban, Unban, ToggleEnrollment, EndEvents } kind;
  std::string uid;
  TimeMs ms = 0;
  int player = 0;
  int amount = 0;
  EventKind event_kind = EventKind::Multiplier;
  double multiplier = 1;
};

std::string uid_of(int i) {
  char buf[16];
  std::snprintf(buf, sizeof buf, "04%010X", i);
  return buf;
}

std::vector<Step> generate(SeededRandom& g, int count, int players) {
  std::vector<Step> steps;
  for (int i = 0; i < count; ++i) {
    Step s{};
    const auto roll = g.uniform(0, 99);
    if (roll < 12) {
      // Jump to just before/after a player's cooldown ends, then tap: probes off-by-one bugs.
      s.kind = Step::BoundaryTap;
      s.player = static_cast<int>(g.uniform(1, players));
      s.ms = g.uniform(-1500, 1500);
    } else if (roll < 55) {
      s.kind = Step::Tap;
      // Mostly known cards; sometimes a brand-new card.
      s.uid = uid_of(static_cast<int>(g.uniform(1, g.uniform(0, 9) == 0 ? players * 3 : players)));
    } else if (roll < 85) {
      s.kind = Step::Advance;
      s.ms = g.uniform(0, 3) == 0 ? g.uniform(minutes(1), minutes(20)) : g.uniform(0, seconds(90));
    } else if (roll < 88) {
      s.kind = Step::Pause;
    } else if (roll < 91) {
      s.kind = Step::Resume;
    } else if (roll < 94) {
      s.kind = Step::Trigger;
      s.event_kind = g.uniform(0, 2) == 0 ? EventKind::CooldownWaiver : EventKind::Multiplier;
      s.ms = g.uniform(seconds(5), minutes(5));
      s.multiplier = 1 + g.uniform(0, 4) * 0.5;
    } else if (roll < 96) {
      s.kind = Step::Adjust;
      s.player = static_cast<int>(g.uniform(1, players));
      s.amount = static_cast<int>(g.uniform(-5, 5));
    } else if (roll < 97) {
      s.kind = Step::Ban;
      s.player = static_cast<int>(g.uniform(1, players));
    } else if (roll < 98) {
      s.kind = Step::Unban;
      s.player = static_cast<int>(g.uniform(1, players));
    } else if (roll < 99) {
      s.kind = Step::ToggleEnrollment;
    } else {
      s.kind = Step::EndEvents;
    }
    steps.push_back(s);
  }
  return steps;
}

// Drives an engine through steps and checks per-tap invariants. Returns the tap transcript.
class Runner {
 public:
  Runner(Engine& e, FakeClock& c) : e_(e), clock_(c) {}

  std::vector<json> run(const std::vector<Step>& steps, std::size_t from, std::size_t to) {
    std::vector<json> transcript;
    for (std::size_t i = from; i < to; ++i) {
      const Step& s = steps[i];
      SCOPED_TRACE("step " + std::to_string(i));
      switch (s.kind) {
        case Step::Tap: transcript.push_back(check_tap(s.uid)); break;
        case Step::BoundaryTap: {
          auto* fp = dynamic_cast<orca::games::FarePaid*>(e_.game());
          if (fp) {
            const TimeMs left = fp->cooldown_left(s.player, e_.now());
            if (left > 0) advance(left + s.ms);
          }
          transcript.push_back(check_tap(uid_of(s.player)));
          break;
        }
        case Step::Advance: advance(s.ms); break;
        case Step::Pause: if (e_.state() == GameState::Running) e_.pause(); break;
        case Step::Resume: if (e_.state() == GameState::Paused) e_.resume(); break;
        case Step::Trigger:
          if (e_.state() == GameState::Running) e_.trigger_event(s.event_kind, "PROP", s.ms, s.multiplier);
          break;
        case Step::Adjust: if (e_.players().find(s.player)) e_.adjust_score(s.player, s.amount); break;
        case Step::Ban: if (e_.players().find(s.player)) e_.update_player(s.player, {{"banned", true}}); break;
        case Step::Unban: if (e_.players().find(s.player)) e_.update_player(s.player, {{"banned", false}}); break;
        case Step::ToggleEnrollment: e_.set_open_enrollment(!e_.open_enrollment()); break;
        case Step::EndEvents: if (e_.state() == GameState::Running) e_.end_events(); break;
      }
      EXPECT_EQ(e_.scores().total(), sum_points()) << "total drifted from per-player points";
    }
    return transcript;
  }

 private:
  void advance(TimeMs ms) {
    for (TimeMs left = ms; left > 0; left -= 500) {
      clock_.advance(std::min<TimeMs>(500, left));
      e_.tick();
    }
  }

  json check_tap(const std::string& uid) {
    const GameState state = e_.state();
    const Player* before_p = e_.players().find_uid(uid);
    const auto before = before_p ? e_.scores().points(before_p->number) : 0;
    const bool waived = e_.events().cooldown_waived(e_.now());

    TapRecord r = e_.tap(uid);

    // Closed exactly when no game is running.
    EXPECT_EQ(r.outcome == Outcome::Closed, state != GameState::Running);
    // Outcomes that refuse a fare never change points.
    if (r.outcome == Outcome::Cooldown || r.outcome == Outcome::Unknown || r.outcome == Outcome::Banned ||
        r.outcome == Outcome::Closed)
      EXPECT_EQ(r.points, 0) << to_string(r.outcome);
    if (r.outcome == Outcome::Accepted || r.outcome == Outcome::Bonus) EXPECT_GE(r.points, 0);
    if (r.outcome == Outcome::Unknown) EXPECT_EQ(r.player, 0);
    // Reported points are exactly the points applied, and balance matches the board.
    if (r.player) {
      EXPECT_EQ(e_.scores().points(r.player), before + r.points);
      EXPECT_EQ(r.balance, e_.scores().points(r.player));
    }
    // Fare Paid family: never accepted inside the player's own cooldown unless waived.
    if (auto* fp = dynamic_cast<orca::games::FarePaid*>(e_.game()); fp && r.player &&
        (r.outcome == Outcome::Accepted || r.outcome == Outcome::Bonus)) {
      const TimeMs cooldown = fp->params().at("cooldown").get<TimeMs>();
      auto it = last_accepted_.find(r.player);
      if (it != last_accepted_.end() && !waived) EXPECT_GE(r.t - it->second, cooldown);
      last_accepted_[r.player] = r.t;
    }
    json j = r;
    j.erase("wall_ms");
    return j;
  }

  std::int64_t sum_points() const {
    std::int64_t t = 0;
    for (const auto& p : e_.players().all()) t += e_.scores().points(p.number);
    return t;
  }

  Engine& e_;
  FakeClock& clock_;
  std::map<int, TimeMs> last_accepted_;
};

int seeds_to_run() {
  if (const char* s = std::getenv("ORCA_PROP_SEEDS")) return std::max(1, std::atoi(s));
  return 25;
}

struct Case {
  std::string game;
  json params;
};

void PrintTo(const Case& c, std::ostream* os) { *os << c.game << " " << c.params.dump(); }

class Properties : public ::testing::TestWithParam<Case> {};

TEST_P(Properties, InvariantsHoldAndSnapshotsReplayIdentically) {
  const Case& c = GetParam();
  std::vector<std::uint64_t> seeds;
  if (const char* one = std::getenv("ORCA_PROP_SEED")) {
    seeds.push_back(std::strtoull(one, nullptr, 10));
  } else {
    for (int i = 1; i <= seeds_to_run(); ++i) seeds.push_back(static_cast<std::uint64_t>(i));
  }

  for (std::uint64_t seed : seeds) {
    SCOPED_TRACE("seed " + std::to_string(seed) + " (replay with ORCA_PROP_SEED=" + std::to_string(seed) + ")");
    SeededRandom gen(seed);
    const int players = static_cast<int>(gen.uniform(1, 12));
    const auto steps = generate(gen, 400, players);
    const std::size_t split = static_cast<std::size_t>(gen.uniform(1, static_cast<std::int64_t>(steps.size()) - 1));

    FakeClock clock(1000);
    SeededRandom rng(seed * 7919);
    RecordingOutputs out;
    Engine a(clock, rng, out, registry());
    for (int i = 1; i <= players; ++i) a.enroll(uid_of(i));
    a.select_game(c.game, c.params);
    a.start();

    Runner ra(a, clock);
    ra.run(steps, 0, split);
    if (::testing::Test::HasFailure()) return;

    // Snapshot, then continue two engines from the same point with identical RNG streams.
    const json snap = a.snapshot();
    rng.reseed(seed);
    FakeClock clock_b(77);  // a "rebooted" clock
    SeededRandom rng_b(seed);
    RecordingOutputs out_b;
    Engine b(clock_b, rng_b, out_b, registry());
    b.restore(snap);
    ASSERT_EQ(b.snapshot(), snap);

    Runner rb(b, clock_b);
    auto ta = ra.run(steps, split, steps.size());
    auto tb = rb.run(steps, split, steps.size());
    ASSERT_EQ(ta.size(), tb.size());
    for (std::size_t i = 0; i < ta.size(); ++i) ASSERT_EQ(ta[i], tb[i]) << "tap " << i << " after restore";
    ASSERT_EQ(a.snapshot(), b.snapshot());
    if (::testing::Test::HasFailure()) return;
  }
}

INSTANTIATE_TEST_SUITE_P(AllGames, Properties,
                         ::testing::Values(Case{"fare_paid", json::object()},
                                           Case{"fare_paid", {{"cooldown", 0}, {"points", 3}}},
                                           Case{"rush_hour", {{"every", minutes(3)}, {"length", seconds(40)}}},
                                           Case{"team_lines", {{"teams", "Red,Blue,Green"}, {"cooldown", seconds(30)}}},
                                           Case{"last_stop", {{"interval", seconds(20)}, {"cooldown", seconds(30)}}},
                                           Case{"fare_inspector", {{"every", minutes(4)}, {"length", seconds(45)}, {"valid_for", minutes(2)}}},
                                           Case{"lottery", {{"cooldown", seconds(20)}}}),
                         [](const auto& info) { return info.param.game + "_" + std::to_string(info.index); });

// Every registered game must appear above.
TEST(Properties, CoverEveryRegisteredGame) {
  std::set<std::string> covered{"fare_paid", "rush_hour", "team_lines", "last_stop", "fare_inspector", "lottery"};
  for (const GameInfo* g : registry().all()) EXPECT_TRUE(covered.count(g->id)) << g->id << " has no property case";
}

}  // namespace
