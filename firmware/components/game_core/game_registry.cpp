#include "orca/game/game.hpp"

namespace orca::game {

void GameRegistry::add(GameFactory factory) {
  GameInfo info = factory()->info();
  if (find(info.id)) throw std::logic_error("duplicate game id: " + info.id);
  entries_.push_back({std::move(info), std::move(factory)});
}

std::unique_ptr<Game> GameRegistry::create(const std::string& id) const {
  for (const auto& e : entries_)
    if (e.info.id == id) return e.factory();
  return nullptr;
}

const GameInfo* GameRegistry::find(const std::string& id) const {
  for (const auto& e : entries_)
    if (e.info.id == id) return &e.info;
  return nullptr;
}

std::vector<const GameInfo*> GameRegistry::all() const {
  std::vector<const GameInfo*> out;
  for (const auto& e : entries_) out.push_back(&e.info);
  return out;
}

}  // namespace orca::game
