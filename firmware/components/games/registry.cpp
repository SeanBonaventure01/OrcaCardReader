#include "orca/games/registry.hpp"

#include "orca/games/fare_paid.hpp"

namespace orca::games {

void register_builtin_games(game::GameRegistry& registry) {
  registry.add([] { return std::make_unique<FarePaid>(); });
  registry.add([] { return std::make_unique<RushHour>(); });
}

}  // namespace orca::games
