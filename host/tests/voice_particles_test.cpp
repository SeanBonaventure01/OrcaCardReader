// Voice lines (ADPCM clips in a voice pack, shuffled) and the confetti/fireworks simulation.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <set>

#include "orca/fx/particles.hpp"
#include "orca/fx/voice.hpp"

using namespace orca::fx;

namespace {

std::vector<std::int16_t> tone(std::size_t n, double freq, int amp) {
  std::vector<std::int16_t> v(n);
  for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::int16_t>(amp * std::sin(2 * M_PI * freq * i / 22050));
  return v;
}

void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// Builds a pack the way tools/assets/make_voice.py does (16 kHz, two voices).
struct Clip {
  LineKind kind;
  std::uint8_t voice;
  std::string text;
  std::vector<std::uint8_t> adpcm;
  std::size_t samples;
};

std::vector<std::uint8_t> make_pack(const std::vector<std::string>& voices, const std::vector<Clip>& clips) {
  std::vector<std::uint8_t> b = {'O', 'R', 'C', 'V', 2, static_cast<std::uint8_t>(clips.size()), 0x80, 0x3E,
                                 static_cast<std::uint8_t>(voices.size())};
  for (const auto& v : voices) {
    b.push_back(static_cast<std::uint8_t>(v.size()));
    b.insert(b.end(), v.begin(), v.end());
  }
  std::size_t offset = b.size();
  for (const auto& c : clips) offset += 15 + c.text.size();
  for (const auto& c : clips) {
    b.push_back(static_cast<std::uint8_t>(c.kind));
    b.push_back(c.voice);
    b.push_back(static_cast<std::uint8_t>(c.text.size()));
    put32(b, static_cast<std::uint32_t>(offset));
    put32(b, static_cast<std::uint32_t>(c.adpcm.size()));
    put32(b, static_cast<std::uint32_t>(c.samples));
    b.insert(b.end(), c.text.begin(), c.text.end());
    offset += c.adpcm.size();
  }
  for (const auto& c : clips) b.insert(b.end(), c.adpcm.begin(), c.adpcm.end());
  return b;
}

}  // namespace

TEST(Adpcm, RoundTripIsCloseAndFourToOne) {
  const auto pcm = tone(2205, 440, 20000);
  const auto enc = adpcm_encode(pcm);
  EXPECT_EQ(enc.size(), (pcm.size() + 1) / 2);
  const auto dec = adpcm_decode(enc.data(), enc.size(), pcm.size());
  ASSERT_EQ(dec.size(), pcm.size());
  double err = 0, sig = 0;
  for (std::size_t i = 200; i < pcm.size(); ++i) {  // after the step size has adapted
    err += std::pow(dec[i] - pcm[i], 2);
    sig += std::pow(pcm[i], 2);
  }
  EXPECT_GT(10 * std::log10(sig / err), 20.0);  // > 20 dB signal-to-noise: fine for speech
}

TEST(Adpcm, KnownCodesAndOddLengths) {
  // Silence encodes to zero codes, decodes to (near) silence.
  const auto enc = adpcm_encode(std::vector<std::int16_t>(5, 0));
  EXPECT_EQ(enc.size(), 3u);
  for (auto v : adpcm_decode(enc.data(), enc.size(), 5)) EXPECT_LE(std::abs(v), 8);
  // Low nibble first: code 0x7 (largest positive step) then 0x0.
  const std::uint8_t one = 0x07;
  const auto d = adpcm_decode(&one, 1, 2);
  EXPECT_EQ(d[0], 7 + 7 / 2 + 7 / 4 + (7 >> 3));  // step 7: diff = s/8 + s + s/2 + s/4
  EXPECT_EQ(adpcm_decode(&one, 1, 99).size(), 2u);  // never reads past the data
}

TEST(VoicePack, ParsesVoicesAndLinesAndRejectsBadBlobs) {
  const auto a = adpcm_encode(tone(1000, 300, 9000)), r = adpcm_encode(tone(501, 200, 9000));
  const auto blob = make_pack({"Ava (Premium)", "Bruce"}, {{LineKind::Accept, 0, "eat shit", a, 1000},
                                                           {LineKind::Reject, 1, "nice try", r, 501}});
  auto pack = parse_voice_pack(blob.data(), blob.size());
  ASSERT_TRUE(pack);
  EXPECT_EQ(pack->sample_rate, 16000u);
  EXPECT_EQ(pack->voices, (std::vector<std::string>{"Ava (Premium)", "Bruce"}));
  ASSERT_EQ(pack->lines.size(), 2u);
  EXPECT_EQ(pack->lines[0].kind, LineKind::Accept);
  EXPECT_EQ(pack->lines[0].voice, 0u);
  EXPECT_EQ(pack->lines[0].text, "eat shit");
  EXPECT_EQ(pack->lines[1].kind, LineKind::Reject);
  EXPECT_EQ(pack->lines[1].voice, 1u);
  EXPECT_EQ(pack->lines[1].samples, 501u);
  EXPECT_EQ(std::memcmp(pack->lines[1].data, r.data(), r.size()), 0);

  auto truncated = blob;
  truncated.resize(blob.size() - 1);  // last clip runs past the end
  EXPECT_FALSE(parse_voice_pack(truncated.data(), truncated.size()));
  auto bad_voice = make_pack({"Bruce"}, {{LineKind::Accept, 1, "x", a, 1000}});  // voice 1 of 1
  EXPECT_FALSE(parse_voice_pack(bad_voice.data(), bad_voice.size()));
  auto old_version = blob;
  old_version[4] = 1;
  EXPECT_FALSE(parse_voice_pack(old_version.data(), old_version.size()));
  EXPECT_FALSE(parse_voice_pack(nullptr, 0));
}

TEST(Resample, SixteenToTwentyTwoKeepsPitchAndLength) {
  std::vector<std::int16_t> in(16000);
  for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<std::int16_t>(10000 * std::sin(2 * M_PI * 440 * i / 16000));
  const auto out = resample_linear(in, 16000, 22050);
  EXPECT_EQ(out.size(), 22050u);  // still one second
  // Still a 440 Hz tone at the new rate: zero crossings ~ 2 * 440 per second.
  int crossings = 0;
  for (std::size_t i = 1; i < out.size(); ++i) crossings += (out[i - 1] < 0) != (out[i] < 0);
  EXPECT_NEAR(crossings, 880, 4);
  EXPECT_EQ(resample_linear(in, 16000, 16000), in);
  EXPECT_TRUE(resample_linear({}, 16000, 22050).empty());
}

TEST(ShuffleBag, EveryLineOnceBeforeRepeatsAndNoBackToBack) {
  ShuffleBag bag(7, 42);
  std::size_t prev = 99;
  for (int round = 0; round < 50; ++round) {
    std::set<std::size_t> seen;
    for (int i = 0; i < 7; ++i) {
      const std::size_t n = bag.next();
      EXPECT_LT(n, 7u);
      EXPECT_NE(n, prev);
      prev = n;
      seen.insert(n);
    }
    EXPECT_EQ(seen.size(), 7u);
  }
  ShuffleBag one(1, 3);
  EXPECT_EQ(one.next(), 0u);
  EXPECT_EQ(one.next(), 0u);
}

TEST(Particles, ConfettiFallsFluttersAndFinishes) {
  Particles p(800, 480, 7);
  p.confetti(60);
  ASSERT_EQ(p.particles().size(), 60u);
  float y0 = 0;
  for (const auto& q : p.particles()) y0 += q.y;
  for (int i = 0; i < 30; ++i) p.step(1.0f / 30);  // 1 s
  ASSERT_TRUE(p.active());
  float y1 = 0;
  bool narrowed = false;
  for (const auto& q : p.particles()) {
    y1 += q.y;
    narrowed |= Particles::drawn_width(q) < q.w * 0.9f;
  }
  EXPECT_GT(y1 / p.particles().size(), y0 / 60 + 100);  // it fell
  EXPECT_TRUE(narrowed);                                 // and turns over as it goes
  for (int i = 0; i < 120; ++i) p.step(1.0f / 30);       // +4 s
  EXPECT_FALSE(p.active());                              // gone before the next tap, usually
}

TEST(Particles, FireworksBurstStaggeredAndFade) {
  Particles p(800, 480, 9);
  p.fireworks(3, 24);
  ASSERT_EQ(p.particles().size(), 72u);
  int visible = 0;
  for (const auto& q : p.particles()) visible += Particles::visible(q);
  EXPECT_EQ(visible, 24);  // later bursts wait their turn
  p.step(0.2f);
  float spread = 0;
  for (const auto& q : p.particles())
    if (Particles::visible(q)) spread = std::max(spread, std::hypot(q.vx, q.vy));
  EXPECT_GT(spread, 10.0f);
  for (int i = 0; i < 25; ++i) p.step(0.04f);  // ~1.2 s: first burst is fading or gone
  bool faded = false;
  for (const auto& q : p.particles()) faded |= Particles::visible(q) && Particles::opacity(q) < 200;
  EXPECT_TRUE(faded || p.particles().size() < 72u);
  for (int i = 0; i < 100; ++i) p.step(0.04f);
  EXPECT_FALSE(p.active());
}

TEST(Particles, NeverMoreThanTheDisplayHasSlotsFor) {
  Particles p(800, 480, 1);
  p.confetti(80);
  p.fireworks(3, 24);
  EXPECT_EQ(p.particles().size(), Particles::kMax);
}

#include "orca/fx/rainbow.hpp"

TEST(Rainbow, HsvPrimariesAndWrap) {
  EXPECT_EQ(hsv(0, 1, 1), 0xFF0000u);
  EXPECT_EQ(hsv(1.0f / 3, 1, 1), 0x00FF00u);
  EXPECT_EQ(hsv(2.0f / 3, 1, 1), 0x0000FFu);
  EXPECT_EQ(hsv(1.25f, 1, 1), hsv(0.25f, 1, 1));
  EXPECT_EQ(hsv(0.5f, 0, 1), 0xFFFFFFu);
  EXPECT_EQ(hsv(0.5f, 1, 0), 0x000000u);
}

TEST(Rainbow, RingIsEveryColourAndItMoves) {
  const auto a = rainbow_ring(40, 0), b = rainbow_ring(40, 250);
  ASSERT_EQ(a.size(), 40u);
  EXPECT_NE(a, b);                                // animates
  EXPECT_EQ(rainbow_ring(40, 2000), a);           // one full turn per period
  std::set<std::uint32_t> distinct(a.begin(), a.end());
  EXPECT_GT(distinct.size(), 30u);                // a real rainbow, not a few colours
  for (auto c : a) EXPECT_GT(std::max({c >> 16, (c >> 8) & 0xFF, c & 0xFF}), 120u);  // never dark
}
