#pragma once

#include "orca/game/game.hpp"

namespace orca::games {

// Adds every built-in game. Registration is explicit (not static-initializer magic) because
// the linker drops unreferenced objects from ESP-IDF component libraries.
void register_builtin_games(game::GameRegistry& registry);

}  // namespace orca::games
