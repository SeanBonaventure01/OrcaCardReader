#include <gtest/gtest.h>

#include "orca/game/player_registry.hpp"
#include "orca/game/score_board.hpp"

using namespace orca::game;

TEST(PlayerRegistry, EnrollAssignsSequentialNumbersAndDefaultNames) {
  PlayerRegistry r;
  EXPECT_EQ(r.enroll("AA000001").number, 1);
  Player& p2 = r.enroll("AA000002", "Ghost");
  EXPECT_EQ(p2.number, 2);
  EXPECT_EQ(p2.name, "Ghost");
  EXPECT_EQ(r.find(1)->name, "Rider 1");
  EXPECT_EQ(r.find_uid("AA000002")->number, 2);
  EXPECT_EQ(r.find(3), nullptr);
  EXPECT_EQ(r.find_uid("FFFFFFFF"), nullptr);
}

TEST(PlayerRegistry, DuplicateUidIsConflict) {
  PlayerRegistry r;
  r.enroll("AA000001");
  try {
    r.enroll("AA000001");
    FAIL() << "expected EngineError";
  } catch (const EngineError& e) {
    EXPECT_EQ(e.status(), 409);
  }
}

TEST(PlayerRegistry, PointersStayValidAcrossEnrollments) {
  // find() results are used transiently, but numbers must keep mapping correctly as the vector grows.
  PlayerRegistry r;
  for (int i = 0; i < 100; ++i) r.enroll("AA" + std::to_string(100000 + i));
  for (int i = 0; i < 100; ++i) EXPECT_EQ(r.find_uid("AA" + std::to_string(100000 + i))->number, i + 1);
}

TEST(PlayerRegistry, JsonRoundTripKeepsNextNumber) {
  PlayerRegistry r;
  r.enroll("AA000001", "A").team = "Red";
  r.enroll("AA000002", "B").banned = true;
  PlayerRegistry copy;
  copy.from_json(r.to_json());
  ASSERT_EQ(copy.size(), 2u);
  EXPECT_EQ(copy.find(1)->team, "Red");
  EXPECT_TRUE(copy.find(2)->banned);
  EXPECT_EQ(copy.enroll("AA000003").number, 3);
}

TEST(PlayerRegistry, TeamsAreDistinctSortedNonEmpty) {
  PlayerRegistry r;
  r.enroll("AA000001").team = "Sounder";
  r.enroll("AA000002").team = "Link";
  r.enroll("AA000003").team = "Sounder";
  r.enroll("AA000004");
  EXPECT_EQ(r.teams(), (std::vector<std::string>{"Link", "Sounder"}));
}

TEST(ScoreBoard, LeaderboardSortsByPointsThenNumber) {
  ScoreBoard s;
  s.add_points(3, 5);
  s.add_points(1, 5);
  s.add_points(2, 9);
  auto b = s.leaderboard();
  ASSERT_EQ(b.size(), 3u);
  EXPECT_EQ(b[0].number, 2);
  EXPECT_EQ(b[1].number, 1);  // tie with #3 goes to the lower number
  EXPECT_EQ(b[2].number, 3);
  EXPECT_EQ(s.leaderboard(2).size(), 2u);
}

TEST(ScoreBoard, RecordTapCountsAcceptedOnly) {
  ScoreBoard s;
  s.record_tap(1, Outcome::Accepted, 10);
  s.record_tap(1, Outcome::Cooldown, 20);
  s.record_tap(1, Outcome::Bonus, 30);
  auto st = s.stats(1);
  EXPECT_EQ(st.taps, 3);
  EXPECT_EQ(st.accepted, 2);
  EXPECT_EQ(st.last_tap, 30);
}

TEST(ScoreBoard, VersionChangesOnEveryMutation) {
  ScoreBoard s;
  auto v = s.version();
  s.add_points(1, 0);  // no-op
  EXPECT_EQ(s.version(), v);
  s.add_points(1, 1);
  EXPECT_GT(s.version(), v);
  v = s.version();
  s.clear();
  EXPECT_GT(s.version(), v);
}

TEST(ScoreBoard, TeamTotalsIncludeEmptyTeams) {
  PlayerRegistry r;
  r.enroll("AA000001").team = "Link";
  r.enroll("AA000002").team = "Link";
  r.enroll("AA000003").team = "Sounder";
  r.enroll("AA000004");  // no team
  ScoreBoard s;
  s.add_points(1, 3);
  s.add_points(2, 4);
  s.add_points(4, 100);
  auto t = s.team_totals(r);
  ASSERT_EQ(t.size(), 2u);
  EXPECT_EQ(t[0], (std::pair<std::string, std::int64_t>{"Link", 7}));
  EXPECT_EQ(t[1], (std::pair<std::string, std::int64_t>{"Sounder", 0}));
}

TEST(ScoreBoard, JsonRoundTrip) {
  ScoreBoard s;
  s.add_points(1, 7);
  s.record_tap(1, Outcome::Accepted, 500);
  s.add_points(2, -3);
  ScoreBoard copy;
  copy.from_json(s.to_json());
  EXPECT_EQ(copy.points(1), 7);
  EXPECT_EQ(copy.points(2), -3);
  EXPECT_EQ(copy.stats(1).last_tap, 500);
  EXPECT_EQ(copy.total(), 4);
}
