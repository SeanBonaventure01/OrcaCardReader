#include "orca/fx/voice.hpp"

#include <algorithm>
#include <cstring>

namespace orca::fx {

namespace {

constexpr int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

struct State {
  int predictor = 0;
  int index = 0;

  // Applies one 4-bit code and returns the new sample.
  std::int16_t step(int code) {
    const int s = kStepTable[index];
    int diff = s >> 3;
    if (code & 4) diff += s;
    if (code & 2) diff += s >> 1;
    if (code & 1) diff += s >> 2;
    predictor += (code & 8) ? -diff : diff;
    predictor = std::clamp(predictor, -32768, 32767);
    index = std::clamp(index + kIndexTable[code], 0, 88);
    return static_cast<std::int16_t>(predictor);
  }
};

std::uint32_t le32(const std::uint8_t* p) {
  return static_cast<std::uint32_t>(p[0]) | p[1] << 8 | p[2] << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

}  // namespace

std::vector<std::uint8_t> adpcm_encode(const std::vector<std::int16_t>& pcm) {
  std::vector<std::uint8_t> out((pcm.size() + 1) / 2, 0);
  State st;
  for (std::size_t i = 0; i < pcm.size(); ++i) {
    const int s = kStepTable[st.index];
    int diff = pcm[i] - st.predictor;
    int code = 0;
    if (diff < 0) {
      code = 8;
      diff = -diff;
    }
    if (diff >= s) code |= 4, diff -= s;
    if (diff >= s >> 1) code |= 2, diff -= s >> 1;
    if (diff >= s >> 2) code |= 1;
    st.step(code);  // track exactly what the decoder will reconstruct
    out[i / 2] |= static_cast<std::uint8_t>(i % 2 ? code << 4 : code);
  }
  return out;
}

std::vector<std::int16_t> adpcm_decode(const std::uint8_t* data, std::size_t bytes, std::size_t samples) {
  samples = std::min(samples, bytes * 2);
  std::vector<std::int16_t> out(samples);
  State st;
  for (std::size_t i = 0; i < samples; ++i) out[i] = st.step(i % 2 ? data[i / 2] >> 4 : data[i / 2] & 0x0F);
  return out;
}

std::optional<VoicePack> parse_voice_pack(const std::uint8_t* b, std::size_t size) {
  if (!b || size < 9 || std::memcmp(b, "ORCV", 4) != 0 || b[4] != 2) return std::nullopt;
  VoicePack pack;
  const std::size_t count = b[5];
  pack.sample_rate = static_cast<std::uint32_t>(b[6] | b[7] << 8);
  const std::size_t voices = b[8];
  std::size_t p = 9;
  for (std::size_t v = 0; v < voices; ++v) {
    if (p >= size || p + 1 + b[p] > size) return std::nullopt;
    pack.voices.emplace_back(reinterpret_cast<const char*>(b + p + 1), b[p]);
    p += 1 + b[p];
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (p + 15 > size) return std::nullopt;
    const std::uint8_t kind = b[p], voice = b[p + 1];
    const std::size_t text_len = b[p + 2];
    const std::size_t offset = le32(b + p + 3), bytes = le32(b + p + 7), samples = le32(b + p + 11);
    p += 15;
    if (kind > 1 || voice >= voices || p + text_len > size || offset > size || bytes > size - offset ||
        samples > bytes * 2)
      return std::nullopt;
    pack.lines.push_back({static_cast<LineKind>(kind), voice,
                          std::string(reinterpret_cast<const char*>(b + p), text_len), b + offset, bytes, samples});
    p += text_len;
  }
  return pack;
}

std::vector<std::int16_t> resample_linear(const std::vector<std::int16_t>& in, std::uint32_t from_hz,
                                          std::uint32_t to_hz) {
  if (in.empty() || from_hz == 0 || to_hz == 0 || from_hz == to_hz) return in;
  const std::size_t n = static_cast<std::size_t>(static_cast<std::uint64_t>(in.size()) * to_hz / from_hz);
  std::vector<std::int16_t> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    // Position in the input, in 1/65536ths of a sample.
    const std::uint64_t pos = static_cast<std::uint64_t>(i) * from_hz * 65536 / to_hz;
    const std::size_t k = static_cast<std::size_t>(pos >> 16);
    const std::int32_t frac = static_cast<std::int32_t>(pos & 0xFFFF);
    const std::int32_t a = in[std::min(k, in.size() - 1)], c = in[std::min(k + 1, in.size() - 1)];
    out[i] = static_cast<std::int16_t>(a + (((c - a) * frac) >> 16));
  }
  return out;
}

std::uint32_t ShuffleBag::rand() {  // xorshift32
  rng_ ^= rng_ << 13;
  rng_ ^= rng_ >> 17;
  rng_ ^= rng_ << 5;
  return rng_;
}

std::size_t ShuffleBag::next() {
  if (size_ <= 1) return 0;
  if (pos_ >= order_.size()) {
    order_.resize(size_);
    for (std::size_t i = 0; i < size_; ++i) order_[i] = i;
    for (std::size_t i = size_ - 1; i > 0; --i) std::swap(order_[i], order_[rand() % (i + 1)]);
    if (last_ && order_.front() == *last_) std::swap(order_.front(), order_.back());
    pos_ = 0;
  }
  last_ = order_[pos_++];
  return *last_;
}

}  // namespace orca::fx
