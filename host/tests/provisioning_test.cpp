// Card programming: the operator arms provisioning, then taps a stack of cards so card #n is
// player #n. The engine decides what each card gets; the reader just writes and reports back.

#include "game_harness.hpp"

using namespace orca::test;
using orca::game::EngineError;
using orca::game::GameState;
using orca::game::WriteResult;

class ProvisioningTest : public GameHarness {
 protected:
  // What the device does with a real card: ask the engine, write if told to, report back.
  std::optional<int> program(const std::string& uid, std::optional<int> on_card = std::nullopt,
                             WriteResult result = WriteResult::Ok) {
    auto n = engine.card_seen(uid, on_card);
    if (n) engine.card_written(uid, *n, result);
    return n;
  }
  std::string last_result() const { return engine.provisioning().last->result; }
};

TEST_F(ProvisioningTest, BlankCardsAreNumberedInOrder) {
  engine.start_provisioning(0, false);
  EXPECT_EQ(out.last_screen.detail, "Tap card #1");  // idle screen asks for the first card
  EXPECT_EQ(program("04000001"), 1);
  EXPECT_EQ(program("04000002"), 2);
  EXPECT_EQ(program("04000003"), 3);
  EXPECT_EQ(engine.players().size(), 3u);
  EXPECT_EQ(engine.players().find_uid("04000002")->number, 2);
  EXPECT_EQ(engine.provisioning().programmed, 3);
  EXPECT_EQ(engine.provisioning().next, 4);
  EXPECT_EQ(last_result(), "written");
  EXPECT_EQ(out.last_screen.headline, "Card #3 programmed");
  EXPECT_EQ(out.last_screen.detail, "Next: card #4");
  EXPECT_EQ(out.sounds.back(), "accept");
  EXPECT_EQ(count_messages("provision"), 3);
}

TEST_F(ProvisioningTest, StartNumberSkipsNumbersAlreadyTaken) {
  add_player("Ghost");  // #1
  add_player("Witch");  // #2
  engine.start_provisioning(0, false);
  EXPECT_EQ(engine.provisioning().next, 3);
  engine.start_provisioning(10, false);
  EXPECT_EQ(program("04000010"), 10);
  EXPECT_EQ(engine.provisioning().next, 11);
  engine.start_provisioning(2, false);  // 2 is taken, 3 is free
  EXPECT_EQ(engine.provisioning().next, 3);
}

TEST_F(ProvisioningTest, StartingAGameEndsProgramming) {
  engine.start_provisioning(0, false);
  start("fare_paid");  // even if "Done" never reached the costume
  EXPECT_FALSE(engine.provisioning().active);
  EXPECT_EQ(engine.card_seen("04000001", std::nullopt), std::nullopt);  // an ordinary tap
  EXPECT_EQ(count_messages("tap"), 1);
  // Selecting a game ends it too.
  engine.stop();
  engine.start_provisioning(0, false);
  engine.select_game("fare_paid", json::object());
  EXPECT_FALSE(engine.provisioning().active);
}

TEST_F(ProvisioningTest, ANumberWhoseCardWasReprogrammedCanBeReused) {
  engine.start_provisioning(10, false);
  program("04000010");  // #10
  program("04000011");  // #11
  engine.start_provisioning(0, true);
  program("04000010", 10);  // overwrite on: card #10 gets the next number, #1
  EXPECT_EQ(engine.players().find_uid("04000010")->number, 1);
  EXPECT_EQ(engine.players().find(10)->uid, "");  // #10 has no card now
  engine.start_provisioning(10, false);
  EXPECT_EQ(engine.provisioning().next, 10);  // not 12: #10 is free for a new card
  EXPECT_EQ(program("040000AA"), 10);
  EXPECT_EQ(engine.provisioning().next, 12);  // #11 still has its card
}

TEST_F(ProvisioningTest, TapsDoNotScoreWhileProgramming) {
  add_player("Ghost");
  start("fare_paid");
  engine.start_provisioning(0, false);
  program(uid_for("Ghost"));  // writes #1 onto Ghost's card instead of paying a fare
  EXPECT_EQ(points("Ghost"), 0);
  EXPECT_EQ(engine.state(), GameState::Running);  // the game is untouched
  engine.stop_provisioning();
  EXPECT_EQ(tap("Ghost").points, 1);
}

TEST_F(ProvisioningTest, EnrolledCardGetsItsNumberWritten) {
  add_player("Ghost");  // enrolled by UID earlier, card still blank
  engine.start_provisioning(0, false);
  EXPECT_EQ(program(uid_for("Ghost")), 1);
  EXPECT_EQ(engine.players().size(), 1u);
  // Tapped again: it already carries #1, nothing to write.
  EXPECT_EQ(program(uid_for("Ghost"), 1), std::nullopt);
  EXPECT_EQ(last_result(), "already");
  EXPECT_EQ(out.sounds.back(), "reject");
}

TEST_F(ProvisioningTest, ProgrammedCardFromAnotherNightIsAdopted) {
  engine.start_provisioning(0, false);
  EXPECT_EQ(program("04000007", 7), std::nullopt);
  EXPECT_EQ(last_result(), "adopted");
  EXPECT_EQ(engine.players().find_uid("04000007")->number, 7);
  EXPECT_EQ(program("04000001"), 1);  // blank cards still start at 1
}

TEST_F(ProvisioningTest, ACardsNumberWinsOverWhoeverHadIt) {
  engine.start_provisioning(0, false);
  program("04000001");  // #1
  // Another card says #1 (a replacement for a lost card): it becomes #1.
  EXPECT_EQ(program("04000099", 1), std::nullopt);
  EXPECT_EQ(last_result(), "adopted");
  EXPECT_EQ(engine.players().find_uid("04000099")->number, 1);
  EXPECT_EQ(engine.players().find_uid("04000001"), nullptr);  // the lost card no longer plays
  EXPECT_EQ(engine.players().size(), 1u);
}

TEST_F(ProvisioningTest, OverwriteGivesProgrammedCardsTheNextNumber) {
  engine.start_provisioning(0, true);
  EXPECT_EQ(program("04000099", 5), 1);  // ignores the old #5 on the card
  EXPECT_EQ(engine.players().find_uid("04000099")->number, 1);
  EXPECT_EQ(engine.players().find(5), nullptr);
}

TEST_F(ProvisioningTest, FailedWriteKeepsTheNumberForTheRetry) {
  engine.start_provisioning(0, false);
  EXPECT_EQ(program("04000001", std::nullopt, WriteResult::Failed), 1);
  EXPECT_EQ(last_result(), "failed");
  EXPECT_EQ(out.last_screen.detail, "Hold the card still and tap again");
  EXPECT_EQ(engine.players().size(), 0u);
  EXPECT_EQ(engine.provisioning().next, 1);
  EXPECT_EQ(program("04000001"), 1);
  EXPECT_EQ(engine.provisioning().next, 2);

  EXPECT_EQ(program("04000002", std::nullopt, WriteResult::Unsupported), 2);
  EXPECT_EQ(last_result(), "unsupported");
  EXPECT_EQ(engine.players().size(), 1u);
}

TEST_F(ProvisioningTest, NotProvisioningMeansAnOrdinaryTap) {
  start("fare_paid");
  EXPECT_EQ(engine.card_seen("04000001", std::nullopt), std::nullopt);
  EXPECT_EQ(engine.players().size(), 1u);  // open enrollment
  EXPECT_EQ(count_messages("tap"), 1);
}

TEST_F(ProvisioningTest, ProgrammedCardPlaysAsItsNumberEvenWithEnrollmentClosed) {
  engine.set_open_enrollment(false);
  start("fare_paid");
  auto r = engine.tap("04000042", 42);
  EXPECT_EQ(r.outcome, Outcome::Accepted);
  EXPECT_EQ(r.player, 42);
  // A blank unknown card is still turned away.
  EXPECT_EQ(engine.tap("04000043").outcome, Outcome::Unknown);
}

TEST_F(ProvisioningTest, ReplacementCardKeepsThePlayersScore) {
  engine.set_open_enrollment(false);
  start("fare_paid");
  engine.tap("04000042", 42);
  engine.update_player(42, json{{"name", "Ghost"}});
  // Lost card #42; a new card programmed as orca-card-game-42 carries on as Ghost.
  advance(minutes(5));
  auto r = engine.tap("040000AA", 42);
  EXPECT_EQ(r.outcome, Outcome::Accepted);
  EXPECT_EQ(r.name, "Ghost");
  EXPECT_EQ(r.balance, 2);
  EXPECT_EQ(engine.tap("04000042").outcome, Outcome::Unknown);  // the old card is out
}

TEST_F(ProvisioningTest, ClearPlayersStartsFresh) {
  add_player("Ghost");
  start("fare_paid");
  tap("Ghost");
  EXPECT_THROW(engine.clear_players(), EngineError);  // not mid-game
  engine.stop();
  engine.clear_players();
  EXPECT_EQ(engine.players().size(), 0u);
  EXPECT_EQ(engine.scores().total(), 0);
  EXPECT_TRUE(engine.recent_taps_json(10).empty());
  engine.start_provisioning(0, false);
  EXPECT_EQ(engine.provisioning().next, 1);  // numbering starts over
}

TEST_F(ProvisioningTest, StatusAndStopAndValidation) {
  EXPECT_FALSE(engine.status_json()["provisioning"]["active"]);
  engine.start_provisioning(5, true);
  const json p = engine.status_json()["provisioning"];
  EXPECT_TRUE(p["active"]);
  EXPECT_EQ(p["next"], 5);
  EXPECT_TRUE(p["overwrite"]);
  EXPECT_TRUE(p["last"].is_null());
  engine.stop_provisioning();
  EXPECT_FALSE(engine.provisioning().active);
  EXPECT_EQ(out.last_screen.detail, "Not in service");  // back to the normal idle screen
  EXPECT_THROW(engine.start_provisioning(-1, false), EngineError);
  EXPECT_THROW(engine.card_written("zz", 1, WriteResult::Ok), EngineError);
}

TEST_F(ProvisioningTest, ProgrammedPlayersSurviveASnapshot) {
  engine.start_provisioning(3, false);
  program("04000001");
  const json snap = engine.snapshot();
  engine.restore(snap);
  EXPECT_EQ(engine.players().find_uid("04000001")->number, 3);
  EXPECT_FALSE(engine.provisioning().active);  // provisioning itself isn't persisted
}
