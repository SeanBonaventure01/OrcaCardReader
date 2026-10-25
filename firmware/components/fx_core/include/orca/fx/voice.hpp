#pragma once

// Voice lines (profanity mode etc.): compressed clips built into the firmware, played after
// the tap sound. No I/O here, so it's all host-tested.
//
// Clips are IMA ADPCM (4 bits/sample, 8 KB/s at 16 kHz), packed into one "voice pack" blob by
// tools/assets/make_voice.py. A pack can hold the same lines in several voices.
//
//   "ORCV" | u8 version (2) | u8 count | u16 sample_rate | u8 voices | voices x { u8 len | name }
//   count x { u8 kind | u8 voice | u8 text_len | u32 offset | u32 bytes | u32 samples | text }
//   ADPCM data, each clip starting from predictor 0 / step index 0, low nibble first
// (integers little-endian)

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orca::fx {

// IMA ADPCM, standard step/index tables. encode is used by tests and mirrors the tool.
std::vector<std::uint8_t> adpcm_encode(const std::vector<std::int16_t>& pcm);
std::vector<std::int16_t> adpcm_decode(const std::uint8_t* data, std::size_t bytes, std::size_t samples);

enum class LineKind : std::uint8_t { Accept = 0, Reject = 1 };

struct VoiceLine {
  LineKind kind;
  std::size_t voice;         // index into VoicePack::voices
  std::string text;          // what it says (for logs and the phone UI)
  const std::uint8_t* data;  // ADPCM, points into the pack blob
  std::size_t bytes;
  std::size_t samples;
};

struct VoicePack {
  std::uint32_t sample_rate = 0;
  std::vector<std::string> voices;  // e.g. "Ava (Premium)", "Bruce"
  std::vector<VoiceLine> lines;
};

// Linear-interpolation resampling (voice clips are stored at a lower rate than the speaker
// plays). Fine for speech; not for music.
std::vector<std::int16_t> resample_linear(const std::vector<std::int16_t>& in, std::uint32_t from_hz,
                                          std::uint32_t to_hz);

// nullopt if the blob is malformed (bad magic/version, or an entry running past the end).
std::optional<VoicePack> parse_voice_pack(const std::uint8_t* blob, std::size_t size);

// Hands out every index once, in shuffled order, before reshuffling; never the same index
// twice in a row across reshuffles (unless there's only one).
class ShuffleBag {
 public:
  ShuffleBag(std::size_t size, std::uint32_t seed) : size_(size), rng_(seed ? seed : 1) {}
  std::size_t next();

 private:
  std::uint32_t rand();
  std::size_t size_;
  std::uint32_t rng_;
  std::vector<std::size_t> order_;
  std::size_t pos_ = 0;
  std::optional<std::size_t> last_;
};

}  // namespace orca::fx
