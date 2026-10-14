#pragma once

#include <map>
#include <set>
#include <vector>

#include "orca/games/fare_paid.hpp"

namespace orca::games {

// Fare Paid in teams named after transit lines. Riders without a team are put on the
// smallest one when they first tap; the screen shows the team's running total.
class TeamLines : public FarePaid {
 public:
  TeamLines();
  void configure(const json& params) override;
  Feedback on_tap(GameContext& ctx, Player& player) override;
  json status(const GameContext& ctx) const override;

  const std::vector<std::string>& teams() const { return teams_; }

 private:
  std::vector<std::string> teams_;
  bool auto_assign_ = true;
};

// King of the hill: the last rider to tap is "on the bus" and earns points on a timer until
// someone else taps. A short per-rider cooldown stops tap-wars.
class LastStop : public Game {
 public:
  LastStop();
  const GameInfo& info() const override { return info_; }
  void configure(const json& params) override;
  json params() const override { return params_; }
  Feedback on_tap(GameContext& ctx, Player& player) override;
  void on_tick(GameContext& ctx) override;
  json status(const GameContext& ctx) const override;
  json save() const override;
  void restore(const json& j) override;

 private:
  GameInfo info_;
  json params_;
  TimeMs interval_ = 30000;
  int points_ = 1;
  TimeMs cooldown_ = 60000;
  int holder_ = 0;
  TimeMs next_award_ = 0;
  std::map<int, TimeMs> last_tap_;
};

// Fare Paid plus fare inspections (random, or a "custom" event from the phone). During an
// inspection every rider must show a valid ticket: a fare in the last `valid_for`, or a tap
// during the inspection. When it ends, riders without one are fined.
class FareInspector : public FarePaid {
 public:
  FareInspector();
  void configure(const json& params) override;
  void start(GameContext& ctx) override;
  Feedback on_tap(GameContext& ctx, Player& player) override;
  void on_event(GameContext& ctx, const EventWindow& w, bool started) override;
  json status(const GameContext& ctx) const override;
  json save() const override;
  void restore(const json& j) override;

 private:
  RandomSchedule schedule_;
  TimeMs valid_for_ = 0;
  int fine_ = 3;
  bool inspecting_ = false;
  std::set<int> checked_;
};

// Every accepted tap is a raffle ticket (1 point = 1 ticket). The operator draws winners
// from the phone; each draw is weighted by tickets and never picks the same rider twice.
class Lottery : public FarePaid {
 public:
  Lottery();
  json on_action(GameContext& ctx, const std::string& action, const json& args) override;
  json status(const GameContext& ctx) const override;
  json save() const override;
  void restore(const json& j) override;

 private:
  std::vector<int> winners_;
};

}  // namespace orca::games
