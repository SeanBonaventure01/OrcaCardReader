// orca-sim: the costume's engine, games and API on a laptop, with simulated hardware.
//
//   orca-sim run <file.scenario.json> [--update-golden]   run a scenario, compare/update golden
//   orca-sim repl [--game <id>]                           interactive console on a fake clock
//   orca-sim serve [--port 8080] [--speed 1] [--web web/dist] [--state sim-state.json] [--seed N]
//                                                         real API + WebSocket on localhost

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "orca/game/snapshot_store.hpp"
#include "orca/hal/posix_file_system.hpp"
#include "scenario.hpp"
#include "sim_server.hpp"
#include "world.hpp"

using namespace orca;
using json = nlohmann::json;

namespace {

std::string read_file(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::string flag(int argc, char** argv, const std::string& name, const std::string& def) {
  for (int i = 2; i + 1 < argc; ++i)
    if (argv[i] == name) return argv[i + 1];
  return def;
}

bool has_flag(int argc, char** argv, const std::string& name) {
  for (int i = 2; i < argc; ++i)
    if (argv[i] == name) return true;
  return false;
}

// Prints one output command the way the costume would show it.
void print_output(const json& o, std::ostream& os) {
  const std::string kind = o.value("out", "");
  if (kind == "screen") {
    os << "  [screen:" << o.value("kind", "") << "/" << o.value("tone", "") << "] " << o.value("headline", "");
    if (!o.value("detail", "").empty()) os << " | " << o.value("detail", "");
    if (!o.value("player", "").empty()) os << " | " << o.value("player", "");
    os << "\n";
  } else if (kind == "sound") {
    os << "  [sound] " << o.value("id", "") << "\n";
  } else if (kind == "leds") {
    os << "  [leds] " << o.value("effect", "") << "\n";
  }
}

// ---------------------------------------------------------------- run

int cmd_run(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: orca-sim run <file.scenario.json> [--update-golden]\n";
    return 2;
  }
  const std::string path = argv[2];
  const auto result = sim::run_scenario(json::parse(read_file(path)));
  const std::string jsonl = sim::to_jsonl(result.transcript);
  const std::string suffix = ".scenario.json";
  const std::string golden =
      path.size() > suffix.size() ? path.substr(0, path.size() - suffix.size()) + ".expected.jsonl" : path + ".expected.jsonl";

  for (const auto& f : result.failures) std::cerr << "FAIL: " << f << "\n";
  if (has_flag(argc, argv, "--update-golden")) {
    std::ofstream(golden) << jsonl;
    std::cerr << "wrote " << golden << " (" << result.transcript.size() << " lines)\n";
  } else {
    std::cout << jsonl;
  }
  return result.failures.empty() ? 0 : 1;
}

// ---------------------------------------------------------------- repl

int cmd_repl(int argc, char** argv) {
  fakes::FakeClock clock(0);
  fakes::SeededRandom rng(static_cast<std::uint64_t>(std::stoull(flag(argc, argv, "--seed", "1"))));
  sim::World world(clock, rng);
  const std::string token = world.login();
  auto call = [&](const std::string& m, const std::string& p, const json& body = nullptr) {
    auto r = world.call(m, p, body, token);
    if (r.status >= 300) std::cout << "  ! " << r.status << " " << r.body << "\n";
    return json::parse(r.body, nullptr, false);
  };
  auto flush = [&] {
    for (const auto& o : world.outputs.log) print_output(o, std::cout);
    world.outputs.log.clear();
  };
  auto advance = [&](game::TimeMs ms) {
    for (; ms > 0; ms -= 100) {
      clock.advance(std::min<game::TimeMs>(100, ms));
      world.engine.tick();
    }
  };
  std::map<std::string, std::string> names;
  auto uid_for = [&](const std::string& who) {
    if (names.count(who)) return names[who];
    for (const auto& p : world.engine.players().all())
      if (p.name == who) return p.uid;
    return who;
  };

  const std::string game = flag(argc, argv, "--game", "");
  if (!game.empty()) {
    call("POST", "/api/game/select", {{"id", game}});
    call("POST", "/api/game/start");
  }
  std::cout << "orca-sim repl. Type 'help'.\n";
  std::string line;
  while (std::cout << "[" << game::format_mmss(clock.now_ms()) << "] > " << std::flush, std::getline(std::cin, line)) {
    std::istringstream in(line);
    std::string cmd;
    in >> cmd;
    std::string a, b;
    in >> a;
    std::getline(in >> std::ws, b);
    if (cmd.empty()) continue;
    if (cmd == "quit" || cmd == "exit") break;
    if (cmd == "help") {
      std::cout << "  games | select <id> [params-json] | start | pause | resume | stop | reset\n"
                   "  add <name> [team] | tap <name|uid> | players | board | status\n"
                   "  advance <dur e.g. 5m> | event <mult> <dur> | waiver <dur> | endevents\n"
                   "  save <file> | load <file> | quit\n";
    } else if (cmd == "games") {
      for (const auto& g : call("GET", "/api/games")["games"]) std::cout << "  " << g["id"].get<std::string>() << " - " << g["name"].get<std::string>() << "\n";
    } else if (cmd == "select") {
      call("POST", "/api/game/select", {{"id", a}, {"params", b.empty() ? json::object() : json::parse(b, nullptr, false)}});
    } else if (cmd == "start" || cmd == "pause" || cmd == "resume" || cmd == "stop" || cmd == "reset") {
      call("POST", "/api/game/" + cmd);
    } else if (cmd == "add") {
      char uid[16];
      std::snprintf(uid, sizeof uid, "04%010zX", names.size() + 1);
      names[a] = uid;
      auto p = call("POST", "/api/players", {{"uid", uid}, {"name", a}});
      if (!b.empty() && p.contains("no")) call("PATCH", "/api/players/" + std::to_string(p["no"].get<int>()), {{"team", b}});
      std::cout << "  added #" << p.value("no", 0) << " " << a << " (" << uid << ")\n";
    } else if (cmd == "tap") {
      auto r = call("POST", "/api/sim/tap", {{"uid", uid_for(a)}});
      std::cout << "  " << r.value("outcome", "?") << " " << r.value("points", 0) << " pts\n";
    } else if (cmd == "players") {
      for (const auto& p : call("GET", "/api/players")["players"])
        std::cout << "  #" << p["no"] << " " << p["name"].get<std::string>() << " " << p["team"].get<std::string>() << " " << p["points"] << " pts"
                  << (p["banned"].get<bool>() ? " BANNED" : "") << "\n";
    } else if (cmd == "board") {
      auto b2 = call("GET", "/api/board");
      for (const auto& r : b2["board"]) std::cout << "  " << r["rank"] << ". " << r["name"].get<std::string>() << " " << r["points"] << "\n";
      for (const auto& t : b2["teams"]) std::cout << "  team " << t["team"].get<std::string>() << ": " << t["points"] << "\n";
    } else if (cmd == "status") {
      std::cout << call("GET", "/api/status").dump(2) << "\n";
    } else if (cmd == "advance") {
      const auto ms = game::parse_duration(a);
      if (ms < 0) std::cout << "  bad duration\n";
      else advance(ms);
    } else if (cmd == "event") {
      call("POST", "/api/events/trigger", {{"multiplier", std::atof(a.c_str())}, {"duration_ms", game::parse_duration(b)}});
    } else if (cmd == "waiver") {
      call("POST", "/api/events/trigger", {{"kind", "cooldown_waiver"}, {"name", "Free ride"}, {"duration_ms", game::parse_duration(a)}});
    } else if (cmd == "endevents") {
      call("POST", "/api/events/end");
    } else if (cmd == "save") {
      std::ofstream(a) << world.engine.snapshot().dump(2);
    } else if (cmd == "load") {
      try {
        world.engine.restore(json::parse(read_file(a)));
      } catch (const std::exception& e) {
        std::cout << "  ! " << e.what() << "\n";
      }
    } else {
      std::cout << "  unknown command (try 'help')\n";
    }
    flush();
  }
  return 0;
}

// ---------------------------------------------------------------- serve

// Wall-clock time sped up by `speed` (e.g. 10x makes a 5 min cooldown last 30 s).
class ScaledClock : public hal::Clock {
 public:
  explicit ScaledClock(double speed) : speed_(speed), start_(std::chrono::steady_clock::now()) {}
  hal::TimeMs now_ms() const override {
    const auto real = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_);
    return static_cast<hal::TimeMs>(real.count() * speed_);
  }

 private:
  double speed_;
  std::chrono::steady_clock::time_point start_;
};

std::atomic<bool> g_stop{false};

int cmd_serve(int argc, char** argv) {
  const int port = std::atoi(flag(argc, argv, "--port", "8080").c_str());
  const double speed = std::atof(flag(argc, argv, "--speed", "1").c_str());
  const std::string web = flag(argc, argv, "--web", "");
  const std::string state = flag(argc, argv, "--state", "");
  const bool quiet = has_flag(argc, argv, "--quiet");
  if (speed <= 0) {
    std::cerr << "--speed must be > 0\n";
    return 2;
  }

  ScaledClock clock(speed);
  fakes::SeededRandom rng(static_cast<std::uint64_t>(std::stoull(flag(argc, argv, "--seed", "1"))));
  api::ApiOptions options;
  options.admin_pin = flag(argc, argv, "--pin", "1031");
  sim::World world(clock, rng, options);
  sim::SimServer server(world, web);
  world.listeners.push_back([&](const json& m) { server.broadcast(m); });

  hal::PosixFileSystem fs;
  game::SnapshotStore store(fs, state);
  if (!state.empty()) {
    if (auto snap = store.load()) {
      try {
        std::lock_guard<std::recursive_mutex> lock(world.mutex);
        world.engine.restore(*snap);
        std::cerr << "restored " << state << " (" << store.loaded_from() << ")\n";
      } catch (const std::exception& e) {
        std::cerr << "ignoring bad state file: " << e.what() << "\n";
      }
    }
  }

  if (!server.listen(port)) return 1;
  std::signal(SIGINT, [](int) { g_stop = true; });
  std::signal(SIGTERM, [](int) { g_stop = true; });
  std::signal(SIGPIPE, SIG_IGN);
  std::cerr << "orca-sim serving on http://localhost:" << port << " (speed " << speed << "x, PIN "
            << options.admin_pin << ")" << (web.empty() ? "" : ", web UI from " + web) << "\n";

  // Ticker: drives the engine like game_task, forwards simulated hardware output, persists.
  std::thread ticker([&] {
    std::uint64_t saved_changes = world.engine.change_count();
    auto last_save = std::chrono::steady_clock::now();
    while (!g_stop) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::vector<json> outs;
      std::uint64_t changes;
      json snap;
      {
        std::lock_guard<std::recursive_mutex> lock(world.mutex);
        world.engine.tick();
        outs.swap(world.outputs.log);
        world.outputs.sounds.clear();
        changes = world.engine.change_count();
        if (!state.empty() && changes != saved_changes &&
            std::chrono::steady_clock::now() - last_save > std::chrono::seconds(2))
          snap = world.engine.snapshot();
      }
      for (const auto& o : outs) {
        if (!quiet) print_output(o, std::cerr);
        json msg = o;
        msg["type"] = "device";
        server.broadcast(msg);
      }
      if (!snap.is_null()) {
        if (!store.save(snap)) std::cerr << "failed to save " << state << "\n";
        saved_changes = changes;
        last_save = std::chrono::steady_clock::now();
      }
    }
    server.stop();
  });
  server.run();
  g_stop = true;
  ticker.join();
  if (!state.empty()) {
    std::lock_guard<std::recursive_mutex> lock(world.mutex);
    store.save(world.engine.snapshot());
  }
  std::cerr << "bye\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string cmd = argc > 1 ? argv[1] : "";
  try {
    if (cmd == "run") return cmd_run(argc, argv);
    if (cmd == "repl") return cmd_repl(argc, argv);
    if (cmd == "serve") return cmd_serve(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "orca-sim: " << e.what() << "\n";
    return 1;
  }
  std::cerr << "usage: orca-sim run|repl|serve ... (see host/sim/main.cpp)\n";
  return 2;
}
