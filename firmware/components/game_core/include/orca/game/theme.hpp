#pragma once

#include <cstdint>
#include <string>

#include "orca/game/types.hpp"
#include "orca/hal/outputs.hpp"

namespace orca::game {

// Default presentation per outcome. Feedback fields override these.
struct ThemeEntry {
  hal::Tone tone;
  std::string headline;
  std::string detail;
  std::string sound;
  std::string leds;
  std::uint32_t hold_ms;
};

// The ORCA look: green "Fare paid", amber "Transfer", red "Card not recognized", ...
ThemeEntry orca_theme(Outcome outcome);

// Sound and LED ids used outside of taps.
namespace fx {
inline constexpr const char* kStartSound = "start";
inline constexpr const char* kEndSound = "end";
inline constexpr const char* kEventStartSound = "event_start";
inline constexpr const char* kEventEndSound = "event_end";
inline constexpr const char* kIdleLeds = "idle_breathe";
inline constexpr const char* kEventLeds = "event_strobe";
inline constexpr const char* kOffLeds = "off";
inline constexpr std::uint32_t kBannerHoldMs = 4000;
}  // namespace fx

}  // namespace orca::game
