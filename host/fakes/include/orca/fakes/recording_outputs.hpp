#pragma once

#include <string>
#include <vector>

#include "nlohmann/json.hpp"
#include "orca/hal/outputs.hpp"

namespace orca::fakes {

// Records every output command as JSON, in order.
class RecordingOutputs : public hal::Outputs {
 public:
  using json = nlohmann::json;

  void screen(const hal::ScreenCmd& c) override {
    last_screen = c;
    log.push_back({{"out", "screen"},
                   {"kind", hal::to_string(c.kind)},
                   {"tone", hal::to_string(c.tone)},
                   {"headline", c.headline},
                   {"detail", c.detail},
                   {"player", c.player_label},
                   {"hold_ms", c.hold_ms},
                   {"note", c.note}});
  }
  void sound(const hal::SoundCmd& c) override {
    sounds.push_back(c.id);
    log.push_back({{"out", "sound"}, {"id", c.id}});
  }
  void leds(const hal::LedCmd& c) override {
    last_leds = c.effect;
    log.push_back({{"out", "leds"}, {"effect", c.effect}, {"duration_ms", c.duration_ms}});
  }

  void clear() {
    log.clear();
    sounds.clear();
  }

  std::vector<json> log;
  std::vector<std::string> sounds;
  hal::ScreenCmd last_screen;
  std::string last_leds;
};

}  // namespace orca::fakes
