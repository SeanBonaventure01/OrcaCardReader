#pragma once

#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "orca/config/config.hpp"
#include "orca/game/engine.hpp"
#include "orca/hal/clock.hpp"
#include "orca/hal/random.hpp"

namespace orca::api {

using json = nlohmann::json;
using hal::TimeMs;

struct ApiRequest {
  std::string method;  // "GET", "POST", "PATCH", ...
  std::string path;    // without the query string
  std::string query;   // raw, e.g. "limit=5"
  std::string body;
  std::string token;   // from "Authorization: Bearer <t>" or ?token=
};

struct ApiResponse {
  int status = 200;
  std::string body;
  std::string content_type = "application/json";
};

// Gives the API exclusive access to the engine. On the device the function runs inside
// game_task; EngineError thrown by `fn` must be rethrown to the caller.
class EngineGateway {
 public:
  virtual ~EngineGateway() = default;
  virtual json with_engine(const std::function<json(game::Engine&)>& fn) = 0;
};

// Runtime settings (volume, brightness, LEDs). The device applies and persists them.
class SettingsStore {
 public:
  virtual ~SettingsStore() = default;
  virtual config::Settings get() = 0;
  virtual void set(const config::Settings& s) = 0;
};

struct ApiOptions {
  std::string admin_pin = "1031";
  TimeMs session_ttl = hal::minutes(12 * 60);
  std::size_t max_sessions = 16;
  int max_failed_logins = 5;
  TimeMs lockout = hal::seconds(60);
};

class Api {
 public:
  Api(EngineGateway& engine, SettingsStore& settings, const hal::Clock& clock, hal::Random& rng, ApiOptions options);

  // Thread-safe. Never throws.
  ApiResponse handle(const ApiRequest& req);

  // First message for a new WebSocket client: current status and leaderboard.
  json hello();

  static std::map<std::string, std::string> parse_query(const std::string& query);

 private:
  struct Ctx {
    const ApiRequest& req;
    json body;
    std::map<std::string, std::string> query;
    int number = 0;  // {no} path parameter
  };
  using Handler = std::function<ApiResponse(Ctx&)>;
  struct Route {
    std::string method;
    std::vector<std::string> segments;  // "{no}" matches a positive integer
    bool admin;
    Handler handler;
  };

  void add(const std::string& method, const std::string& pattern, bool admin, Handler h);
  ApiResponse dispatch(const ApiRequest& req);
  bool authorized(const std::string& token);
  ApiResponse login(Ctx& c);
  json engine(const std::function<json(game::Engine&)>& fn) { return engine_.with_engine(fn); }

  EngineGateway& engine_;
  SettingsStore& settings_;
  const hal::Clock& clock_;
  hal::Random& rng_;
  ApiOptions options_;
  std::vector<Route> routes_;

  std::mutex mutex_;  // guards sessions and login throttling
  std::map<std::string, TimeMs> sessions_;  // token -> expiry
  int failed_logins_ = 0;
  TimeMs locked_until_ = 0;
};

}  // namespace orca::api
