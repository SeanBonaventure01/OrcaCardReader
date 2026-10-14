#include "scenario.hpp"

#include <cstdio>
#include <map>
#include <memory>
#include <sstream>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "orca/game/types.hpp"
#include "world.hpp"

namespace orca::sim {

using json = nlohmann::json;
using game::TimeMs;

namespace {

constexpr TimeMs kTickMs = 100;

class Runner {
 public:
  explicit Runner(const json& spec) : spec_(spec), rng_(spec.value("seed", 1)) {}

  ScenarioResult run() {
    try {
      boot();
      setup();
      for (const auto& step : spec_.value("steps", json::array())) do_step(step);
    } catch (const std::exception& e) {
      fail(std::string("scenario error: ") + e.what());
    }
    drain_outputs();
    return std::move(result_);
  }

 private:
  void boot(const json* snapshot = nullptr) {
    clock_ = std::make_unique<fakes::FakeClock>(0);
    world_ = std::make_unique<World>(*clock_, rng_);
    if (snapshot) world_->engine.restore(*snapshot);
    token_ = world_->login();
    if (token_.empty()) throw std::runtime_error("login failed");
  }

  json call(const std::string& method, const std::string& path, const json& body = nullptr) {
    auto r = world_->call(method, path, body, token_);
    json out = json::parse(r.body, nullptr, false);
    if (r.status >= 300) last_error_ = std::to_string(r.status) + " " + r.body;
    out["_status"] = r.status;
    return out;
  }

  void must(const std::string& method, const std::string& path, const json& body = nullptr) {
    json r = call(method, path, body);
    if (r["_status"].get<int>() >= 300) throw std::runtime_error(method + " " + path + " -> " + last_error_);
  }

  void setup() {
    int n = 0;
    for (const auto& p : spec_.value("players", json::array())) {
      ++n;
      const std::string name = p.at("name");
      char uid[16];
      std::snprintf(uid, sizeof uid, "040000000000%02X", n);
      const std::string u = p.value("uid", std::string(uid));
      uids_[name] = u;
      must("POST", "/api/players", {{"uid", u}, {"name", name}});
      if (p.contains("team")) must("PATCH", "/api/players/" + std::to_string(n), {{"team", p.at("team")}});
    }
    if (spec_.contains("open_enrollment")) must("POST", "/api/enrollment", {{"open", spec_.at("open_enrollment")}});
    if (spec_.contains("game")) {
      must("POST", "/api/game/select", {{"id", spec_.at("game")}, {"params", spec_.value("params", json::object())}});
      must("POST", "/api/game/start");
    }
    drain_outputs();
  }

  void advance_to(TimeMs at) {
    if (at < elapsed_) throw std::runtime_error("step times must not go backwards");
    while (elapsed_ < at) {
      const TimeMs d = std::min(kTickMs, at - elapsed_);
      clock_->advance(d);
      elapsed_ += d;
      world_->engine.tick();
    }
    drain_outputs();
  }

  void do_step(const json& step) {
    const TimeMs at = game::parse_duration(step.value("at", "0s"));
    if (at < 0) throw std::runtime_error("bad 'at': " + step.dump());
    advance_to(at);

    if (step.contains("tap")) {
      const std::string who = step.at("tap");
      const std::string uid = uids_.count(who) ? uids_[who] : who;
      json r = call("POST", "/api/sim/tap", {{"uid", uid}});
      r.erase("wall_ms");
      if (r["_status"] == 200) r.erase("_status");
      drain_outputs();
      result_.transcript.push_back({{"t", elapsed_}, {"tap", who}, {"result", r}});
      last_tap_ = r;
    }
    if (step.contains("api")) {
      const json& a = step.at("api");
      json r = call(a.at(0), a.at(1), a.size() > 2 ? a.at(2) : json(nullptr));
      drain_outputs();
      result_.transcript.push_back({{"t", elapsed_}, {"api", {a.at(0), a.at(1)}}, {"status", r["_status"]}});
    }
    if (step.value("reboot", false)) {
      const json snap = world_->engine.snapshot();
      drain_outputs();
      boot(&snap);
      result_.transcript.push_back({{"t", elapsed_}, {"reboot", true}});
      drain_outputs();
    }
    if (step.contains("expect")) check(step.at("expect"));
  }

  void check(const json& expect) {
    bool ok = true;
    auto mismatch = [&](const std::string& what, const json& want, const json& got) {
      ok = false;
      fail("at " + std::to_string(elapsed_) + " ms: expected " + what + " = " + want.dump() + ", got " + got.dump());
    };
    if (expect.contains("score")) {
      for (auto it = expect.at("score").begin(); it != expect.at("score").end(); ++it) {
        const auto uid = uids_.count(it.key()) ? uids_[it.key()] : it.key();
        const game::Player* p = world_->engine.players().find_uid(uid);
        const json got = p ? json(world_->engine.scores().points(p->number)) : json(nullptr);
        if (got != *it) mismatch("score of " + it.key(), *it, got);
      }
    }
    if (expect.contains("state")) {
      const json got = game::to_string(world_->engine.state());
      if (got != expect.at("state")) mismatch("state", expect.at("state"), got);
    }
    if (expect.contains("players")) {
      const json got = world_->engine.players().size();
      if (got != expect.at("players")) mismatch("players", expect.at("players"), got);
    }
    if (expect.contains("outcome")) {
      const json got = last_tap_.value("outcome", json(nullptr));
      if (got != expect.at("outcome")) mismatch("last tap outcome", expect.at("outcome"), got);
    }
    if (expect.contains("events_active")) {
      const json got = world_->engine.events().active(world_->engine.now()).size();
      if (got != expect.at("events_active")) mismatch("active events", expect.at("events_active"), got);
    }
    result_.transcript.push_back({{"t", elapsed_}, {"expect", expect}, {"ok", ok}});
  }

  void drain_outputs() {
    if (!world_) return;
    for (auto& e : world_->outputs.log) result_.transcript.push_back({{"t", elapsed_}, {"out", e}});
    world_->outputs.log.clear();
  }

  void fail(const std::string& msg) { result_.failures.push_back(msg); }

  json spec_;
  fakes::SeededRandom rng_;
  std::unique_ptr<fakes::FakeClock> clock_;
  std::unique_ptr<World> world_;
  std::string token_;
  std::string last_error_;
  std::map<std::string, std::string> uids_;
  TimeMs elapsed_ = 0;
  json last_tap_ = json::object();
  ScenarioResult result_;
};

}  // namespace

ScenarioResult run_scenario(const json& spec) { return Runner(spec).run(); }

std::string to_jsonl(const std::vector<json>& lines) {
  std::string out;
  for (const auto& l : lines) out += l.dump() + "\n";
  return out;
}

std::vector<json> from_jsonl(const std::string& text) {
  std::vector<json> out;
  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) out.push_back(json::parse(line));
  return out;
}

}  // namespace orca::sim
