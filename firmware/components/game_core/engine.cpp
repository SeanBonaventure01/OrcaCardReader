#include "orca/game/engine.hpp"

#include <cmath>
#include <cstdio>

#include "orca/game/theme.hpp"
#include "orca/hal/card.hpp"

namespace orca::game {

namespace {

constexpr std::size_t kRecentTaps = 100;
constexpr int kSnapshotVersion = 1;

std::string format_mult(double m) {
  char buf[16];
  if (std::fabs(m - std::round(m)) < 1e-9)
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(m)));
  else
    std::snprintf(buf, sizeof buf, "%.1f", m);
  return buf;
}

void replace_all(std::string& s, const std::string& from, const std::string& to) {
  for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::string::npos; pos += to.size()) s.replace(pos, from.size(), to);
}

}  // namespace

const char* to_string(GameState s) {
  switch (s) {
    case GameState::None: return "none";
    case GameState::Ready: return "ready";
    case GameState::Running: return "running";
    case GameState::Paused: return "paused";
    case GameState::Ended: return "ended";
  }
  return "?";
}

std::optional<GameState> game_state_from_string(const std::string& s) {
  for (auto st : {GameState::None, GameState::Ready, GameState::Running, GameState::Paused, GameState::Ended})
    if (s == to_string(st)) return st;
  return std::nullopt;
}

void to_json(json& j, const TapRecord& r) {
  j = json{{"seq", r.seq},           {"t", r.t},           {"wall_ms", r.wall_ms},
           {"uid", r.uid},           {"player", r.player}, {"name", r.name},
           {"outcome", to_string(r.outcome)}, {"points", r.points}, {"balance", r.balance},
           {"multiplier", r.multiplier},      {"headline", r.headline}, {"detail", r.detail}};
}

Engine::Engine(const hal::Clock& clock, hal::Random& rng, hal::Outputs& out, const GameRegistry& games)
    : clock_(clock), rng_(rng), out_(out), games_(games) {}

GameContext Engine::ctx() { return GameContext{rng_, players_, scores_, events_, *this, now()}; }

void Engine::require_game() const {
  if (!game_) throw EngineError(409, "no game selected");
}

void Engine::broadcast(const json& msg) {
  if (listener_) listener_(msg);
}

void Engine::broadcast_state() { broadcast(json{{"type", "state"}, {"status", status_json()}}); }

void Engine::broadcast_scores_if_changed() {
  if (scores_.version() == broadcast_score_version_) return;
  broadcast_score_version_ = scores_.version();
  json msg = board_json(10);
  msg["type"] = "scores";
  broadcast(msg);
}

void Engine::show_idle() {
  hal::ScreenCmd cmd;
  cmd.kind = hal::ScreenKind::Idle;
  cmd.headline = (game_ && state_ != GameState::None) ? games_.find(game_->info().id)->name : "";
  switch (state_) {
    case GameState::Running: cmd.detail = "Tap card here"; break;
    case GameState::Paused: cmd.detail = "Paused"; break;
    case GameState::Ended: cmd.detail = "End of the line"; break;
    default: cmd.detail = "Not in service"; break;
  }
  const auto active = events_.active(now());
  if (state_ == GameState::Running && !active.empty()) cmd.detail = active.front().name;
  out_.screen(cmd);
}

std::optional<std::int64_t> Engine::wall_clock_ms() const {
  if (!wall_offset_) return std::nullopt;
  return now() + *wall_offset_;
}

// ---------------------------------------------------------------- taps

std::string Engine::fill(const std::string& tmpl, const TapRecord& r) const {
  std::string s = tmpl;
  replace_all(s, "{points}", std::to_string(r.points));
  replace_all(s, "{balance}", std::to_string(r.balance));
  replace_all(s, "{name}", r.name);
  replace_all(s, "{no}", std::to_string(r.player));
  replace_all(s, "{mult}", format_mult(r.multiplier));
  return s;
}

TapRecord Engine::tap(const std::string& uid_in) {
  auto uid = hal::CardUid::from_hex(uid_in);
  if (!uid) throw EngineError(400, "malformed card uid");

  TapRecord rec;
  rec.seq = ++tap_seq_;
  rec.t = now();
  rec.uid = uid->hex();
  if (auto w = wall_clock_ms()) rec.wall_ms = *w;

  Player* player = players_.find_uid(rec.uid);
  Feedback fb;
  if (state_ != GameState::Running) {
    fb.outcome = Outcome::Closed;
  } else if (!player && !open_enrollment_) {
    fb.outcome = Outcome::Unknown;
  } else {
    if (!player) {
      player = &players_.enroll(rec.uid);
      broadcast(json{{"type", "players"}});
    }
    if (player->banned) {
      fb.outcome = Outcome::Banned;
    } else {
      GameContext c = ctx();
      fb = game_->on_tap(c, *player);
    }
  }

  if (player) {
    rec.player = player->number;
    rec.name = player->name;
  }

  // Scoring: multipliers only boost positive awards.
  std::int64_t points = fb.points;
  if (fb.apply_multiplier && points > 0) {
    rec.multiplier = events_.multiplier(rec.t);
    if (rec.multiplier != 1.0) {
      points = std::llround(points * rec.multiplier);
      if (fb.outcome == Outcome::Accepted && rec.multiplier > 1.0) fb.outcome = Outcome::Bonus;
    }
  }
  rec.outcome = fb.outcome;
  rec.points = points;
  if (player) {
    scores_.add_points(player->number, points);
    if (fb.outcome != Outcome::Closed) scores_.record_tap(player->number, fb.outcome, rec.t);
    rec.balance = scores_.points(player->number);
  }

  const ThemeEntry theme = orca_theme(fb.outcome);
  // A game's own headline/detail wins, except that a Bonus upgrade keeps the bonus wording.
  const bool upgraded = fb.outcome == Outcome::Bonus && fb.headline.empty();
  rec.headline = fill(upgraded || fb.headline.empty() ? theme.headline : fb.headline, rec);
  rec.detail = fill(fb.detail.empty() ? theme.detail : fb.detail, rec);

  // Beep first: it's the cue people notice first.
  out_.sound({fb.sound.empty() ? theme.sound : fb.sound});
  out_.leds({fb.leds.empty() ? theme.leds : fb.leds, fb.hold_ms ? fb.hold_ms : theme.hold_ms});
  hal::ScreenCmd screen;
  screen.kind = hal::ScreenKind::Result;
  screen.tone = theme.tone;
  screen.headline = rec.headline;
  screen.detail = rec.detail;
  if (player) {
    char label[48];
    std::snprintf(label, sizeof label, "#%02d %s", player->number, player->name.c_str());
    screen.player_label = label;
  }
  screen.hold_ms = fb.hold_ms ? fb.hold_ms : theme.hold_ms;
  out_.screen(screen);

  recent_.push_front(rec);
  if (recent_.size() > kRecentTaps) recent_.pop_back();
  touch();
  broadcast(json{{"type", "tap"}, {"tap", rec}});
  broadcast_scores_if_changed();
  return rec;
}

// ---------------------------------------------------------------- time

void Engine::handle_event_changes(const EventScheduler::Changes& ch) {
  if (ch.empty()) return;
  GameContext c = ctx();
  for (const auto& w : ch.ended) {
    if (game_) game_->on_event(c, w, false);
    broadcast(json{{"type", "event"}, {"action", "end"}, {"event", w}});
  }
  for (const auto& w : ch.started) {
    char detail[64];
    if (w.kind == EventKind::Multiplier)
      std::snprintf(detail, sizeof detail, "%sx points for %s", format_mult(w.multiplier).c_str(),
                    format_mmss(w.end - w.start).c_str());
    else
      std::snprintf(detail, sizeof detail, "For %s", format_mmss(w.end - w.start).c_str());
    announce(w.name, detail, hal::Tone::Bonus, fx::kEventStartSound, fx::kEventLeds);
    if (game_) game_->on_event(c, w, true);
    broadcast(json{{"type", "event"}, {"action", "start"}, {"event", w}});
  }
  if (!ch.ended.empty() && events_.active(now()).empty()) {
    out_.sound({fx::kEventEndSound});
    out_.leds({fx::kIdleLeds, 0});
  }
  show_idle();
  touch();
}

void Engine::tick() {
  if (state_ != GameState::Running) return;
  handle_event_changes(events_.tick(now(), rng_));
  GameContext c = ctx();
  const auto before = scores_.version();
  game_->on_tick(c);
  if (scores_.version() != before) touch();
  broadcast_scores_if_changed();
}

// ---------------------------------------------------------------- admin

void Engine::select_game(const std::string& id, const json& params, bool keep_scores) {
  if (state_ == GameState::Running || state_ == GameState::Paused)
    throw EngineError(409, "stop the current game first");
  const GameInfo* info = games_.find(id);
  if (!info) throw EngineError(404, "unknown game: " + id);
  ParamResult pr = validate_params(info->params, params);
  if (!pr.ok) throw EngineError(400, pr.error);
  auto g = games_.create(id);
  g->configure(pr.params);
  game_ = std::move(g);
  events_.clear();
  if (!keep_scores) scores_.clear();
  state_ = GameState::Ready;
  touch();
  show_idle();
  broadcast_state();
  broadcast_scores_if_changed();
}

void Engine::start() {
  require_game();
  if (state_ != GameState::Ready) throw EngineError(409, std::string("cannot start from state ") + to_string(state_));
  state_ = GameState::Running;
  GameContext c = ctx();
  game_->start(c);
  out_.sound({fx::kStartSound});
  out_.leds({fx::kIdleLeds, 0});
  touch();
  show_idle();
  broadcast_state();
}

void Engine::pause() {
  if (state_ != GameState::Running) throw EngineError(409, "game is not running");
  state_ = GameState::Paused;
  touch();
  show_idle();
  broadcast_state();
}

void Engine::resume() {
  if (state_ != GameState::Paused) throw EngineError(409, "game is not paused");
  state_ = GameState::Running;
  touch();
  show_idle();
  broadcast_state();
}

void Engine::stop() {
  if (state_ != GameState::Running && state_ != GameState::Paused) throw EngineError(409, "game is not running");
  GameContext c = ctx();
  game_->stop(c);
  events_.clear();
  state_ = GameState::Ended;
  std::string detail = "Thanks for riding";
  const auto board = scores_.leaderboard(1);
  if (!board.empty()) {
    if (const Player* p = players_.find(board.front().number))
      detail = "Winner: " + p->name + " (" + std::to_string(board.front().points) + " pts)";
  }
  announce("END OF THE LINE", detail, hal::Tone::Neutral, fx::kEndSound, fx::kOffLeds);
  touch();
  show_idle();
  broadcast_state();
}

void Engine::reset() {
  require_game();
  if (state_ == GameState::Running || state_ == GameState::Paused) throw EngineError(409, "stop the game first");
  const json params = game_->params();
  auto g = games_.create(game_->info().id);
  g->configure(params);
  game_ = std::move(g);
  scores_.clear();
  events_.clear();
  state_ = GameState::Ready;
  touch();
  show_idle();
  broadcast_state();
  broadcast_scores_if_changed();
}

EventWindow Engine::trigger_event(EventKind kind, const std::string& name, TimeMs duration, double multiplier) {
  if (state_ != GameState::Running) throw EngineError(409, "game is not running");
  if (duration <= 0 || duration > hal::minutes(60)) throw EngineError(400, "duration must be 1 ms to 60 min");
  if (kind == EventKind::Multiplier && (multiplier <= 0 || multiplier > 10))
    throw EngineError(400, "multiplier must be between 0 and 10");
  EventWindow w = events_.trigger(kind, name.empty() ? "RUSH HOUR" : name, duration, multiplier, now());
  EventScheduler::Changes ch;
  ch.started.push_back(w);
  handle_event_changes(ch);
  return w;
}

void Engine::end_events() {
  events_.end_all(now());
  handle_event_changes(events_.tick(now(), rng_));
}

Player& Engine::enroll(const std::string& uid_in, const std::string& name) {
  auto uid = hal::CardUid::from_hex(uid_in);
  if (!uid) throw EngineError(400, "malformed card uid");
  if (name.size() > 24) throw EngineError(400, "name too long (max 24)");
  Player& p = players_.enroll(uid->hex(), name);
  touch();
  broadcast(json{{"type", "players"}});
  return p;
}

Player& Engine::update_player(int number, const json& patch) {
  Player* p = players_.find(number);
  if (!p) throw EngineError(404, "no such player");
  if (!patch.is_object()) throw EngineError(400, "patch must be an object");
  Player updated = *p;
  for (auto it = patch.begin(); it != patch.end(); ++it) {
    if (it.key() == "name") {
      if (!it->is_string() || it->get<std::string>().empty() || it->get<std::string>().size() > 24)
        throw EngineError(400, "name must be 1-24 characters");
      updated.name = it->get<std::string>();
    } else if (it.key() == "team") {
      if (!it->is_string() || it->get<std::string>().size() > 24) throw EngineError(400, "team must be 0-24 characters");
      updated.team = it->get<std::string>();
    } else if (it.key() == "banned") {
      if (!it->is_boolean()) throw EngineError(400, "banned must be true or false");
      updated.banned = it->get<bool>();
    } else {
      throw EngineError(400, "unknown field: " + it.key());
    }
  }
  *p = updated;
  touch();
  broadcast(json{{"type", "players"}});
  broadcast_scores_if_changed();
  return *p;
}

void Engine::adjust_score(int number, std::int64_t delta) {
  if (!players_.find(number)) throw EngineError(404, "no such player");
  if (delta < -1'000'000 || delta > 1'000'000) throw EngineError(400, "delta out of range");
  scores_.add_points(number, delta);
  touch();
  broadcast_scores_if_changed();
}

void Engine::set_open_enrollment(bool open) {
  open_enrollment_ = open;
  touch();
  broadcast_state();
}

void Engine::sync_wall_clock(std::int64_t epoch_ms) {
  if (epoch_ms < 1'600'000'000'000LL) throw EngineError(400, "epoch_ms looks wrong");
  wall_offset_ = epoch_ms - now();
}

void Engine::play_sound(const std::string& id) {
  if (id.empty() || id.size() > 32) throw EngineError(400, "bad sound id");
  out_.sound({id});
}

void Engine::announce(const std::string& headline, const std::string& detail, hal::Tone tone, const std::string& sound,
                      const std::string& leds) {
  if (!sound.empty()) out_.sound({sound});
  if (!leds.empty()) out_.leds({leds, fx::kBannerHoldMs});
  hal::ScreenCmd cmd;
  cmd.kind = hal::ScreenKind::Banner;
  cmd.tone = tone;
  cmd.headline = headline;
  cmd.detail = detail;
  cmd.hold_ms = fx::kBannerHoldMs;
  out_.screen(cmd);
  broadcast(json{{"type", "announce"}, {"headline", headline}, {"detail", detail}});
}

// ---------------------------------------------------------------- queries

json Engine::status_json() {
  const TimeMs t = now();
  json events = json::array();
  for (const auto& w : events_.active(t)) {
    json e = w;
    e["remaining_ms"] = w.end - t;
    events.push_back(e);
  }
  json game = nullptr;
  if (game_) {
    GameContext c = ctx();
    game = json{{"id", game_->info().id},
                {"name", game_->info().name},
                {"params", game_->params()},
                {"status", game_->status(c)}};
  }
  const auto next = events_.next_random_at();
  return json{{"state", to_string(state_)},
              {"game", game},
              {"players", players_.size()},
              {"open_enrollment", open_enrollment_},
              {"events", events},
              {"next_event_in_ms", next && state_ == GameState::Running ? json(*next - t) : json(nullptr)},
              {"taps", tap_seq_},
              {"total_points", scores_.total()},
              {"time_ms", t},
              {"wall_clock_ms", wall_clock_ms() ? json(*wall_clock_ms()) : json(nullptr)}};
}

json Engine::players_json() const {
  json arr = json::array();
  for (const auto& p : players_.all()) {
    json j = p;
    const auto s = scores_.stats(p.number);
    j["points"] = s.points;
    j["taps"] = s.taps;
    j["accepted"] = s.accepted;
    arr.push_back(j);
  }
  return arr;
}

json Engine::board_json(std::size_t limit) const {
  json rows = json::array();
  int rank = 0;
  for (const auto& row : scores_.leaderboard(limit)) {
    const Player* p = players_.find(row.number);
    rows.push_back({{"rank", ++rank},
                    {"no", row.number},
                    {"name", p ? p->name : "?"},
                    {"team", p ? p->team : ""},
                    {"points", row.points}});
  }
  json teams = json::array();
  for (const auto& [team, pts] : scores_.team_totals(players_)) teams.push_back({{"team", team}, {"points", pts}});
  return json{{"board", rows}, {"teams", teams}};
}

json Engine::games_json() const {
  json arr = json::array();
  for (const GameInfo* g : games_.all())
    arr.push_back({{"id", g->id}, {"name", g->name}, {"description", g->description}, {"params", schema_to_json(g->params)}});
  return arr;
}

json Engine::recent_taps_json(std::size_t limit) const {
  json arr = json::array();
  for (const auto& r : recent_) {
    if (arr.size() >= limit) break;
    arr.push_back(r);
  }
  return arr;
}

// ---------------------------------------------------------------- persistence

json Engine::snapshot() const {
  json game = nullptr;
  if (game_) game = json{{"id", game_->info().id}, {"params", game_->params()}, {"state", game_->save()}};
  return json{{"version", kSnapshotVersion},
              {"game_time", now()},
              {"state", to_string(state_)},
              {"open_enrollment", open_enrollment_},
              {"tap_seq", tap_seq_},
              {"game", game},
              {"players", players_.to_json()},
              {"scores", scores_.to_json()},
              {"events", events_.save()}};
}

void Engine::restore(const json& snap) {
  // Build everything in temporaries first so a bad snapshot changes nothing.
  std::unique_ptr<Game> game;
  PlayerRegistry players;
  ScoreBoard scores;
  EventScheduler events;
  GameState state;
  try {
    if (snap.at("version").get<int>() != kSnapshotVersion) throw EngineError(400, "unsupported snapshot version");
    auto st = game_state_from_string(snap.at("state").get<std::string>());
    if (!st) throw EngineError(400, "bad state in snapshot");
    state = *st;
    const json& g = snap.at("game");
    if (!g.is_null()) {
      const GameInfo* info = games_.find(g.at("id").get<std::string>());
      if (!info) throw EngineError(400, "snapshot game is not installed");
      ParamResult pr = validate_params(info->params, g.at("params"));
      if (!pr.ok) throw EngineError(400, "snapshot params: " + pr.error);
      game = games_.create(info->id);
      game->configure(pr.params);
      game->restore(g.at("state"));
    } else {
      state = GameState::None;
    }
    players.from_json(snap.at("players"));
    scores.from_json(snap.at("scores"));
    events.restore(snap.at("events"));
    offset_ = snap.at("game_time").get<TimeMs>() - clock_.now_ms();
    open_enrollment_ = snap.value("open_enrollment", true);
    tap_seq_ = snap.value("tap_seq", std::int64_t{0});
  } catch (const json::exception& e) {
    throw EngineError(400, std::string("bad snapshot: ") + e.what());
  }
  game_ = std::move(game);
  players_ = std::move(players);
  scores_ = std::move(scores);
  events_ = std::move(events);
  state_ = state;
  recent_.clear();
  if (wall_offset_) wall_offset_.reset();  // re-sync after restore; game time moved
  broadcast_score_version_ = scores_.version();
  touch();
  show_idle();
  if (state_ == GameState::Running) announce("SERVICE RESTORED", "Scores are safe", hal::Tone::Neutral, "", "");
  broadcast_state();
}

}  // namespace orca::game
