// Rules of Team Lines, Last Stop, Fare Inspector and Lottery, written as tests.
#include "game_harness.hpp"
#include "orca/games/more_games.hpp"

using namespace orca::test;
using orca::game::EngineError;
using orca::game::EventKind;

// ---------------------------------------------------------------- Team Lines

class TeamLinesTest : public GameHarness {};

TEST_F(TeamLinesTest, NewRidersJoinTheSmallestTeam) {
  add_player("A");
  add_player("B");
  add_player("C");
  add_player("D", "Sounder");  // already on a team
  start("team_lines", {{"teams", "1 Line, Sounder, RapidRide"}, {"cooldown", 0}});
  tap("A");
  tap("B");
  tap("C");
  auto team = [&](const char* n) { return engine.players().find_uid(uid_for(n))->team; };
  EXPECT_EQ(team("A"), "1 Line");      // all empty: first listed
  EXPECT_EQ(team("B"), "RapidRide");   // 1 Line=1, Sounder=1 (D), RapidRide=0
  EXPECT_EQ(team("C"), "1 Line");      // tie 1/1/1 -> first listed
  EXPECT_EQ(team("D"), "Sounder");
  EXPECT_GE(count_messages("players"), 3);
}

TEST_F(TeamLinesTest, ScreenShowsTeamTotal) {
  add_player("A");
  add_player("B");
  start("team_lines", {{"teams", "Red,Blue"}, {"cooldown", 0}, {"auto_assign", false}});
  engine.update_player(1, {{"team", "Red"}});
  engine.update_player(2, {{"team", "Red"}});
  tap("A");
  auto r = tap("B");
  EXPECT_EQ(r.detail, "Red +1 - Team total 2");
  EXPECT_EQ(engine.board_json(10)["teams"][0]["points"], 2);
}

TEST_F(TeamLinesTest, NoAutoAssignKeepsRidersTeamless) {
  add_player("A");
  start("team_lines", {{"teams", "Red,Blue"}, {"auto_assign", false}});
  auto r = tap("A");
  EXPECT_EQ(engine.players().find(1)->team, "");
  EXPECT_EQ(r.headline, "FARE PAID");
  EXPECT_EQ(r.detail, "+1 pts · Balance 1");  // falls back to the normal detail
}

TEST_F(TeamLinesTest, TeamListValidation) {
  EXPECT_THROW(engine.select_game("team_lines", {{"teams", "Solo"}}), EngineError);
  EXPECT_THROW(engine.select_game("team_lines", {{"teams", "A,A"}}), EngineError);  // duplicates collapse to 1
  EXPECT_THROW(engine.select_game("team_lines", {{"teams", "a,b,c,d,e,f,g"}}), EngineError);
  EXPECT_NO_THROW(engine.select_game("team_lines", {{"teams", " Link ,  Bus "}}));
}

// ---------------------------------------------------------------- Last Stop

class LastStopTest : public GameHarness {
 protected:
  void SetUp() override {
    add_player("A");
    add_player("B");
  }
};

TEST_F(LastStopTest, HolderEarnsPointsEachInterval) {
  start("last_stop", {{"interval", seconds(30)}, {"points", 2}});
  auto r = tap("A");
  EXPECT_EQ(r.headline, "ALL ABOARD");
  EXPECT_EQ(r.points, 0);
  advance(seconds(29));
  EXPECT_EQ(points("A"), 0);
  advance(seconds(1));
  EXPECT_EQ(points("A"), 2);
  advance(seconds(60));
  EXPECT_EQ(points("A"), 6);
}

TEST_F(LastStopTest, AnotherRiderTakesTheBus) {
  start("last_stop", {{"interval", seconds(30)}});
  tap("A");
  advance(seconds(45));  // A: 1 award
  auto r = tap("B");
  EXPECT_EQ(r.detail, "You took the bus from A");
  advance(seconds(30));
  EXPECT_EQ(points("A"), 1);
  EXPECT_EQ(points("B"), 1);
  EXPECT_EQ(engine.status_json()["game"]["status"]["holder_name"], "B");
}

TEST_F(LastStopTest, HolderTappingAgainIsRejectedAndCooldownStopsTapWars) {
  start("last_stop", {{"interval", seconds(30)}, {"cooldown", minutes(1)}});
  tap("A");
  EXPECT_EQ(tap("A").headline, "STILL ON THE BUS");
  tap("B");
  auto r = tap("A");  // A tapped 0 s ago
  EXPECT_EQ(r.outcome, Outcome::Cooldown);
  EXPECT_EQ(r.detail, "Next boarding in 1:00");
  advance(minutes(1));
  EXPECT_EQ(tap("A").headline, "ALL ABOARD");
}

TEST_F(LastStopTest, RushHourDoublesRideAwards) {
  start("last_stop", {{"interval", seconds(30)}});
  tap("A");
  engine.trigger_event(EventKind::Multiplier, "RUSH", minutes(5), 2);
  advance(seconds(30));
  EXPECT_EQ(points("A"), 2);
}

TEST_F(LastStopTest, NoAwardsWhilePaused) {
  start("last_stop", {{"interval", seconds(30)}});
  tap("A");
  engine.pause();
  advance(minutes(5));
  EXPECT_EQ(points("A"), 0);
}

// ---------------------------------------------------------------- Fare Inspector

class FareInspectorTest : public GameHarness {
 protected:
  void SetUp() override {
    add_player("A");
    add_player("B");
    add_player("C");
  }
  void inspect(orca::game::TimeMs length = minutes(1)) {
    engine.trigger_event(EventKind::Custom, "FARE INSPECTION", length, 1);
  }
};

TEST_F(FareInspectorTest, EvadersAreFinedWhenInspectionEnds) {
  start("fare_inspector", {{"valid_for", minutes(5)}, {"fine", 3}, {"cooldown", minutes(5)}});
  tap("A");  // t=0: valid until 5:00
  tap("B");
  advance(minutes(10));  // both tickets expired
  tap("C");              // C has a fresh fare
  inspect();
  tap("B");  // B shows a ticket during the inspection
  advance(minutes(1));
  EXPECT_EQ(points("A"), 1 - 3);  // fined
  EXPECT_EQ(points("B"), 2);      // checked (and the tap was a new fare)
  EXPECT_EQ(points("C"), 1);      // recent fare
  bool over = false;
  for (const auto& e : out.log) over |= e.value("headline", "") == "INSPECTION OVER" && e.value("detail", "") == "1 rider fined 3 pts";
  EXPECT_TRUE(over);
}

TEST_F(FareInspectorTest, TapDuringCooldownStillCountsAsTicket) {
  start("fare_inspector", {{"valid_for", 0}, {"cooldown", minutes(5)}});
  tap("A");
  advance(seconds(10));
  inspect();
  auto r = tap("A");
  EXPECT_EQ(r.outcome, Outcome::Cooldown);
  EXPECT_EQ(r.detail, "Ticket checked - you're safe");
  advance(minutes(1));
  EXPECT_EQ(points("A"), 1);
}

TEST_F(FareInspectorTest, RidersWhoNeverRodeAreNotFined) {
  start("fare_inspector", {{"valid_for", 0}});
  inspect();
  advance(minutes(1));
  EXPECT_EQ(points("A"), 0);
}

TEST_F(FareInspectorTest, OnlyTheGameAnnouncesInspections) {
  start("fare_inspector");
  out.clear();
  inspect();
  int banners = 0;
  for (const auto& e : out.log) banners += e.value("kind", "") == "banner";
  EXPECT_EQ(banners, 1);
}

TEST_F(FareInspectorTest, InspectionsHappenRandomly) {
  start("fare_inspector", {{"every", minutes(5)}, {"length", seconds(30)}});
  advance(minutes(30), 1000);
  int starts = 0;
  for (const auto& m : messages) starts += m.value("type", "") == "event" && m["action"] == "start";
  EXPECT_GE(starts, 2);
}

TEST_F(FareInspectorTest, SurvivesSnapshotMidInspection) {
  start("fare_inspector", {{"valid_for", 0}, {"fine", 5}});
  tap("A");
  tap("B");
  advance(seconds(1));
  inspect(minutes(2));
  tap("A");  // checked
  const auto snap = engine.snapshot();
  orca::fakes::FakeClock clock2(0);
  orca::fakes::RecordingOutputs out2;
  orca::game::Engine e2(clock2, rng, out2, registry_with_games());
  e2.restore(snap);
  for (int i = 0; i < 1300; ++i) {
    clock2.advance(100);
    e2.tick();
  }
  EXPECT_EQ(e2.scores().points(1), 1);       // A was checked
  EXPECT_EQ(e2.scores().points(2), 1 - 5);   // B evaded
}

// ---------------------------------------------------------------- Lottery

class LotteryTest : public GameHarness {};

TEST_F(LotteryTest, DrawsWeightedWinnersWithoutRepeats) {
  for (const char* n : {"A", "B", "C"}) add_player(n);
  start("lottery", {{"cooldown", 0}});
  for (int i = 0; i < 8; ++i) tap("A");
  tap("B");
  // C has no tickets.
  std::set<int> winners;
  auto first = engine.game_action("draw", json::object());
  winners.insert(first["winner"].get<int>());
  auto second = engine.game_action("draw", json::object());
  winners.insert(second["winner"].get<int>());
  EXPECT_EQ(winners, (std::set<int>{1, 2}));
  EXPECT_EQ(first["pool"], 9);
  EXPECT_THROW(engine.game_action("draw", json::object()), EngineError);  // only C left, no tickets
  EXPECT_EQ(engine.status_json()["game"]["status"]["winners"].size(), 2u);
  EXPECT_EQ(out.last_screen.headline, "WINNER!");
}

TEST_F(LotteryTest, HeavyTicketHoldersWinMoreOften) {
  int a_wins = 0;
  for (std::uint64_t seed = 1; seed <= 200; ++seed) {
    orca::fakes::FakeClock c(0);
    orca::fakes::SeededRandom r(seed);
    orca::fakes::RecordingOutputs o;
    orca::game::Engine e(c, r, o, registry_with_games());
    e.enroll("04000001", "A");
    e.enroll("04000002", "B");
    e.select_game("lottery", {{"cooldown", 0}});
    e.start();
    for (int i = 0; i < 9; ++i) e.tap("04000001");
    e.tap("04000002");
    a_wins += e.game_action("draw", json::object())["winner"] == 1;
  }
  EXPECT_GT(a_wins, 160);  // expected 180 (90%)
  EXPECT_LT(a_wins, 197);
}

TEST_F(LotteryTest, BannedRidersCannotWin) {
  add_player("A");
  add_player("B");
  start("lottery", {{"cooldown", 0}});
  tap("A");
  tap("B");
  engine.update_player(1, {{"banned", true}});
  EXPECT_EQ(engine.game_action("draw", json::object())["winner"], 2);
}

TEST_F(LotteryTest, ActionValidation) {
  add_player("A");
  engine.select_game("lottery", json::object());
  EXPECT_THROW(engine.game_action("draw", json::object()), EngineError);  // not started
  engine.start();
  try {
    engine.game_action("explode", json::object());
    FAIL();
  } catch (const EngineError& e) {
    EXPECT_EQ(e.status(), 404);
  }
}

TEST_F(LotteryTest, GamesJsonListsActions) {
  for (const auto& g : engine.games_json()) {
    if (g["id"] == "lottery") {
      EXPECT_EQ(g["actions"][0]["id"], "draw");
      return;
    }
  }
  FAIL() << "lottery not registered";
}
