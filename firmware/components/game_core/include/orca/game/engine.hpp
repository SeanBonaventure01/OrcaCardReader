#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "orca/game/event_scheduler.hpp"
#include "orca/game/game.hpp"
#include "orca/game/player_registry.hpp"
#include "orca/game/score_board.hpp"
#include "orca/game/types.hpp"
#include "orca/hal/clock.hpp"
#include "orca/hal/outputs.hpp"
#include "orca/hal/random.hpp"

namespace orca::game {

enum class GameState {
  None,     // no game selected
  Ready,    // selected, not started
  Running,
  Paused,
  Ended,
};

const char* to_string(GameState s);
std::optional<GameState> game_state_from_string(const std::string& s);

struct TapRecord {
  std::int64_t seq = 0;
  TimeMs t = 0;               // engine game time
  std::int64_t wall_ms = 0;   // Unix epoch ms, 0 if the wall clock isn't synced
  std::string uid;
  int player = 0;             // 0 if the card is unknown
  std::string name;
  Outcome outcome = Outcome::Closed;
  std::int64_t points = 0;    // after multipliers
  std::int64_t balance = 0;
  double multiplier = 1.0;
  std::string headline;
  std::string detail;
};

void to_json(json& j, const TapRecord& r);

// Owns all game state. Not thread-safe: on the device only game_task calls it, and on the
// host the simulator serializes access.
class Engine : public GameEffects {
 public:
  using Listener = std::function<void(const json& message)>;

  Engine(const hal::Clock& clock, hal::Random& rng, hal::Outputs& out, const GameRegistry& games);

  // Receives push messages for WebSocket clients: tap, scores, state, event, players.
  void set_listener(Listener l) { listener_ = std::move(l); }

  // Game time: the hardware clock plus an offset that carries time across restores.
  TimeMs now() const { return clock_.now_ms() + offset_; }

  // --- inputs ---
  TapRecord tap(const std::string& uid);  // throws EngineError(400) on a malformed UID
  void tick();

  // --- admin commands (throw EngineError on bad input or wrong state) ---
  void select_game(const std::string& id, const json& params, bool keep_scores = false);
  void start();
  void pause();
  void resume();
  void stop();
  void reset();  // clears scores and game state; keeps players and the selected game
  EventWindow trigger_event(EventKind kind, const std::string& name, TimeMs duration, double multiplier);
  void end_events();
  Player& enroll(const std::string& uid, const std::string& name = "");
  Player& update_player(int number, const json& patch);  // keys: name, team, banned
  void adjust_score(int number, std::int64_t delta);
  void set_open_enrollment(bool open);
  void sync_wall_clock(std::int64_t epoch_ms);
  void play_sound(const std::string& id);

  // --- queries ---
  GameState state() const { return state_; }
  Game* game() { return game_.get(); }
  bool open_enrollment() const { return open_enrollment_; }
  std::optional<std::int64_t> wall_clock_ms() const;
  PlayerRegistry& players() { return players_; }
  ScoreBoard& scores() { return scores_; }
  EventScheduler& events() { return events_; }

  json status_json();
  json players_json() const;
  json board_json(std::size_t limit) const;
  json games_json() const;
  json recent_taps_json(std::size_t limit) const;

  // --- persistence ---
  json snapshot() const;
  // Throws EngineError(400) and leaves the engine unchanged if the snapshot is invalid.
  void restore(const json& snapshot);
  // Increases whenever persisted state changes. Storage saves when it moves.
  std::uint64_t change_count() const { return changes_; }

  // --- GameEffects ---
  void announce(const std::string& headline, const std::string& detail, hal::Tone tone, const std::string& sound,
                const std::string& leds) override;
  void play(const std::string& sound) override { play_sound(sound); }

 private:
  GameContext ctx();
  void require_game() const;
  void touch() { ++changes_; }
  void broadcast(const json& msg);
  void broadcast_state();
  void broadcast_scores_if_changed();
  void show_idle();
  void handle_event_changes(const EventScheduler::Changes& ch);
  std::string fill(const std::string& tmpl, const TapRecord& r) const;

  const hal::Clock& clock_;
  hal::Random& rng_;
  hal::Outputs& out_;
  const GameRegistry& games_;
  Listener listener_;

  TimeMs offset_ = 0;
  std::optional<std::int64_t> wall_offset_;  // epoch_ms - now()
  GameState state_ = GameState::None;
  std::unique_ptr<Game> game_;
  PlayerRegistry players_;
  ScoreBoard scores_;
  EventScheduler events_;
  bool open_enrollment_ = true;
  std::int64_t tap_seq_ = 0;
  std::deque<TapRecord> recent_;
  std::uint64_t changes_ = 0;
  std::uint64_t broadcast_score_version_ = 0;
};

}  // namespace orca::game
