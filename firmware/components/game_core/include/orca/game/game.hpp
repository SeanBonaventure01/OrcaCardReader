#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "orca/game/event_scheduler.hpp"
#include "orca/game/params.hpp"
#include "orca/game/player_registry.hpp"
#include "orca/game/score_board.hpp"
#include "orca/game/types.hpp"
#include "orca/hal/outputs.hpp"
#include "orca/hal/random.hpp"

namespace orca::game {

// A button the phone UI shows for this game (e.g. Lottery's "Draw a winner").
struct GameAction {
  std::string id;
  std::string label;
  std::string help;
};

struct GameInfo {
  std::string id;  // stable, used in the API and snapshots
  std::string name;
  std::string description;
  ParamSchema params;
  std::vector<GameAction> actions;
};

// Out-of-band effects a game may trigger outside of a tap (e.g. from on_tick).
class GameEffects {
 public:
  virtual ~GameEffects() = default;
  virtual void announce(const std::string& headline, const std::string& detail, hal::Tone tone,
                        const std::string& sound = "", const std::string& leds = "") = 0;
  virtual void play(const std::string& sound) = 0;
};

// Services the engine lends to the active game. Games may change scores directly
// (e.g. awarding points on a timer); the engine notices and broadcasts.
struct GameContext {
  hal::Random& rng;
  PlayerRegistry& players;
  ScoreBoard& scores;
  EventScheduler& events;
  GameEffects& fx;
  TimeMs now;
};

class Game {
 public:
  virtual ~Game() = default;
  virtual const GameInfo& info() const = 0;

  // `params` has already been validated against info().params, with defaults filled in.
  virtual void configure(const json& params) = 0;
  virtual json params() const = 0;

  virtual void start(GameContext&) {}
  virtual void stop(GameContext&) {}
  virtual Feedback on_tap(GameContext& ctx, Player& player) = 0;
  virtual void on_tick(GameContext&) {}
  virtual void on_event(GameContext&, const EventWindow&, bool /*started*/) {}

  // Extra fields for the status API and idle screen (e.g. "holder" in Last Stop).
  virtual json status(const GameContext&) const { return json::object(); }

  // Runs one of info().actions. Returns a result for the API; throw EngineError for bad input.
  virtual json on_action(GameContext&, const std::string& action, const json& /*args*/) {
    throw EngineError(404, "unknown action: " + action);
  }

  // Per-session state for snapshots. Times are engine game time, which survives reboots.
  virtual json save() const = 0;
  virtual void restore(const json& j) = 0;
};

using GameFactory = std::function<std::unique_ptr<Game>()>;

class GameRegistry {
 public:
  void add(GameFactory factory);
  std::unique_ptr<Game> create(const std::string& id) const;
  const GameInfo* find(const std::string& id) const;
  std::vector<const GameInfo*> all() const;

 private:
  struct Entry {
    GameInfo info;
    GameFactory factory;
  };
  std::vector<Entry> entries_;
};

}  // namespace orca::game
