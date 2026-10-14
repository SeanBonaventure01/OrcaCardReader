#include "orca/games/registry.hpp"

#include "orca/games/fare_paid.hpp"
#include "orca/games/more_games.hpp"

namespace orca::games {

void register_builtin_games(game::GameRegistry& registry) {
  registry.add([] { return std::make_unique<FarePaid>(); });
  registry.add([] { return std::make_unique<RushHour>(); });
  registry.add([] { return std::make_unique<TeamLines>(); });
  registry.add([] { return std::make_unique<LastStop>(); });
  registry.add([] { return std::make_unique<FareInspector>(); });
  registry.add([] { return std::make_unique<Lottery>(); });
}

}  // namespace orca::games
