#include "orca/game/player_registry.hpp"

#include <algorithm>
#include <set>

namespace orca::game {

Player* PlayerRegistry::find_uid(const std::string& uid) {
  auto it = by_uid_.find(uid);
  return it == by_uid_.end() ? nullptr : &players_[it->second];
}

const Player* PlayerRegistry::find_uid(const std::string& uid) const {
  return const_cast<PlayerRegistry*>(this)->find_uid(uid);
}

Player* PlayerRegistry::find(int number) {
  auto it = std::lower_bound(players_.begin(), players_.end(), number,
                             [](const Player& p, int n) { return p.number < n; });
  return (it != players_.end() && it->number == number) ? &*it : nullptr;
}

const Player* PlayerRegistry::find(int number) const { return const_cast<PlayerRegistry*>(this)->find(number); }

Player& PlayerRegistry::enroll(const std::string& uid, const std::string& name) {
  if (uid.empty()) throw EngineError(400, "uid required");
  if (by_uid_.count(uid)) throw EngineError(409, "card already enrolled");
  Player p;
  p.number = next_number_++;
  p.uid = uid;
  p.name = name.empty() ? "Rider " + std::to_string(p.number) : name;
  players_.push_back(p);
  by_uid_[uid] = players_.size() - 1;
  return players_.back();
}

Player& PlayerRegistry::enroll_as(const std::string& uid, int number, const std::string& name) {
  if (uid.empty()) throw EngineError(400, "uid required");
  if (number < 1) throw EngineError(400, "player number must be at least 1");
  if (by_uid_.count(uid)) throw EngineError(409, "card already enrolled");
  if (find(number)) throw EngineError(409, "player number already taken");
  Player p;
  p.number = number;
  p.uid = uid;
  p.name = name.empty() ? "Rider " + std::to_string(number) : name;
  auto it = std::lower_bound(players_.begin(), players_.end(), number,
                             [](const Player& q, int n) { return q.number < n; });
  const auto at = static_cast<std::size_t>(it - players_.begin());
  players_.insert(it, p);
  for (std::size_t i = at; i < players_.size(); ++i)
    if (!players_[i].uid.empty()) by_uid_[players_[i].uid] = i;
  next_number_ = std::max(next_number_, number + 1);
  return players_[at];
}

Player& PlayerRegistry::bind(const std::string& uid, int number) {
  if (uid.empty()) throw EngineError(400, "uid required");
  if (number < 1) throw EngineError(400, "player number must be at least 1");
  if (Player* owner = find_uid(uid)) {
    if (owner->number == number) return *owner;
    owner->uid.clear();  // the card now belongs to `number`
    by_uid_.erase(uid);
  }
  Player* p = find(number);
  if (!p) return enroll_as(uid, number);
  if (!p->uid.empty()) by_uid_.erase(p->uid);  // its old card no longer plays as this player
  p->uid = uid;
  by_uid_[uid] = static_cast<std::size_t>(p - players_.data());
  return *p;
}

int PlayerRegistry::next_unclaimed(int from) const {
  int n = std::max(from, 1);
  for (const Player* p = find(n); p && !p->uid.empty(); p = find(n)) ++n;
  return n;
}

std::vector<std::string> PlayerRegistry::teams() const {
  std::set<std::string> t;
  for (const auto& p : players_)
    if (!p.team.empty()) t.insert(p.team);
  return {t.begin(), t.end()};
}

void PlayerRegistry::clear() {
  players_.clear();
  by_uid_.clear();
  next_number_ = 1;
}

json PlayerRegistry::to_json() const { return json{{"next", next_number_}, {"players", players_}}; }

void PlayerRegistry::from_json(const json& j) {
  clear();
  players_ = j.at("players").get<std::vector<Player>>();
  std::sort(players_.begin(), players_.end(), [](const Player& a, const Player& b) { return a.number < b.number; });
  int max_no = 0;
  for (std::size_t i = 0; i < players_.size(); ++i) {
    if (!players_[i].uid.empty()) by_uid_[players_[i].uid] = i;
    max_no = std::max(max_no, players_[i].number);
  }
  next_number_ = std::max(j.value("next", 1), max_no + 1);
}

}  // namespace orca::game
