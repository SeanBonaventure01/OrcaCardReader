#include "game_harness.hpp"

using namespace orca::test;
using orca::game::EngineError;
using orca::game::EventKind;
using orca::game::GameState;

namespace {

int status_of(const std::function<void()>& fn) {
  try {
    fn();
  } catch (const EngineError& e) {
    return e.status();
  }
  return 0;
}

}  // namespace

class EngineTest : public GameHarness {};

TEST_F(EngineTest, StartsWithNoGameAndRejectsTapsAsClosed) {
  EXPECT_EQ(engine.state(), GameState::None);
  add_player("Ghost");
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Closed);
  EXPECT_EQ(r.headline, "Not in service");
  EXPECT_EQ(points("Ghost"), 0);
  EXPECT_EQ(engine.scores().stats(1).taps, 0);  // closed taps aren't counted
}

TEST_F(EngineTest, LifecycleTransitions) {
  engine.select_game("fare_paid", json::object());
  EXPECT_EQ(engine.state(), GameState::Ready);
  engine.start();
  EXPECT_EQ(engine.state(), GameState::Running);
  engine.pause();
  EXPECT_EQ(engine.state(), GameState::Paused);
  engine.resume();
  engine.stop();
  EXPECT_EQ(engine.state(), GameState::Ended);
  engine.reset();
  EXPECT_EQ(engine.state(), GameState::Ready);
}

TEST_F(EngineTest, InvalidTransitionsAreConflicts) {
  EXPECT_EQ(status_of([&] { engine.start(); }), 409);
  EXPECT_EQ(status_of([&] { engine.reset(); }), 409);
  engine.select_game("fare_paid", json::object());
  EXPECT_EQ(status_of([&] { engine.pause(); }), 409);
  EXPECT_EQ(status_of([&] { engine.resume(); }), 409);
  EXPECT_EQ(status_of([&] { engine.stop(); }), 409);
  engine.start();
  EXPECT_EQ(status_of([&] { engine.start(); }), 409);
  EXPECT_EQ(status_of([&] { engine.select_game("fare_paid", json::object()); }), 409);
  EXPECT_EQ(status_of([&] { engine.reset(); }), 409);
}

TEST_F(EngineTest, SelectValidatesGameAndParams) {
  EXPECT_EQ(status_of([&] { engine.select_game("nope", json::object()); }), 404);
  EXPECT_EQ(status_of([&] { engine.select_game("fare_paid", {{"points", 0}}); }), 400);
  EXPECT_EQ(engine.state(), GameState::None);
}

TEST_F(EngineTest, PausedGameRejectsTaps) {
  add_player("Ghost");
  start("fare_paid");
  engine.pause();
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Closed);
  engine.resume();
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);
}

TEST_F(EngineTest, OpenEnrollmentAutoEnrollsUnknownCards) {
  start("fare_paid");
  auto r = engine.tap("04AABBCCDDEEFF");
  EXPECT_EQ(r.outcome, Outcome::Accepted);
  EXPECT_EQ(r.player, 1);
  EXPECT_EQ(r.name, "Rider 1");
  EXPECT_EQ(engine.players().size(), 1u);
  EXPECT_EQ(count_messages("players"), 1);
}

TEST_F(EngineTest, ClosedEnrollmentRejectsUnknownCards) {
  engine.set_open_enrollment(false);
  start("fare_paid");
  auto r = engine.tap("04AABBCCDDEEFF");
  EXPECT_EQ(r.outcome, Outcome::Unknown);
  EXPECT_EQ(r.headline, "Card not recognized");
  EXPECT_EQ(engine.players().size(), 0u);
}

TEST_F(EngineTest, MalformedUidIsBadRequest) {
  start("fare_paid");
  EXPECT_EQ(status_of([&] { engine.tap("xyz"); }), 400);
  EXPECT_EQ(status_of([&] { engine.enroll("12"); }), 400);
}

TEST_F(EngineTest, UidCaseAndSeparatorsDoNotMatter) {
  start("fare_paid");
  engine.tap("04:aa:bb:cc");
  EXPECT_EQ(engine.tap("04AABBCC").outcome, Outcome::Cooldown);
  EXPECT_EQ(engine.players().size(), 1u);
}

TEST_F(EngineTest, BannedPlayerIsRejectedWithoutPoints) {
  auto& p = add_player("Ghost");
  start("fare_paid");
  engine.update_player(p.number, {{"banned", true}});
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Banned);
  EXPECT_EQ(r.headline, "See customer service");
  EXPECT_EQ(points("Ghost"), 0);
}

TEST_F(EngineTest, UpdatePlayerValidatesEveryField) {
  auto& p = add_player("Ghost");
  const int no = p.number;
  EXPECT_EQ(status_of([&] { engine.update_player(99, {{"name", "x"}}); }), 404);
  EXPECT_EQ(status_of([&] { engine.update_player(no, {{"name", ""}}); }), 400);
  EXPECT_EQ(status_of([&] { engine.update_player(no, {{"name", std::string(25, 'x')}}); }), 400);
  EXPECT_EQ(status_of([&] { engine.update_player(no, {{"banned", "yes"}}); }), 400);
  EXPECT_EQ(status_of([&] { engine.update_player(no, {{"color", "red"}}); }), 400);
  // A bad field anywhere leaves the player unchanged.
  EXPECT_EQ(status_of([&] { engine.update_player(no, {{"name", "Boo"}, {"banned", 1}}); }), 400);
  EXPECT_EQ(engine.players().find(no)->name, "Ghost");
  engine.update_player(no, {{"name", "Boo"}, {"team", "Sounder"}});
  EXPECT_EQ(engine.players().find(no)->name, "Boo");
  EXPECT_EQ(engine.players().find(no)->team, "Sounder");
}

TEST_F(EngineTest, AdjustScore) {
  auto& p = add_player("Ghost");
  engine.adjust_score(p.number, 5);
  engine.adjust_score(p.number, -2);
  EXPECT_EQ(points("Ghost"), 3);
  EXPECT_EQ(status_of([&] { engine.adjust_score(42, 1); }), 404);
  EXPECT_EQ(status_of([&] { engine.adjust_score(p.number, 2'000'000); }), 400);
}

TEST_F(EngineTest, TapOutputsBeepFirstThenLedsThenScreen) {
  add_player("Ghost");
  start("fare_paid");
  out.clear();
  tap("Ghost");
  ASSERT_GE(out.log.size(), 3u);
  EXPECT_EQ(out.log[0]["out"], "sound");
  EXPECT_EQ(out.log[0]["id"], "accept");
  EXPECT_EQ(out.log[1]["out"], "leds");
  EXPECT_EQ(out.log[2]["out"], "screen");
  EXPECT_EQ(out.log[2]["kind"], "result");
  EXPECT_EQ(out.log[2]["tone"], "success");
  EXPECT_EQ(out.log[2]["player"], "#01 Ghost");
}

TEST_F(EngineTest, TemplatesAreFilled) {
  add_player("Ghost");
  start("fare_paid", {{"points", 3}});
  auto r = tap("Ghost");
  EXPECT_EQ(r.headline, "Good for 5:00");  // clock not synced yet: relative time
  EXPECT_EQ(r.detail, "+3 pts · Balance 3");
}

TEST_F(EngineTest, MultiplierUpgradesToBonus) {
  add_player("Ghost");
  start("fare_paid", {{"points", 3}});
  engine.trigger_event(EventKind::Multiplier, "Rush hour", minutes(1), 2);
  auto r = tap("Ghost");
  EXPECT_EQ(r.outcome, Outcome::Bonus);
  EXPECT_EQ(r.points, 6);
  EXPECT_EQ(r.headline, "Good for 5:00");
  EXPECT_EQ(r.detail, "+6 pts (2x bonus) · Balance 6");
  EXPECT_EQ(out.last_screen.tone, orca::hal::Tone::Bonus);
}

TEST_F(EngineTest, FractionalMultiplierRounds) {
  add_player("Ghost");
  start("fare_paid", {{"points", 3}});
  engine.trigger_event(EventKind::Multiplier, "HALF", minutes(1), 1.5);
  EXPECT_EQ(tap("Ghost").points, 5);  // 4.5 rounds away from zero
}

TEST_F(EngineTest, TriggerEventAnnouncesAndEnds) {
  start("fare_paid");
  out.clear();
  engine.trigger_event(EventKind::Multiplier, "Rush hour", seconds(30), 2);
  EXPECT_EQ(out.sounds.front(), "event_start");
  bool banner = false;
  for (const auto& e : out.log) banner |= e.value("out", "") == "screen" && e.value("kind", "") == "banner" && e.value("headline", "") == "Rush hour";
  EXPECT_TRUE(banner);
  EXPECT_EQ(count_messages("event"), 1);
  advance(seconds(30));
  EXPECT_EQ(count_messages("event"), 2);
  EXPECT_EQ(out.sounds.back(), "event_end");
  EXPECT_EQ(out.last_leds, "idle_breathe");
}

TEST_F(EngineTest, RushModeOnTheScreenWhileAMultiplierIsOn) {
  start("fare_paid");
  EXPECT_FALSE(out.last_screen.rush);
  engine.trigger_event(EventKind::CooldownWaiver, "Free ride", seconds(30), 1.0);
  EXPECT_FALSE(out.last_screen.rush);  // not a multiplier: no party
  engine.end_events();
  engine.trigger_event(EventKind::Multiplier, "Rush hour", seconds(30), 2.0);
  EXPECT_TRUE(out.last_screen.rush);
  EXPECT_EQ(out.last_screen.note, "Rush hour 2x");
  advance(seconds(31));
  EXPECT_FALSE(out.last_screen.rush);  // back to normal when it ends
}

TEST_F(EngineTest, TriggerEventValidation) {
  EXPECT_EQ(status_of([&] { engine.trigger_event(EventKind::Multiplier, "X", 1000, 2); }), 409);
  start("fare_paid");
  EXPECT_EQ(status_of([&] { engine.trigger_event(EventKind::Multiplier, "X", 0, 2); }), 400);
  EXPECT_EQ(status_of([&] { engine.trigger_event(EventKind::Multiplier, "X", minutes(61), 2); }), 400);
  EXPECT_EQ(status_of([&] { engine.trigger_event(EventKind::Multiplier, "X", 1000, 11); }), 400);
}

TEST_F(EngineTest, StopAnnouncesWinner) {
  add_player("Ghost");
  add_player("Witch");
  start("fare_paid", {{"cooldown", 0}});
  tap("Ghost");
  tap("Witch");
  tap("Witch");
  engine.stop();
  EXPECT_EQ(out.last_screen.kind, orca::hal::ScreenKind::Idle);
  bool winner = false;
  for (const auto& e : out.log) winner |= e.value("kind", "") == "banner" && e.value("detail", "") == "Winner: Witch (2 pts)";
  EXPECT_TRUE(winner);
}

TEST_F(EngineTest, SelectGameClearsScoresUnlessKept) {
  add_player("Ghost");
  start("fare_paid");
  tap("Ghost");
  engine.stop();
  engine.select_game("fare_paid", json::object(), /*keep_scores=*/true);
  EXPECT_EQ(points("Ghost"), 1);
  engine.select_game("fare_paid", json::object());
  EXPECT_EQ(points("Ghost"), 0);
  EXPECT_EQ(engine.players().size(), 1u);  // players are never cleared
}

TEST_F(EngineTest, ResetClearsScoresAndCooldowns) {
  add_player("Ghost");
  start("fare_paid");
  tap("Ghost");
  engine.stop();
  engine.reset();
  engine.start();
  EXPECT_EQ(points("Ghost"), 0);
  EXPECT_EQ(tap("Ghost").outcome, Outcome::Accepted);  // cooldown forgotten
}

TEST_F(EngineTest, BroadcastsTapAndScoreMessages) {
  add_player("Ghost");
  start("fare_paid");
  messages.clear();
  tap("Ghost");
  EXPECT_EQ(count_messages("tap"), 1);
  EXPECT_EQ(count_messages("scores"), 1);
  messages.clear();
  tap("Ghost");  // cooldown: stats change, points don't
  EXPECT_EQ(count_messages("tap"), 1);
}

TEST_F(EngineTest, RecentTapsNewestFirstAndCapped) {
  start("fare_paid", {{"cooldown", 0}});
  for (int i = 0; i < 120; ++i) engine.tap("04000001");
  auto recent = engine.recent_taps_json(1000);
  EXPECT_EQ(recent.size(), 100u);
  EXPECT_EQ(recent[0]["seq"], 120);
  EXPECT_EQ(engine.recent_taps_json(5).size(), 5u);
}

TEST_F(EngineTest, ChangeCountMovesOnStateChanges) {
  auto c = engine.change_count();
  add_player("Ghost");
  EXPECT_GT(engine.change_count(), c);
  c = engine.change_count();
  engine.status_json();  // queries don't count
  engine.players_json();
  EXPECT_EQ(engine.change_count(), c);
}

TEST_F(EngineTest, GoodUntilUsesLocalWallClockLikeTheRealReader) {
  add_player("Ghost");
  start("fare_paid");  // 5 min cooldown
  // 2026-10-31 20:02:00 UTC; PDT is UTC-7 -> 1:02 pm local.
  engine.sync_wall_clock(1'793'476'920'000, -420);
  EXPECT_EQ(tap("Ghost").headline, "Good until 1:07 pm");
  EXPECT_EQ(engine.status_json()["utc_offset_min"], -420);
}

TEST_F(EngineTest, GoodUntilFormatsMidnightAndNoon) {
  add_player("A");
  add_player("B");
  start("fare_paid", {{"cooldown", minutes(1)}});
  engine.sync_wall_clock(1'793'491'140'000, 0);  // 23:59:00 UTC -> fare good until 12:00 am
  EXPECT_EQ(tap("A").headline, "Good until 12:00 am");
  engine.sync_wall_clock(1'793'447'940'000, 0);  // 11:59:00 UTC -> 12:00 pm
  EXPECT_EQ(tap("B").headline, "Good until 12:00 pm");
  EXPECT_THROW(engine.sync_wall_clock(1'793'447'940'000, 15 * 60), EngineError);
}

TEST_F(EngineTest, NoCooldownMeansPlainFarePaid) {
  add_player("Ghost");
  start("fare_paid", {{"cooldown", 0}, {"points", 1}});
  auto r = tap("Ghost");
  EXPECT_EQ(r.headline, "Fare paid");
  EXPECT_EQ(r.detail, "+1 pt · Balance 1");
}

TEST_F(EngineTest, IdleDetailOnlyWhenNotTakingFares) {
  engine.select_game("fare_paid", json::object());
  EXPECT_EQ(out.last_screen.detail, "Not in service");
  engine.start();
  EXPECT_EQ(out.last_screen.detail, "");
  engine.pause();
  EXPECT_EQ(out.last_screen.detail, "Paused");
}

TEST_F(EngineTest, WallClockSync) {
  EXPECT_FALSE(engine.wall_clock_ms());
  EXPECT_EQ(status_of([&] { engine.sync_wall_clock(12345); }), 400);
  engine.sync_wall_clock(1'761'900'000'000);
  clock.advance(5000);
  EXPECT_EQ(*engine.wall_clock_ms(), 1'761'900'005'000);
  start("fare_paid");
  EXPECT_EQ(engine.tap("04000001").wall_ms, 1'761'900'005'000);
}

TEST_F(EngineTest, StatusJsonShape) {
  start("rush_hour");
  auto s = engine.status_json();
  EXPECT_EQ(s["state"], "running");
  EXPECT_EQ(s["game"]["id"], "rush_hour");
  EXPECT_EQ(s["game"]["params"]["points"], 1);
  EXPECT_TRUE(s["next_event_in_ms"].is_number());
  EXPECT_TRUE(s["events"].is_array());
}

TEST_F(EngineTest, GamesJsonListsSchemas) {
  auto g = engine.games_json();
  ASSERT_GE(g.size(), 2u);
  EXPECT_EQ(g[0]["id"], "fare_paid");
  EXPECT_EQ(g[0]["params"][0]["key"], "cooldown");
}

// ---------------------------------------------------------------- snapshots

TEST_F(EngineTest, SnapshotRestoreRoundTrip) {
  add_player("Ghost", "Link");
  add_player("Witch");
  start("fare_paid");
  tap("Ghost");
  advance(minutes(2));
  const json snap = engine.snapshot();

  // A fresh engine on a clock that restarted near zero (reboot).
  orca::fakes::FakeClock clock2(500);
  orca::fakes::RecordingOutputs out2;
  orca::game::Engine e2(clock2, rng, out2, registry_with_games());
  e2.restore(snap);
  EXPECT_EQ(e2.state(), GameState::Running);
  EXPECT_EQ(e2.now(), engine.now());  // game time continues
  EXPECT_EQ(e2.players().size(), 2u);
  EXPECT_EQ(e2.players().find(1)->team, "Link");
  EXPECT_EQ(e2.scores().points(1), 1);
  EXPECT_EQ(e2.snapshot(), snap);

  // Cooldown remembered: 3 more minutes needed.
  auto r = e2.tap(uid_for("Ghost"));
  EXPECT_EQ(r.outcome, Outcome::Cooldown);
  EXPECT_EQ(r.detail, "Next fare in 3:00");
  bool restored_banner = false;
  for (const auto& e : out2.log) restored_banner |= e.value("headline", "") == "Service restored";
  EXPECT_TRUE(restored_banner);
}

TEST_F(EngineTest, BadSnapshotLeavesEngineUntouched) {
  add_player("Ghost");
  start("fare_paid");
  tap("Ghost");
  const json before = engine.snapshot();

  json bad = before;
  bad["game"]["id"] = "not_a_game";
  EXPECT_EQ(status_of([&] { engine.restore(bad); }), 400);
  bad = before;
  bad.erase("players");
  EXPECT_EQ(status_of([&] { engine.restore(bad); }), 400);
  bad = before;
  bad["version"] = 99;
  EXPECT_EQ(status_of([&] { engine.restore(bad); }), 400);
  bad = before;
  bad["game"]["params"]["points"] = -5;
  EXPECT_EQ(status_of([&] { engine.restore(bad); }), 400);
  EXPECT_EQ(status_of([&] { engine.restore(json::array()); }), 400);

  EXPECT_EQ(engine.snapshot(), before);
}
