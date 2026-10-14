#include "orca/game/score_board.hpp"

#include <algorithm>

namespace orca::game {

void ScoreBoard::add_points(int number, std::int64_t delta) {
  if (delta == 0) return;
  stats_[number].points += delta;
  ++version_;
}

void ScoreBoard::record_tap(int number, Outcome outcome, TimeMs now) {
  auto& s = stats_[number];
  ++s.taps;
  if (outcome == Outcome::Accepted || outcome == Outcome::Bonus) ++s.accepted;
  s.last_tap = now;
  ++version_;
}

std::int64_t ScoreBoard::points(int number) const {
  auto it = stats_.find(number);
  return it == stats_.end() ? 0 : it->second.points;
}

PlayerStats ScoreBoard::stats(int number) const {
  auto it = stats_.find(number);
  return it == stats_.end() ? PlayerStats{} : it->second;
}

std::int64_t ScoreBoard::total() const {
  std::int64_t t = 0;
  for (const auto& [_, s] : stats_) t += s.points;
  return t;
}

std::vector<BoardRow> ScoreBoard::leaderboard(std::size_t limit) const {
  std::vector<BoardRow> rows;
  rows.reserve(stats_.size());
  for (const auto& [no, s] : stats_) rows.push_back({no, s.points});
  std::stable_sort(rows.begin(), rows.end(), [](const BoardRow& a, const BoardRow& b) {
    return a.points != b.points ? a.points > b.points : a.number < b.number;
  });
  if (limit && rows.size() > limit) rows.resize(limit);
  return rows;
}

std::vector<std::pair<std::string, std::int64_t>> ScoreBoard::team_totals(const PlayerRegistry& players) const {
  std::map<std::string, std::int64_t> totals;
  for (const auto& team : players.teams()) totals[team] = 0;
  for (const auto& [no, s] : stats_) {
    const Player* p = players.find(no);
    if (p && !p->team.empty()) totals[p->team] += s.points;
  }
  std::vector<std::pair<std::string, std::int64_t>> out(totals.begin(), totals.end());
  std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  return out;
}

void ScoreBoard::clear() {
  stats_.clear();
  ++version_;
}

json ScoreBoard::to_json() const {
  json arr = json::array();
  for (const auto& [no, s] : stats_)
    arr.push_back({{"no", no}, {"points", s.points}, {"taps", s.taps}, {"accepted", s.accepted}, {"last", s.last_tap}});
  return arr;
}

void ScoreBoard::from_json(const json& j) {
  stats_.clear();
  for (const auto& row : j) {
    PlayerStats s;
    s.points = row.at("points").get<std::int64_t>();
    s.taps = row.value("taps", 0);
    s.accepted = row.value("accepted", 0);
    s.last_tap = row.value("last", TimeMs{-1});
    stats_[row.at("no").get<int>()] = s;
  }
  ++version_;
}

}  // namespace orca::game
