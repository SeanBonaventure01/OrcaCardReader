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

// Card programming (docs/ARCHITECTURE.md §6.1): while active, cards tapped on the reader get
// player numbers written onto them instead of playing.
struct ProvisionRecord {
  std::string uid;
  int number = 0;
  // written | already (card already carries its number) | adopted (programmed card registered)
  // | failed | unsupported (not an NTAG card)
  std::string result;
  std::string message;
};

void to_json(json& j, const ProvisionRecord& r);

struct Provisioning {
  bool active = false;
  int next = 1;            // the number the next blank card gets (always unused)
  bool overwrite = false;  // give cards that already carry a number the next number instead
  int programmed = 0;      // cards written since provisioning started
  std::optional<ProvisionRecord> last;
};

enum class WriteResult { Ok, Failed, Unsupported };

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
  // `card_player` is the player number written on a programmed card: the card plays as that
  // player (created if needed, even with enrollment closed). Blank cards are known by UID.
  TapRecord tap(const std::string& uid, std::optional<int> card_player = std::nullopt);  // throws EngineError(400) on a malformed UID
  // A card the reader saw. Not provisioning: an ordinary tap, returns nullopt. Provisioning:
  // returns the player number to write onto the card, or nullopt (with feedback on screen)
  // when it shouldn't be written. Report the write with card_written().
  std::optional<int> card_seen(const std::string& uid, std::optional<int> card_player);
  void card_written(const std::string& uid, int number, WriteResult result);
  void tick();

  // --- admin commands (throw EngineError on bad input or wrong state) ---
  void select_game(const std::string& id, const json& params, bool keep_scores = false);
  void start();
  void pause();
  void resume();
  void stop();
  void reset();  // clears scores and game state; keeps players and the selected game
  json game_action(const std::string& action, const json& args);  // one of the game's actions
  EventWindow trigger_event(EventKind kind, const std::string& name, TimeMs duration, double multiplier);
  void end_events();
  Player& enroll(const std::string& uid, const std::string& name = "");
  Player& update_player(int number, const json& patch);  // keys: name, team, banned
  void clear_players();  // removes every player and score; not while a game is running
  void adjust_score(int number, std::int64_t delta);
  void set_open_enrollment(bool open);
  // epoch_ms: Unix time; utc_offset_min: local time minus UTC (e.g. -420 for PDT).
  void sync_wall_clock(std::int64_t epoch_ms, int utc_offset_min = 0);
  void play_sound(const std::string& id);
  // start_no 0 = the next unused number. Players aren't touched until a card is written.
  void start_provisioning(int start_no, bool overwrite);
  void stop_provisioning();

  // --- queries ---
  GameState state() const { return state_; }
  Game* game() { return game_.get(); }
  bool open_enrollment() const { return open_enrollment_; }
  std::optional<std::int64_t> wall_clock_ms() const;
  PlayerRegistry& players() { return players_; }
  ScoreBoard& scores() { return scores_; }
  EventScheduler& events() { return events_; }

  const Provisioning& provisioning() const { return prov_; }
  json provisioning_json() const;
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
  std::string fill(const std::string& tmpl, const TapRecord& r, TimeMs valid_until) const;
  std::string until_text(TimeMs valid_until) const;
  void provision_feedback(ProvisionRecord rec, hal::Tone tone, const std::string& sound, const std::string& headline);

  const hal::Clock& clock_;
  hal::Random& rng_;
  hal::Outputs& out_;
  const GameRegistry& games_;
  Listener listener_;

  TimeMs offset_ = 0;
  std::optional<std::int64_t> wall_offset_;  // epoch_ms - now()
  int utc_offset_min_ = 0;
  GameState state_ = GameState::None;
  std::unique_ptr<Game> game_;
  PlayerRegistry players_;
  ScoreBoard scores_;
  EventScheduler events_;
  bool open_enrollment_ = true;
  Provisioning prov_;  // not persisted: a reboot ends provisioning
  std::int64_t tap_seq_ = 0;
  std::deque<TapRecord> recent_;
  std::uint64_t changes_ = 0;
  std::uint64_t broadcast_score_version_ = 0;
};

}  // namespace orca::game
