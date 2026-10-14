#include "orca/games/more_games.hpp"

#include <algorithm>
#include <sstream>

namespace orca::games {

using hal::minutes;
using hal::seconds;

// ---------------------------------------------------------------- Team Lines

namespace {
std::vector<std::string> split_teams(const std::string& csv) {
  std::vector<std::string> out;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    const auto b = item.find_first_not_of(' ');
    if (b == std::string::npos) continue;
    item = item.substr(b, item.find_last_not_of(' ') - b + 1);
    if (item.size() <= 24 && std::find(out.begin(), out.end(), item) == out.end()) out.push_back(item);
  }
  return out;
}
}  // namespace

TeamLines::TeamLines()
    : FarePaid(GameInfo{"team_lines", "Team Lines",
                        "Fare Paid in teams named after transit lines. New riders join the smallest team.", {}, {}}) {
  info_.params = base_params();
  info_.params.push_back({"teams", "Teams (comma-separated)", ParamType::String, "1 Line, Sounder, RapidRide", 0, 120, {},
                          "2 to 6 team names"});
  info_.params.push_back({"auto_assign", "Put new riders on a team", ParamType::Bool, true, 0, 0, {}, ""});
}

void TeamLines::configure(const json& params) {
  FarePaid::configure(params);
  teams_ = split_teams(params.at("teams").get<std::string>());
  if (teams_.size() < 2 || teams_.size() > 6) throw EngineError(400, "teams: give 2 to 6 different names");
  auto_assign_ = params.at("auto_assign").get<bool>();
}

Feedback TeamLines::on_tap(GameContext& ctx, Player& player) {
  if (player.team.empty() && auto_assign_) {
    std::map<std::string, int> sizes;
    for (const auto& t : teams_) sizes[t] = 0;
    for (const auto& p : ctx.players.all())
      if (sizes.count(p.team)) ++sizes[p.team];
    // Smallest team; ties go to the first listed.
    std::string best = teams_.front();
    for (const auto& t : teams_)
      if (sizes[t] < sizes[best]) best = t;
    player.team = best;
  }
  Feedback fb = fare(ctx, player);
  if (fb.outcome == Outcome::Accepted && !player.team.empty()) {
    fb.headline = "FARE PAID";
    fb.detail = "{team} +{points} - Team total {team_total}";
  }
  return fb;
}

json TeamLines::status(const GameContext& ctx) const {
  json j = FarePaid::status(ctx);
  j["teams"] = teams_;
  return j;
}

// ---------------------------------------------------------------- Last Stop

LastStop::LastStop()
    : info_{"last_stop", "Last Stop",
            "King of the hill: the last rider to tap is on the bus and earns points until someone else taps.",
            {
                {"interval", "Award points every", ParamType::Duration, seconds(30), static_cast<double>(seconds(5)),
                 static_cast<double>(minutes(10)), {}, ""},
                {"points", "Points per award", ParamType::Int, 1, 1, 100, {}, ""},
                {"cooldown", "Cooldown between a rider's taps", ParamType::Duration, minutes(1), 0,
                 static_cast<double>(minutes(30)), {}, "Stops two riders tapping back and forth"},
            },
            {}} {}

void LastStop::configure(const json& params) {
  params_ = params;
  interval_ = params.at("interval").get<TimeMs>();
  points_ = params.at("points").get<int>();
  cooldown_ = params.at("cooldown").get<TimeMs>();
}

Feedback LastStop::on_tap(GameContext& ctx, Player& player) {
  Feedback fb;
  if (player.number == holder_) {
    fb.outcome = Outcome::Rejected;
    fb.headline = "STILL ON THE BUS";
    fb.detail = "Next stop in " + format_mmss(next_award_ - ctx.now);
    return fb;
  }
  auto it = last_tap_.find(player.number);
  if (it != last_tap_.end() && ctx.now - it->second < cooldown_ && !ctx.events.cooldown_waived(ctx.now)) {
    fb.outcome = Outcome::Cooldown;
    fb.detail = "Next boarding in " + format_mmss(it->second + cooldown_ - ctx.now);
    return fb;
  }
  last_tap_[player.number] = ctx.now;
  const Player* previous = ctx.players.find(holder_);
  holder_ = player.number;
  next_award_ = ctx.now + interval_;
  fb.outcome = Outcome::Accepted;
  fb.points = 0;  // points come while riding
  fb.headline = "ALL ABOARD";
  fb.detail = previous ? "You took the bus from " + previous->name : "You're on the bus!";
  return fb;
}

void LastStop::on_tick(GameContext& ctx) {
  if (!holder_ || ctx.now < next_award_) return;
  // Catch up if ticks were missed, one award per interval.
  while (ctx.now >= next_award_) {
    const double mult = ctx.events.multiplier(next_award_);
    ctx.scores.add_points(holder_, static_cast<std::int64_t>(points_ * mult + 0.5));
    next_award_ += interval_;
  }
}

json LastStop::status(const GameContext& ctx) const {
  const Player* p = ctx.players.find(holder_);
  return json{{"holder", holder_ ? json(holder_) : json(nullptr)},
              {"holder_name", p ? json(p->name) : json(nullptr)},
              {"next_award_in_ms", holder_ ? json(next_award_ - ctx.now) : json(nullptr)}};
}

json LastStop::save() const {
  json last = json::array();
  for (const auto& [no, t] : last_tap_) last.push_back({no, t});
  return json{{"holder", holder_}, {"next_award", next_award_}, {"last", last}};
}

void LastStop::restore(const json& j) {
  holder_ = j.at("holder").get<int>();
  next_award_ = j.at("next_award").get<TimeMs>();
  last_tap_.clear();
  for (const auto& pair : j.at("last")) last_tap_[pair.at(0).get<int>()] = pair.at(1).get<TimeMs>();
}

// ---------------------------------------------------------------- Fare Inspector

FareInspector::FareInspector()
    : FarePaid(GameInfo{"fare_inspector", "Fare Inspector",
                        "Fare Paid with surprise inspections: tap during one (or have a recent fare) or get fined.", {}, {}}) {
  info_.params = base_params();
  info_.params.push_back({"every", "Average time between inspections", ParamType::Duration, minutes(12),
                          static_cast<double>(minutes(1)), static_cast<double>(minutes(120)), {}, ""});
  info_.params.push_back({"length", "Inspection length", ParamType::Duration, minutes(1), static_cast<double>(seconds(10)),
                          static_cast<double>(minutes(10)), {}, ""});
  info_.params.push_back({"valid_for", "A fare counts as a valid ticket for", ParamType::Duration, minutes(5), 0,
                          static_cast<double>(minutes(60)), {}, ""});
  info_.params.push_back({"fine", "Fine for fare evasion (points)", ParamType::Int, 3, 0, 100, {}, ""});
}

void FareInspector::configure(const json& params) {
  FarePaid::configure(params);
  valid_for_ = params.at("valid_for").get<TimeMs>();
  fine_ = params.at("fine").get<int>();
  schedule_.enabled = true;
  schedule_.mean_interval = params.at("every").get<TimeMs>();
  schedule_.duration = params.at("length").get<TimeMs>();
  schedule_.kind = EventKind::Custom;
  schedule_.name = "FARE INSPECTION";
  schedule_.tag = "inspection";
}

void FareInspector::start(GameContext& ctx) { ctx.events.configure_random(schedule_, ctx.now, ctx.rng); }

Feedback FareInspector::on_tap(GameContext& ctx, Player& player) {
  Feedback fb = fare(ctx, player);
  if (inspecting_) {
    checked_.insert(player.number);
    if (fb.outcome == Outcome::Accepted) {
      fb.headline = "TICKET CHECKED";
    } else {
      fb.detail = "Ticket checked - you're safe";
    }
  }
  return fb;
}

void FareInspector::on_event(GameContext& ctx, const EventWindow& w, bool started) {
  if (w.kind != EventKind::Custom) return;
  if (started) {
    inspecting_ = true;
    checked_.clear();
    // A recent fare is a valid ticket.
    for (const auto& [no, t] : last_accepted_)
      if (w.start - t < valid_for_) checked_.insert(no);
    ctx.fx.announce("FARE INSPECTION", "Tap your card to show your ticket!", hal::Tone::Warning, "event_start",
                    "event_strobe");
    return;
  }
  if (!inspecting_) return;
  inspecting_ = false;
  // Everyone who has ridden this game and didn't show a ticket is fined.
  int fined = 0;
  for (const auto& [no, t] : last_accepted_) {
    (void)t;
    if (!checked_.count(no) && fine_ > 0) {
      ctx.scores.add_points(no, -fine_);
      ++fined;
    }
  }
  checked_.clear();
  ctx.fx.announce("INSPECTION OVER",
                  fined ? std::to_string(fined) + (fined == 1 ? " rider" : " riders") + " fined " + std::to_string(fine_) + " pts"
                        : "Everyone had a ticket!",
                  hal::Tone::Neutral, "event_end", "");
}

json FareInspector::status(const GameContext& ctx) const {
  json j = FarePaid::status(ctx);
  j["inspecting"] = inspecting_;
  j["checked"] = checked_.size();
  return j;
}

json FareInspector::save() const {
  json j = FarePaid::save();
  j["inspecting"] = inspecting_;
  j["checked"] = std::vector<int>(checked_.begin(), checked_.end());
  return j;
}

void FareInspector::restore(const json& j) {
  FarePaid::restore(j);
  inspecting_ = j.value("inspecting", false);
  checked_.clear();
  for (int no : j.value("checked", std::vector<int>{})) checked_.insert(no);
}

// ---------------------------------------------------------------- Lottery

Lottery::Lottery()
    : FarePaid(GameInfo{"lottery", "Last Call Lottery",
                        "Every fare is a raffle ticket. Draw winners from the phone at the end of the night.", {}, {}}) {
  info_.params = base_params();
  info_.params[0].def = minutes(2);  // shorter cooldown: more tickets
  info_.actions.push_back({"draw", "Draw a winner", "Weighted by tickets; never the same rider twice"});
}

json Lottery::on_action(GameContext& ctx, const std::string& action, const json&) {
  if (action != "draw") throw EngineError(404, "unknown action: " + action);
  std::vector<std::pair<int, std::int64_t>> pool;
  std::int64_t total = 0;
  for (const auto& row : ctx.scores.leaderboard()) {
    const Player* p = ctx.players.find(row.number);
    if (row.points <= 0 || !p || p->banned) continue;
    if (std::find(winners_.begin(), winners_.end(), row.number) != winners_.end()) continue;
    pool.push_back({row.number, row.points});
    total += row.points;
  }
  if (pool.empty()) throw EngineError(409, "no eligible tickets left");
  std::int64_t pick = ctx.rng.uniform(0, total - 1);
  int winner = pool.back().first;
  for (const auto& [no, tickets] : pool) {
    if (pick < tickets) {
      winner = no;
      break;
    }
    pick -= tickets;
  }
  winners_.push_back(winner);
  const Player* p = ctx.players.find(winner);
  char line[64];
  std::snprintf(line, sizeof line, "#%02d %s", winner, p->name.c_str());
  ctx.fx.announce("WINNER!", line, hal::Tone::Bonus, "bonus", "bonus_chase");
  return json{{"winner", winner}, {"name", p->name}, {"tickets", ctx.scores.points(winner)}, {"pool", total},
              {"draw", winners_.size()}};
}

json Lottery::status(const GameContext& ctx) const {
  json j = FarePaid::status(ctx);
  json w = json::array();
  for (int no : winners_) {
    const Player* p = ctx.players.find(no);
    w.push_back({{"no", no}, {"name", p ? p->name : "?"}});
  }
  j["winners"] = w;
  return j;
}

json Lottery::save() const {
  json j = FarePaid::save();
  j["winners"] = winners_;
  return j;
}

void Lottery::restore(const json& j) {
  FarePaid::restore(j);
  winners_ = j.value("winners", std::vector<int>{});
}

}  // namespace orca::games
