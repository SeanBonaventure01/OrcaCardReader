#include "orca/api/api.hpp"

#include <algorithm>
#include <cstdio>

namespace orca::api {

using game::EngineError;
using game::EventKind;

namespace {

ApiResponse json_response(int status, const json& body) { return {status, body.dump(), "application/json"}; }

ApiResponse error(int status, const std::string& message) { return json_response(status, json{{"error", message}}); }

std::vector<std::string> split_path(const std::string& path) {
  std::vector<std::string> out;
  std::size_t start = 0;
  while (start <= path.size()) {
    std::size_t end = path.find('/', start);
    if (end == std::string::npos) end = path.size();
    if (end > start) out.push_back(path.substr(start, end - start));
    start = end + 1;
  }
  return out;
}

bool parse_positive_int(const std::string& s, int& out) {
  if (s.empty() || s.size() > 6) return false;
  int v = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  if (v <= 0) return false;
  out = v;
  return true;
}

// --- body field helpers; all throw EngineError(400) on bad input

const json& field(const json& body, const char* key) {
  if (!body.contains(key)) throw EngineError(400, std::string(key) + " is required");
  return body.at(key);
}

std::string req_string(const json& body, const char* key, std::size_t max_len = 64) {
  const json& v = field(body, key);
  if (!v.is_string() || v.get<std::string>().empty() || v.get<std::string>().size() > max_len)
    throw EngineError(400, std::string(key) + " must be a non-empty string");
  return v.get<std::string>();
}

std::string opt_string(const json& body, const char* key, std::size_t max_len = 64) {
  if (!body.contains(key)) return "";
  const json& v = body.at(key);
  if (!v.is_string() || v.get<std::string>().size() > max_len) throw EngineError(400, std::string(key) + " must be a string");
  return v.get<std::string>();
}

std::int64_t req_int(const json& body, const char* key) {
  const json& v = field(body, key);
  if (!v.is_number_integer()) throw EngineError(400, std::string(key) + " must be an integer");
  return v.get<std::int64_t>();
}

bool req_bool(const json& body, const char* key) {
  const json& v = field(body, key);
  if (!v.is_boolean()) throw EngineError(400, std::string(key) + " must be true or false");
  return v.get<bool>();
}

void only_keys(const json& body, std::initializer_list<const char*> keys) {
  for (auto it = body.begin(); it != body.end(); ++it) {
    if (std::none_of(keys.begin(), keys.end(), [&](const char* k) { return it.key() == k; }))
      throw EngineError(400, "unknown field: " + it.key());
  }
}

std::size_t limit_param(const std::map<std::string, std::string>& q, std::size_t def, std::size_t max) {
  auto it = q.find("limit");
  if (it == q.end()) return def;
  int v = 0;
  if (!parse_positive_int(it->second, v) || static_cast<std::size_t>(v) > max)
    throw EngineError(400, "limit must be 1-" + std::to_string(max));
  return static_cast<std::size_t>(v);
}

json player_json(game::Engine& e, int number) {
  const game::Player* p = e.players().find(number);
  json j = *p;
  j["points"] = e.scores().points(number);
  return j;
}

}  // namespace

std::map<std::string, std::string> Api::parse_query(const std::string& query) {
  std::map<std::string, std::string> out;
  std::size_t start = 0;
  while (start < query.size()) {
    std::size_t end = query.find('&', start);
    if (end == std::string::npos) end = query.size();
    const std::string pair = query.substr(start, end - start);
    const std::size_t eq = pair.find('=');
    if (!pair.empty()) out[pair.substr(0, eq)] = eq == std::string::npos ? "" : pair.substr(eq + 1);
    start = end + 1;
  }
  return out;
}

Api::Api(EngineGateway& gateway, SettingsStore& settings) : engine_(gateway), settings_(settings) {
  using E = game::Engine;
  auto status = [this] { return engine([](E& e) { return e.status_json(); }); };

  add("GET", "/api/health", [](Ctx&) { return json_response(200, json{{"ok", true}}); });
  add("GET", "/api/status", [status](Ctx&) { return json_response(200, status()); });
  add("GET", "/api/board", [this](Ctx& c) {
    const auto limit = limit_param(c.query, 10, 100);
    return json_response(200, engine([&](E& e) { return e.board_json(limit); }));
  });
  add("GET", "/api/taps", [this](Ctx& c) {
    const auto limit = limit_param(c.query, 20, 100);
    return json_response(200, json{{"taps", engine([&](E& e) { return e.recent_taps_json(limit); })}});
  });
  add("GET", "/api/games", [this](Ctx&) {
    return json_response(200, json{{"games", engine([](E& e) { return e.games_json(); })}});
  });
  add("POST", "/api/game/select", [this](Ctx& c) {
    only_keys(c.body, {"id", "params", "keep_scores"});
    const std::string id = req_string(c.body, "id");
    const json params = c.body.value("params", json::object());
    if (!params.is_object()) throw EngineError(400, "params must be an object");
    const bool keep = c.body.contains("keep_scores") ? req_bool(c.body, "keep_scores") : false;
    return json_response(200, engine([&](E& e) {
                           e.select_game(id, params, keep);
                           return e.status_json();
                         }));
  });
  for (const char* action : {"start", "pause", "resume", "stop", "reset"}) {
    const std::string a = action;
    add("POST", "/api/game/" + a, [this, a](Ctx&) {
      return json_response(200, engine([&](E& e) {
                             if (a == "start") e.start();
                             else if (a == "pause") e.pause();
                             else if (a == "resume") e.resume();
                             else if (a == "stop") e.stop();
                             else e.reset();
                             return e.status_json();
                           }));
    });
  }
  add("POST", "/api/game/action", [this](Ctx& c) {
    only_keys(c.body, {"action", "args"});
    const std::string action = req_string(c.body, "action", 32);
    const json args = c.body.value("args", json::object());
    return json_response(200, engine([&](E& e) { return e.game_action(action, args); }));
  });
  add("GET", "/api/players", [this](Ctx&) {
    return json_response(200, json{{"players", engine([](E& e) { return e.players_json(); })}});
  });
  add("DELETE", "/api/players", [this](Ctx&) {
    return json_response(200, json{{"players", engine([](E& e) {
                                     e.clear_players();
                                     return e.players_json();
                                   })}});
  });
  add("POST", "/api/players", [this](Ctx& c) {
    only_keys(c.body, {"uid", "name"});
    const std::string uid = req_string(c.body, "uid", 32);
    const std::string name = opt_string(c.body, "name", 24);
    return json_response(201, engine([&](E& e) { return player_json(e, e.enroll(uid, name).number); }));
  });
  add("PATCH", "/api/players/{no}", [this](Ctx& c) {
    return json_response(200, engine([&](E& e) { return player_json(e, e.update_player(c.number, c.body).number); }));
  });
  add("POST", "/api/players/{no}/adjust", [this](Ctx& c) {
    only_keys(c.body, {"delta"});
    const auto delta = req_int(c.body, "delta");
    return json_response(200, engine([&](E& e) {
                           e.adjust_score(c.number, delta);
                           return player_json(e, c.number);
                         }));
  });
  add("POST", "/api/events/trigger", [this](Ctx& c) {
    only_keys(c.body, {"kind", "name", "duration_ms", "multiplier"});
    const std::string kind_s = c.body.contains("kind") ? req_string(c.body, "kind") : "multiplier";
    const auto kind = game::event_kind_from_string(kind_s);
    if (!kind) throw EngineError(400, "kind must be multiplier, cooldown_waiver or custom");
    const std::string name = opt_string(c.body, "name", 24);
    const TimeMs duration = req_int(c.body, "duration_ms");
    double mult = 2.0;
    if (c.body.contains("multiplier")) {
      if (!c.body.at("multiplier").is_number()) throw EngineError(400, "multiplier must be a number");
      mult = c.body.at("multiplier").get<double>();
    }
    return json_response(200, engine([&](E& e) { return json(e.trigger_event(*kind, name, duration, mult)); }));
  });
  add("POST", "/api/events/end", [this](Ctx&) {
    return json_response(200, engine([](E& e) {
                           e.end_events();
                           return e.status_json();
                         }));
  });
  add("POST", "/api/sim/tap", [this](Ctx& c) {
    only_keys(c.body, {"uid", "player", "card"});
    if (c.body.contains("uid") == c.body.contains("player")) throw EngineError(400, "give exactly one of uid or player");
    // "card": the player number stored on the simulated card, as if it had been programmed.
    std::optional<int> card;
    if (c.body.contains("card")) card = static_cast<int>(req_int(c.body, "card"));
    return json_response(200, engine([&](E& e) {
                           std::string uid;
                           if (c.body.contains("uid")) {
                             uid = req_string(c.body, "uid", 32);
                           } else {
                             const game::Player* p = e.players().find(static_cast<int>(req_int(c.body, "player")));
                             if (!p) throw EngineError(404, "no such player");
                             uid = p->uid;
                           }
                           if (!e.provisioning().active) return json(e.tap(uid, card));
                           // A simulated card always takes the write.
                           if (auto n = e.card_seen(uid, card)) e.card_written(uid, *n, game::WriteResult::Ok);
                           return json{{"provision", *e.provisioning().last}};
                         }));
  });
  add("POST", "/api/provision", [this](Ctx& c) {
    only_keys(c.body, {"start", "overwrite"});
    int start = 0;
    if (c.body.contains("start")) {
      const auto v = req_int(c.body, "start");
      if (v < 1 || v > 9999) throw EngineError(400, "start must be between 1 and 9999");
      start = static_cast<int>(v);
    }
    const bool overwrite = c.body.contains("overwrite") && req_bool(c.body, "overwrite");
    return json_response(200, engine([&](E& e) {
                           e.start_provisioning(start, overwrite);
                           return e.provisioning_json();
                         }));
  });
  add("POST", "/api/provision/stop", [this](Ctx&) {
    return json_response(200, engine([&](E& e) {
                           e.stop_provisioning();
                           return e.provisioning_json();
                         }));
  });
  add("POST", "/api/enrollment", [this](Ctx& c) {
    only_keys(c.body, {"open"});
    const bool open = req_bool(c.body, "open");
    return json_response(200, engine([&](E& e) {
                           e.set_open_enrollment(open);
                           return e.status_json();
                         }));
  });
  add("POST", "/api/clock", [this](Ctx& c) {
    only_keys(c.body, {"epoch_ms", "utc_offset_min"});
    const auto epoch = req_int(c.body, "epoch_ms");
    const auto offset = c.body.contains("utc_offset_min") ? req_int(c.body, "utc_offset_min") : 0;
    engine([&](E& e) {
      e.sync_wall_clock(epoch, static_cast<int>(offset));
      return json();
    });
    return json_response(200, json{{"ok", true}});
  });
  add("POST", "/api/fx/sound", [this](Ctx& c) {
    only_keys(c.body, {"id"});
    const std::string id = req_string(c.body, "id", 32);
    engine([&](E& e) {
      e.play_sound(id);
      return json();
    });
    return json_response(200, json{{"ok", true}});
  });
  add("GET", "/api/settings", [this](Ctx&) { return json_response(200, config::to_json(settings_.get())); });
  add("PATCH", "/api/settings", [this](Ctx& c) {
    config::PatchResult r = config::apply_patch(settings_.get(), c.body);
    if (!r.ok) throw EngineError(400, r.error);
    settings_.set(r.settings);
    return json_response(200, config::to_json(r.settings));
  });
  add("GET", "/api/snapshot", [this](Ctx&) {
    return json_response(200, engine([](E& e) { return e.snapshot(); }));
  });
}

void Api::add(const std::string& method, const std::string& pattern, Handler h) {
  routes_.push_back({method, split_path(pattern), std::move(h)});
}

json Api::hello() {
  return engine([](game::Engine& e) {
    return json{{"type", "hello"}, {"status", e.status_json()}, {"board", e.board_json(10)}};
  });
}

ApiResponse Api::dispatch(const ApiRequest& req) {
  const auto segs = split_path(req.path);
  bool path_matched = false;
  for (const auto& r : routes_) {
    if (r.segments.size() != segs.size()) continue;
    int number = 0;
    bool match = true;
    for (std::size_t i = 0; i < segs.size() && match; ++i) {
      if (r.segments[i] == "{no}")
        match = parse_positive_int(segs[i], number);
      else
        match = r.segments[i] == segs[i];
    }
    if (!match) continue;
    path_matched = true;
    if (r.method != req.method) continue;

    Ctx c{req, json::object(), parse_query(req.query), number};
    if (!req.body.empty()) {
      c.body = json::parse(req.body, nullptr, /*allow_exceptions=*/false);
      if (c.body.is_discarded()) return error(400, "invalid JSON body");
    }
    if (!c.body.is_object()) return error(400, "body must be a JSON object");
    return r.handler(c);
  }
  return path_matched ? error(405, "method not allowed") : error(404, "not found");
}

ApiResponse Api::handle(const ApiRequest& req) {
  try {
    return dispatch(req);
  } catch (const EngineError& e) {
    return error(e.status(), e.what());
  } catch (const std::exception& e) {
    return error(500, std::string("internal error: ") + e.what());
  } catch (...) {
    return error(500, "internal error");
  }
}

}  // namespace orca::api
