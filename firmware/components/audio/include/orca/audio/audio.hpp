#pragma once

#include "esp_err.h"
#include "orca/hal/outputs.hpp"

namespace orca::audio {

// Starts the audio task: ORCA beeps are synthesized (no SD card needed) and played over I2S
// to the board's speaker amplifier. If I2S can't start, sounds are only logged.
esp_err_t start(int volume_percent);

void play(const hal::SoundCmd& cmd);  // thread-safe; a new sound interrupts the current one
void set_volume(int percent);

}  // namespace orca::audio
