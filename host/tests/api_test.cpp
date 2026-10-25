// API behaviour not covered by the shared contract fixtures: auth, sessions, routing edges.
#include <gtest/gtest.h>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "world.hpp"

using orca::api::Api;
using json = nlohmann::json;

class ApiTest : public ::testing::Test {
 protected:
  orca::fakes::FakeClock clock{1000};
  orca::fakes::SeededRandom rng{5};
  orca::sim::World world{clock, rng};

  json body(const orca::api::ApiResponse& r) { return json::parse(r.body); }
};

TEST_F(ApiTest, EveryRouteWorksWithoutLogin) {
  // No PIN: the costume is only reachable from the operator's hotspot.
  EXPECT_EQ(body(world.call("GET", "/api/health")), json({{"ok", true}}));
  EXPECT_EQ(world.call("GET", "/api/status").status, 200);
  EXPECT_EQ(world.call("GET", "/api/games").status, 200);
  EXPECT_EQ(world.call("POST", "/api/game/select", {{"id", "fare_paid"}}).status, 200);
  EXPECT_EQ(world.call("GET", "/api/settings").status, 200);
  EXPECT_EQ(world.call("POST", "/api/login", {{"pin", "1031"}}).status, 404);  // gone
}

TEST_F(ApiTest, RoutingErrors) {
  EXPECT_EQ(world.call("GET", "/api/nope").status, 404);
  EXPECT_EQ(world.call("GET", "/").status, 404);
  EXPECT_EQ(world.call("DELETE", "/api/status").status, 405);
  EXPECT_EQ(world.call("PATCH", "/api/players/0", json::object()).status, 404);   // {no} must be positive
  EXPECT_EQ(world.call("PATCH", "/api/players/abc", json::object()).status, 404);
  EXPECT_EQ(world.call("PATCH", "/api/players/7", {{"name", "x"}}).status, 404);  // no such player
}

TEST_F(ApiTest, BodyMustBeAJsonObject) {
  orca::api::ApiRequest req{"POST", "/api/game/select", "", "{not json"};
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
  world.call("POST", "/api/game/select", {{"id", "fare_paid"}, {"params", {{"cooldown", 0}}}});
  world.call("POST", "/api/game/start", nullptr);
  auto r = world.call("POST", "/api/sim/tap", {{"uid", "04AABBCC"}});
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(body(r)["player"], 1);
  EXPECT_EQ(body(world.call("POST", "/api/sim/tap", {{"player", 1}}))["outcome"], "accepted");
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"player", 9}}).status, 404);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", json::object()).status, 400);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"uid", "04AABBCC"}, {"player", 1}}).status, 400);
  EXPECT_EQ(world.call("POST", "/api/sim/tap", {{"uid", "zz"}}).status, 400);
}

TEST_F(ApiTest, SettingsRoundTrip) {
  EXPECT_EQ(body(world.call("GET", "/api/settings", nullptr))["volume"], 70);
  auto r = world.call("PATCH", "/api/settings", {{"volume", 20}});
  EXPECT_EQ(r.status, 200);
  EXPECT_EQ(world.settings.settings.volume, 20);
  EXPECT_EQ(world.call("PATCH", "/api/settings", {{"volume", 200}}).status, 400);
  EXPECT_EQ(world.settings.settings.volume, 20);
}

TEST_F(ApiTest, ClockAndSoundAndEnrollment) {
  EXPECT_EQ(world.call("POST", "/api/clock", {{"epoch_ms", 1'761'900'000'000}}).status, 200);
  EXPECT_EQ(world.call("POST", "/api/clock", {{"epoch_ms", "now"}}).status, 400);
  EXPECT_EQ(world.call("POST", "/api/fx/sound", {{"id", "spooky"}}).status, 200);
  EXPECT_EQ(world.outputs.sounds.back(), "spooky");
  auto r = world.call("POST", "/api/enrollment", {{"open", false}});
  EXPECT_EQ(body(r)["open_enrollment"], false);
}

TEST_F(ApiTest, AdjustAndEnrollPlayers) {
  auto r = world.call("POST", "/api/players", {{"uid", "04:aa:bb:cc"}, {"name", "Ghost"}});
  EXPECT_EQ(r.status, 201);
  EXPECT_EQ(body(r)["uid"], "04AABBCC");
  EXPECT_EQ(world.call("POST", "/api/players", {{"uid", "04AABBCC"}}).status, 409);
  r = world.call("POST", "/api/players/1/adjust", {{"delta", 5}});
  EXPECT_EQ(body(r)["points"], 5);
  EXPECT_EQ(world.call("POST", "/api/players/1/adjust", {{"delta", 1.5}}).status, 400);
}

TEST_F(ApiTest, HelloHasStatusAndBoard) {
  auto h = world.api.hello();
  EXPECT_EQ(h["type"], "hello");
  EXPECT_EQ(h["status"]["state"], "none");
  EXPECT_TRUE(h["board"]["board"].is_array());
}
