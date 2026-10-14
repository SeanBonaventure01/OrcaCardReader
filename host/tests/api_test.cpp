// API behaviour not covered by the shared contract fixtures: auth, sessions, routing edges.
#include <gtest/gtest.h>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "world.hpp"

using orca::api::Api;
using orca::hal::minutes;
using orca::hal::seconds;
using json = nlohmann::json;

class ApiTest : public ::testing::Test {
 protected:
  orca::fakes::FakeClock clock{1000};
  orca::fakes::SeededRandom rng{5};
  orca::sim::World world{clock, rng};

  json body(const orca::api::ApiResponse& r) { return json::parse(r.body); }
};

TEST_F(ApiTest, PublicEndpointsNeedNoLogin) {
  EXPECT_EQ(world.call("GET", "/api/health").status, 200);
  EXPECT_EQ(world.call("GET", "/api/status").status, 200);
  EXPECT_EQ(world.call("GET", "/api/board").status, 200);
  EXPECT_EQ(world.call("GET", "/api/taps").status, 200);
}

TEST_F(ApiTest, EveryAdminRouteRejectsAnonymousAndBadTokens) {
  const std::vector<std::pair<const char*, const char*>> admin = {
      {"POST", "/api/logout"},          {"GET", "/api/games"},         {"POST", "/api/game/select"},
      {"POST", "/api/game/start"},      {"POST", "/api/game/pause"},   {"POST", "/api/game/resume"},
      {"POST", "/api/game/stop"},       {"POST", "/api/game/reset"},   {"GET", "/api/players"},
      {"POST", "/api/players"},         {"PATCH", "/api/players/1"},   {"POST", "/api/players/1/adjust"},
      {"POST", "/api/events/trigger"},  {"POST", "/api/events/end"},   {"POST", "/api/sim/tap"},
      {"POST", "/api/enrollment"},      {"POST", "/api/clock"},        {"POST", "/api/fx/sound"},
      {"GET", "/api/settings"},         {"PATCH", "/api/settings"},    {"GET", "/api/snapshot"},
  };
  for (const auto& [method, path] : admin) {
    EXPECT_EQ(world.call(method, path, json::object()).status, 401) << method << " " << path;
    EXPECT_EQ(world.call(method, path, json::object(), "deadbeef").status, 401) << method << " " << path;
  }
}

TEST_F(ApiTest, LoginGivesWorkingTokenAndLogoutRevokesIt) {
  const std::string t = world.login();
  ASSERT_EQ(t.size(), 32u);
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, t).status, 200);
  EXPECT_EQ(world.call("POST", "/api/logout", nullptr, t).status, 200);
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, t).status, 401);
}

TEST_F(ApiTest, SessionsExpire) {
  const std::string t = world.login();
  clock.advance(minutes(12 * 60) - 1);
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, t).status, 200);
  clock.advance(1);
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, t).status, 401);
}

TEST_F(ApiTest, OldestSessionIsEvictedWhenFull) {
  const std::string first = world.login();
  clock.advance(1);
  std::vector<std::string> later;
  for (int i = 0; i < 16; ++i) {
    later.push_back(world.login());
    clock.advance(1);
  }
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, first).status, 401);
  EXPECT_EQ(world.call("GET", "/api/games", nullptr, later.back()).status, 200);
}

TEST_F(ApiTest, RepeatedWrongPinsLockLoginTemporarily) {
  for (int i = 0; i < 4; ++i) EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "0000"}}).status, 401);
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "0000"}}).status, 401);  // 5th: triggers lockout
  auto locked = world.call("POST", "/api/login", {{"pin", "1031"}});
  EXPECT_EQ(locked.status, 429);
  EXPECT_NE(body(locked)["error"].get<std::string>().find("60 s"), std::string::npos);
  clock.advance(seconds(60));
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "1031"}}).status, 200);
}

TEST_F(ApiTest, LoginValidation) {
  EXPECT_EQ(world.call("POST", "/api/login", json::object()).status, 400);
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", 1031}}).status, 400);
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "1031"}, {"x", 1}}).status, 400);
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "10310"}}).status, 401);
}

TEST_F(ApiTest, RoutingErrors) {
  const std::string t = world.login();
  EXPECT_EQ(world.call("GET", "/api/nope").status, 404);
  EXPECT_EQ(world.call("GET", "/").status, 404);
  EXPECT_EQ(world.call("DELETE", "/api/status").status, 405);
  EXPECT_EQ(world.call("GET", "/api/login").status, 405);
  EXPECT_EQ(world.call("PATCH", "/api/players/0", json::object(), t).status, 404);   // {no} must be positive
  EXPECT_EQ(world.call("PATCH", "/api/players/abc", json::object(), t).status, 404);
  EXPECT_EQ(world.call("PATCH", "/api/players/7", {{"name", "x"}}, t).status, 404);  // no such player
}

TEST_F(ApiTest, BodyMustBeAJsonObject) {
  const std::string t = world.login();
  orca::api::ApiRequest req{"POST", "/api/game/select", "", "{not json", t};
  auto r = world.api.handle(req);
  EXPECT_EQ(r.status, 400);
  EXPECT_EQ(body(r)["error"], "invalid JSON body");
  req.body = "[1,2]";
  EXPECT_EQ(world.api.handle(req).body, R"({"error":"body must be a JSON object"})");
}

TEST_F(ApiTest, QueryLimits) {
  EXPECT_EQ(world.call("GET", "/api/board?limit=5").status, 200);
  EXPECT_EQ(world.call("GET", "/api/board?limit=0").status, 400);
  EXPECT_EQ(world.call("GET", "/api/board?limit=101").status, 400);
  EXPECT_EQ(world.call("GET", "/api/taps?limit=x").status, 400);
}

TEST_F(ApiTest, ParseQuery) {
  auto q = Api::parse_query("a=1&b=&c&=z&d=x=y");
  EXPECT_EQ(q["a"], "1");
  EXPECT_EQ(q["b"], "");
  EXPECT_EQ(q.count("c"), 1u);
  EXPECT_EQ(q["d"], "x=y");
  EXPECT_TRUE(Api::parse_query("").empty());
}

TEST_F(ApiTest, SimTapByUidOrPlayer) {
  const std::string t = world.login();
  world.call("POST", "/api/game/select", {{"id", "fare_paid"}, {"params", {{"cooldown", 0}}}}, t);
  world.call("POST", "/api/game/start", nullptr, t);
  auto r = world.call("POST", "/api/sim/tap", {{"uid", "04AABBCC"}}, t);
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(body(r)["player"], 1);
  EXPECT_EQ(body(world.call("POST", "/api/sim/tap", {{"player", 1}}, t))["outcome"], "accepted");
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"player", 9}}, t).status, 404);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", json::object(), t).status, 400);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"uid", "04AABBCC"}, {"player", 1}}, t).status, 400);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"uid", "zz"}}, t).status, 400);
}

TEST_F(ApiTest, SettingsRoundTrip) {
  const std::string t = world.login();
  EXPECT_EQ(body(world.call("GET", "/api/settings", nullptr, t))["volume"], 70);
  auto r = world.call("PATCH", "/api/settings", {{"volume", 20}}, t);
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(world.settings.settings.volume, 20);
  EXPECT_EQ(world.call("PATCH", "/api/settings", {{"volume", 200}}, t).status, 400);
  EXPECT_EQ(world.settings.settings.volume, 20);
}

TEST_F(ApiTest, ClockAndSoundAndEnrollment) {
  const std::string t = world.login();
  EXPECT_EQ(world.call("POST", "/api/clock", {{"epoch_ms", 1'761'900'000'000}}, t).status, 200);
  EXPECT_EQ(world.call("POST", "/api/clock", {{"epoch_ms", "now"}}, t).status, 400);
  EXPECT_EQ(world.call("POST", "/api/fx/sound", {{"id", "spooky"}}, t).status, 200);
  EXPECT_EQ(world.outputs.sounds.back(), "spooky");
  auto r = world.call("POST", "/api/enrollment", {{"open", false}}, t);
  EXPECT_EQ(body(r)["open_enrollment"], false);
}

TEST_F(ApiTest, AdjustAndEnrollPlayers) {
  const std::string t = world.login();
  auto r = world.call("POST", "/api/players", {{"uid", "04:aa:bb:cc"}, {"name", "Ghost"}}, t);
  EXPECT_EQ(r.status, 201);
  EXPECT_EQ(body(r)["uid"], "04AABBCC");
  EXPECT_EQ(world.call("POST", "/api/players", {{"uid", "04AABBCC"}}, t).status, 409);
  r = world.call("POST", "/api/players/1/adjust", {{"delta", 5}}, t);
  EXPECT_EQ(body(r)["points"], 5);
  EXPECT_EQ(world.call("POST", "/api/players/1/adjust", {{"delta", 1.5}}, t).status, 400);
}

TEST_F(ApiTest, HelloHasStatusAndBoard) {
  auto h = world.api.hello();
  EXPECT_EQ(h["type"], "hello");
  EXPECT_EQ(h["status"]["state"], "none");
  EXPECT_TRUE(h["board"]["board"].is_array());
}
