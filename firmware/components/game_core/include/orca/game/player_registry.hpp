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
  // Adds a player with a specific number (a programmed card carries its number). Throws
  // EngineError(409) if the UID is enrolled or the number is taken, 400 if number < 1.
  Player& enroll_as(const std::string& uid, int number, const std::string& name = "");
  // Makes card `uid` player `number` (a programmed card's number is who it is): creates the
  // player if needed, and takes the card away from any other player, and the player's old
  // card away from it (a replacement card keeps the score). Such players have an empty uid.
  Player& bind(const std::string& uid, int number);
  // The smallest player number >= `from` that no card plays as: no player has it, or its
  // player's card was replaced (empty uid), so a new card can take it over.
  int next_unclaimed(int from = 1) const;

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
