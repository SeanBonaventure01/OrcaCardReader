#pragma once

// Test fixture that wires an Engine to fakes, so game tests read like the rules:
//   add_player("Ghost"); start("fare_paid"); tap("Ghost"); advance(5min); EXPECT_EQ(...)

#include <gtest/gtest.h>

#include <cstdio>
#include <map>
#include <string>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/recording_outputs.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "orca/game/engine.hpp"
#include "orca/games/registry.hpp"

namespace orca::test {

using game::json;
using game::Outcome;
using game::TimeMs;
using hal::minutes;
using hal::seconds;

class GameHarness : public ::testing::Test {
 protected:
  GameHarness() : rng(42), engine(clock, rng, out, registry_with_games()) {
    engine.set_listener([this](const json& m) { messages.push_back(m); });
  }

  static const game::GameRegistry& registry_with_games() {
    static game::GameRegistry reg = [] {
      game::GameRegistry r;
      games::register_builtin_games(r);
      return r;
    }();
    return reg;
  }

  // Deterministic UID per name: 04 00 00 00 00 00 <n>
  std::string uid_for(const std::string& name) {
    auto it = uids.find(name);
    if (it != uids.end()) return it->second;
    char buf[16];
    std::snprintf(buf, sizeof buf, "040000000000%02X", static_cast<unsigned>(uids.size() + 1));
    return uids[name] = buf;
  }

  game::Player& add_player(const std::string& name, const std::string& team = "") {
    game::Player& p = engine.enroll(uid_for(name), name);
    if (!team.empty()) engine.update_player(p.number, json{{"team", team}});
    return *engine.players().find(p.number);
  }

  void start(const std::string& game_id, const json& params = json::object()) {
    engine.select_game(game_id, params);
    engine.start();
  }

  game::TapRecord tap(const std::string& name) { return engine.tap(uid_for(name)); }

  // Moves time forward, ticking the engine every `step` like game_task does.
  void advance(TimeMs ms, TimeMs step = 100) {
    while (ms > 0) {
      const TimeMs d = ms < step ? ms : step;
      clock.advance(d);
      engine.tick();
      ms -= d;
    }
  }

  std::int64_t points(const std::string& name) {
    const game::Player* p = engine.players().find_uid(uid_for(name));
    return p ? engine.scores().points(p->number) : 0;
  }

  int count_messages(const std::string& type) const {
    int n = 0;
    for (const auto& m : messages) n += m.at("type") == type;
    return n;
  }

  fakes::FakeClock clock{1000};
  fakes::SeededRandom rng;
  fakes::RecordingOutputs out;
  game::Engine engine;
  std::map<std::string, std::string> uids;
  std::vector<json> messages;
};

}  // namespace orca::test
