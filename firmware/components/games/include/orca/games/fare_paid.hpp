#pragma once

#include <map>

#include "orca/game/game.hpp"

namespace orca::games {

using namespace orca::game;

// Baseline: each accepted tap is worth `points`, then the card has a per-player cooldown.
class FarePaid : public Game {
 public:
  FarePaid();

  const GameInfo& info() const override { return info_; }
  void configure(const json& params) override;
  json params() const override { return params_; }
  Feedback on_tap(GameContext& ctx, Player& player) override;
  json status(const GameContext& ctx) const override;
  json save() const override;
  void restore(const json& j) override;

 protected:
  explicit FarePaid(GameInfo info);
  static ParamSchema base_params();

  // Remaining cooldown for a player (0 if they may tap now).
  TimeMs cooldown_left(int number, TimeMs now) const;

  GameInfo info_;
  json params_;
  TimeMs cooldown_ = 0;
  int points_ = 1;
  std::map<int, TimeMs> last_accepted_;
};

// Fare Paid plus random multiplier windows ("Rush Hour").
class RushHour : public FarePaid {
 public:
  RushHour();
  void configure(const json& params) override;
  void start(GameContext& ctx) override;
  json status(const GameContext& ctx) const override;

 private:
  RandomSchedule schedule_;
};

}  // namespace orca::games
