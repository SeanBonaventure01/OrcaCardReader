#pragma once

// Audio helpers with no I/O: WAV header parsing, mixing, beep synthesis and the sound-id map.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace orca::fx {

struct WavInfo {
  std::uint32_t sample_rate = 0;
  std::uint16_t channels = 0;
  std::uint16_t bits = 0;
  std::size_t data_offset = 0;  // byte offset of the PCM samples in the file
  std::size_t data_size = 0;    // bytes of PCM
};

struct WavResult {
  std::optional<WavInfo> info;
  std::string error;
};

// Parses a RIFF/WAVE header from the first bytes of a file (pass at least 512 bytes when
// available; LIST chunks before "data" are skipped). Only 16-bit mono PCM at 8-48 kHz is
// accepted, because that's what the I2S path plays.
WavResult parse_wav_header(const std::uint8_t* data, std::size_t len, std::size_t file_size);

// out[i] = clamp(a[i]*va + b[i]*vb), volumes 0-100. Shorter inputs are treated as silence.
void mix(const std::int16_t* a, std::size_t na, int va, const std::int16_t* b, std::size_t nb, int vb,
         std::int16_t* out, std::size_t n);

// A tone with 5 ms attack/release, so beeps don't click. Square waves sound most like the reader.
std::vector<std::int16_t> synth_tone(std::uint32_t sample_rate, double freq_hz, std::uint32_t ms, int amplitude = 12000,
                                     bool square = true);

// The ORCA-style feedback beeps: accept = one high beep; reject = two low beeps.
std::vector<std::int16_t> synth_beep(const std::string& id, std::uint32_t sample_rate);

struct SoundMap {
  std::map<std::string, std::string> files;  // id -> path on SD (e.g. "sounds/accept.wav")
  std::vector<std::string> warnings;
};

// Parses sounds.json: {"accept": "sounds/accept.wav", ...}. Bad entries are skipped with warnings.
SoundMap parse_sound_map(const std::string& json_text);

}  // namespace orca::fx
