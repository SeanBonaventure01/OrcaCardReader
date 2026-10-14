#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

#include "nlohmann/json.hpp"
#include "orca/hal/clock.hpp"

namespace orca::game {

using json = nlohmann::json;
using hal::TimeMs;

enum class Outcome {
  Accepted,  // fare paid, points awarded
  Bonus,     // accepted with a multiplier or special reward
  Cooldown,  // tapped again too soon
  Rejected,  // game-specific refusal
  Unknown,   // card not registered and enrollment closed
  Banned,    // player banned by admin
  Closed,    // no game running (not started, paused or ended)
};

const char* to_string(Outcome o);

// What a game wants to happen for a tap. Empty strings and zeros fall back to the theme.
// headline/detail support placeholders: {points} {points_text} ("1 pt"/"3 pts") {balance}
// {name} {no} {mult} {team} {team_total} {until} ("until 1:07 pm", or "for 4:59" before the
// phone has synced the clock)
struct Feedback {
  Outcome outcome = Outcome::Rejected;
  int points = 0;  // base points before multipliers (may be negative)
  TimeMs valid_until = -1;  // game time the fare stays good until (Fare Paid family); -1 = n/a
  bool apply_multiplier = true;
  std::string headline;
  std::string detail;
  std::string sound;
  std::string leds;
  std::uint32_t hold_ms = 0;
};

struct Player {
  int number = 0;
  std::string uid;  // uppercase hex
  std::string name;
  std::string team;
  bool banned = false;
};

void to_json(json& j, const Player& p);
void from_json(const json& j, Player& p);

// Error raised by engine commands; `status` maps onto an HTTP status code.
class EngineError : public std::runtime_error {
 public:
  EngineError(int status, const std::string& msg) : std::runtime_error(msg), status_(status) {}
  int status() const { return status_; }

 private:
  int status_;
};

// "m:ss" for countdowns, e.g. 192000 -> "3:12". Negative values clamp to 0.
std::string format_mmss(TimeMs ms);

// Parses "250ms", "30s", "5m", "1h", "1m30s" or a bare integer (milliseconds).
// Returns -1 on malformed input.
TimeMs parse_duration(const std::string& s);

}  // namespace orca::game
