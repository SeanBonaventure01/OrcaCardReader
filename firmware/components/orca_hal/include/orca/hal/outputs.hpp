#pragma once

#include <cstdint>
#include <string>

namespace orca::hal {

enum class Tone { Neutral, Success, Warning, Error, Bonus };

enum class ScreenKind {
  Idle,    // "Tap card here"
  Result,  // full-screen tap result; returns to Idle after hold_ms
  Banner,  // overlay announcement (events)
  Fault,   // "Out of service"
};

struct ScreenCmd {
  ScreenKind kind = ScreenKind::Idle;
  Tone tone = Tone::Neutral;
  std::string headline;
  std::string detail;
  std::string player_label;  // e.g. "#07 Ghost"
  std::uint32_t hold_ms = 0;
};

struct SoundCmd {
  std::string id;  // logical id, mapped to a file by sounds.json
};

struct LedCmd {
  std::string effect;  // e.g. "accept_flash", "idle_breathe"
  std::uint32_t duration_ms = 0;  // 0 = until replaced
};

// Where the game engine sends its output. The device implementation posts to the
// UI/audio/LED task queues; tests and the simulator record the calls.
class Outputs {
 public:
  virtual ~Outputs() = default;
  virtual void screen(const ScreenCmd& cmd) = 0;
  virtual void sound(const SoundCmd& cmd) = 0;
  virtual void leds(const LedCmd& cmd) = 0;
};

const char* to_string(Tone t);
const char* to_string(ScreenKind k);

}  // namespace orca::hal
