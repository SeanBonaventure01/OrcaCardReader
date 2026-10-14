#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "orca/game/player_registry.hpp"
#include "orca/game/types.hpp"

namespace orca::game {

struct PlayerStats {
  std::int64_t points = 0;
  int taps = 0;
  int accepted = 0;  // Accepted or Bonus
  TimeMs last_tap = -1;
};

struct BoardRow {
  int number;
  std::int64_t points;
};

class ScoreBoard {
 public:
  void add_points(int number, std::int64_t delta);
  void record_tap(int number, Outcome outcome, TimeMs now);

  std::int64_t points(int number) const;
  PlayerStats stats(int number) const;
  std::int64_t total() const;

  // Highest points first; ties go to the lower player number. Players with no stats are
  // omitted. limit 0 = everyone.
  std::vector<BoardRow> leaderboard(std::size_t limit = 0) const;
  // Team name -> total points, highest first. Players without a team are ignored.
  std::vector<std::pair<std::string, std::int64_t>> team_totals(const PlayerRegistry& players) const;

  void clear();
  // Increments on every change, so observers can cheaply detect updates.
  std::uint64_t version() const { return version_; }

  json to_json() const;
  void from_json(const json& j);

 private:
  std::map<int, PlayerStats> stats_;
  std::uint64_t version_ = 0;
};

}  // namespace orca::game
