#include "orca/games/fare_paid.hpp"

namespace orca::games {

using hal::minutes;
using hal::seconds;

ParamSchema FarePaid::base_params() {
  return {
      {"cooldown", "Cooldown between taps", ParamType::Duration, minutes(5), 0, static_cast<double>(minutes(60)), {},
       "How long a card must wait before it can score again"},
      {"points", "Points per fare", ParamType::Int, 1, 1, 100, {}, ""},
  };
}

FarePaid::FarePaid()
    : FarePaid(GameInfo{"fare_paid", "Fare Paid", "Tap to pay your fare: 1 point per tap, then a cooldown.",
                        base_params()}) {}

FarePaid::FarePaid(GameInfo info) : info_(std::move(info)) {}

void FarePaid::configure(const json& params) {
  params_ = params;
  cooldown_ = params.at("cooldown").get<TimeMs>();
  points_ = params.at("points").get<int>();
}

TimeMs FarePaid::cooldown_left(int number, TimeMs now) const {
  auto it = last_accepted_.find(number);
  if (it == last_accepted_.end()) return 0;
  const TimeMs left = it->second + cooldown_ - now;
  return left > 0 ? left : 0;
}

Feedback FarePaid::on_tap(GameContext& ctx, Player& player) {
  Feedback fb;
  const TimeMs left = cooldown_left(player.number, ctx.now);
  if (left > 0 && !ctx.events.cooldown_waived(ctx.now)) {
    fb.outcome = Outcome::Cooldown;
    fb.detail = "Next fare in " + format_mmss(left);
    return fb;
  }
  last_accepted_[player.number] = ctx.now;
  fb.outcome = Outcome::Accepted;
  fb.points = points_;
  return fb;
}

json FarePaid::status(const GameContext&) const { return json{{"cooldown_ms", cooldown_}}; }

json FarePaid::save() const {
  json last = json::array();
  for (const auto& [no, t] : last_accepted_) last.push_back({no, t});
  return json{{"last", last}};
}

void FarePaid::restore(const json& j) {
  last_accepted_.clear();
  for (const auto& pair : j.at("last")) last_accepted_[pair.at(0).get<int>()] = pair.at(1).get<TimeMs>();
}

// ---------------------------------------------------------------- RushHour

RushHour::RushHour()
    : FarePaid(GameInfo{"rush_hour", "Rush Hour",
                        "Fare Paid with surprise rush hours: points are multiplied while they last.", {}}) {
  info_.params = base_params();
  info_.params.push_back({"every", "Average time between rush hours", ParamType::Duration, minutes(15),
                          static_cast<double>(minutes(1)), static_cast<double>(minutes(120)), {}, ""});
  info_.params.push_back({"length", "Rush hour length", ParamType::Duration, minutes(2),
                          static_cast<double>(seconds(10)), static_cast<double>(minutes(30)), {}, ""});
  info_.params.push_back({"multiplier", "Points multiplier", ParamType::Float, 2.0, 1, 10, {}, ""});
}

void RushHour::configure(const json& params) {
  FarePaid::configure(params);
  schedule_.enabled = true;
  schedule_.mean_interval = params.at("every").get<TimeMs>();
  schedule_.duration = params.at("length").get<TimeMs>();
  schedule_.multiplier = params.at("multiplier").get<double>();
  schedule_.jitter = 0.5;
  schedule_.kind = EventKind::Multiplier;
  schedule_.name = "RUSH HOUR";
}

void RushHour::start(GameContext& ctx) { ctx.events.configure_random(schedule_, ctx.now, ctx.rng); }

json RushHour::status(const GameContext& ctx) const {
  json j = FarePaid::status(ctx);
  j["multiplier"] = schedule_.multiplier;
  return j;
}

}  // namespace orca::games
