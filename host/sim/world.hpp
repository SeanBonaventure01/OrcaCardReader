#pragma once

// A complete costume minus hardware: engine, games, API and fake outputs, wired the same
// way as on the device. Used by the API tests, contract tests and orca-sim.

#include <functional>
#include <mutex>
#include <vector>

#include "orca/api/api.hpp"
#include "orca/fakes/recording_outputs.hpp"
#include "orca/game/engine.hpp"
#include "orca/games/registry.hpp"

namespace orca::sim {

using json = nlohmann::json;

// Runs API calls directly against the engine under a lock (the device posts them to game_task).
class DirectGateway : public api::EngineGateway {
 public:
  DirectGateway(game::Engine& e, std::recursive_mutex& m) : engine_(e), mutex_(m) {}
  json with_engine(const std::function<json(game::Engine&)>& fn) override {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return fn(engine_);
  }

 private:
  game::Engine& engine_;
  std::recursive_mutex& mutex_;
};

class MemorySettings : public api::SettingsStore {
 public:
  config::Settings get() override { return settings; }
  void set(const config::Settings& s) override { settings = s; }
  config::Settings settings;
};

struct World {
  World(const hal::Clock& clock, hal::Random& rng, api::ApiOptions options = {})
      : registry(make_registry()),
        engine(clock, rng, outputs, registry),
        gateway(engine, mutex),
        api(gateway, settings, clock, rng, std::move(options)) {
    engine.set_listener([this](const json& m) {
      for (auto& l : listeners) l(m);
    });
  }

  static game::GameRegistry make_registry() {
    game::GameRegistry r;
    games::register_builtin_games(r);
    return r;
  }

  // Convenience for tests: one call, optionally authenticated.
  api::ApiResponse call(const std::string& method, const std::string& path, const json& body = nullptr,
                        const std::string& token = "") {
    api::ApiRequest req;
    req.method = method;
    const auto q = path.find('?');
    req.path = path.substr(0, q);
    if (q != std::string::npos) req.query = path.substr(q + 1);
    if (!body.is_null()) req.body = body.dump();
    req.token = token;
    return api.handle(req);
  }

  std::string login(const std::string& pin = "1031") {
    auto r = call("POST", "/api/login", {{"pin", pin}});
    return r.status == 200 ? json::parse(r.body)["token"].get<std::string>() : "";
  }

  std::recursive_mutex mutex;
  fakes::RecordingOutputs outputs;
  game::GameRegistry registry;
  game::Engine engine;
  DirectGateway gateway;
  MemorySettings settings;
  api::Api api;
  std::vector<std::function<void(const json&)>> listeners;
};

}  // namespace orca::sim
