#pragma once

#include <map>
#include <string>
#include <vector>

#include "orca/game/types.hpp"

namespace orca::game {

// Players keyed by card UID. Numbers are assigned in enrollment order starting at 1, so
// provisioning cards in order makes card #n player #n.
class PlayerRegistry {
 public:
  Player* find_uid(const std::string& uid);
  const Player* find_uid(const std::string& uid) const;
  Player* find(int number);
  const Player* find(int number) const;

  // Adds a player for `uid`. Empty name -> "Rider <n>". Throws EngineError(409) if the
  // UID is already enrolled.
  Player& enroll(const std::string& uid, const std::string& name = "");

  const std::vector<Player>& all() const { return players_; }
  std::size_t size() const { return players_.size(); }
  std::vector<std::string> teams() const;  // distinct, non-empty, sorted
  void clear();

  json to_json() const;
  void from_json(const json& j);

 private:
  std::vector<Player> players_;  // sorted by number
  std::map<std::string, std::size_t> by_uid_;
  int next_number_ = 1;
};

}  // namespace orca::game
