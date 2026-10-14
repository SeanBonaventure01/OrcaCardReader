#include <gtest/gtest.h>

#include <cmath>

#include "orca/fx/audio.hpp"
#include "orca/fx/leds.hpp"

using namespace orca::fx;

// ---------------------------------------------------------------- LEDs

TEST(Leds, EffectsAreDeterministic) {
  for (const char* e : {"idle_breathe", "accept_flash", "reject_flash", "cooldown_pulse", "bonus_chase", "event_strobe",
                        "fault_blink"}) {
    EXPECT_TRUE(is_known_effect(e));
    for (std::int64_t t : {0, 123, 999, 4567}) {
      Frame a(30), b(30);
      render(e, t, a);
      render(e, t, b);
      EXPECT_EQ(a, b) << e << " @" << t;
    }
  }
}

TEST(Leds, UnknownEffectIsOff) {
  EXPECT_FALSE(is_known_effect("disco"));
  Frame f(5, Rgb{9, 9, 9});
  render("disco", 100, f);
  EXPECT_EQ(estimate_ma(f), 5);
}

TEST(Leds, AcceptFlashFadesOut) {
  Frame start(1), end(1);
  render("accept_flash", 0, start);
  render("accept_flash", 1500, end);
  EXPECT_GT(start[0].g, 200);
  EXPECT_EQ(end[0], Rgb{});
}

TEST(Leds, RejectBlinksTwice) {
  auto lit = [](std::int64_t t) {
    Frame f(1);
    render("reject_flash", t, f);
    return f[0].r > 0;
  };
  EXPECT_TRUE(lit(0));
  EXPECT_FALSE(lit(200));
  EXPECT_TRUE(lit(350));
  EXPECT_FALSE(lit(500));
}

TEST(Leds, CurrentLimitNeverExceeded) {
  for (int pixels : {1, 30, 120}) {
    for (int max_ma : {50, 200, 450, 2000}) {
      for (std::int64_t t = 0; t < 4000; t += 333) {
        for (const char* e : {"event_strobe", "bonus_chase", "accept_flash"}) {
          Frame f(pixels);
          render(e, t, f);
          limit_current(f, max_ma);
          EXPECT_LE(estimate_ma(f), std::max(max_ma, pixels)) << e << " px=" << pixels << " max=" << max_ma;
        }
      }
    }
  }
}

TEST(Leds, LimitDoesNotScaleUp) {
  Frame f(10, Rgb{10, 10, 10});
  const Frame before = f;
  limit_current(f, 2000);
  EXPECT_EQ(f, before);
}

TEST(Leds, EstimateFullWhite) {
  Frame f(30, Rgb{255, 255, 255});
  EXPECT_EQ(estimate_ma(f), 30 * 60 + 30);
}

TEST(Leds, Brightness) {
  Frame f(1, Rgb{200, 100, 0});
  apply_brightness(f, 50);
  EXPECT_EQ(f[0], (Rgb{100, 50, 0}));
  apply_brightness(f, 0);
  EXPECT_EQ(f[0], Rgb{});
}

TEST(LedAnimator, OverlayReturnsToBase) {
  LedAnimator a;
  a.set_base("idle_breathe", 0);
  a.play("accept_flash", 2500, 1000);
  EXPECT_EQ(a.current(1000), "accept_flash");
  EXPECT_EQ(a.current(3499), "accept_flash");
  EXPECT_EQ(a.current(3500), "idle_breathe");
  a.play("off", 0, 4000);  // duration 0 replaces the base
  EXPECT_EQ(a.current(5000), "off");
  EXPECT_FALSE(a.animating(5000));
}

TEST(LedAnimator, NewOverlayRestartsTiming) {
  LedAnimator a;
  a.play("reject_flash", 1000, 0);
  a.play("reject_flash", 1000, 500);
  Frame f(1);
  a.render_at(500, f);
  EXPECT_GT(f[0].r, 0);  // restarted: t=0 of the blink
}

// ---------------------------------------------------------------- WAV

namespace {
std::vector<std::uint8_t> wav(std::uint16_t format, std::uint16_t channels, std::uint32_t rate, std::uint16_t bits,
                              std::uint32_t data_bytes, bool with_list = false) {
  std::vector<std::uint8_t> b;
  auto s = [&](const char* t) { b.insert(b.end(), t, t + 4); };
  auto u32 = [&](std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
  };
  auto u16 = [&](std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
  };
  s("RIFF");
  u32(0);
  s("WAVE");
  s("fmt ");
  u32(16);
  u16(format);
  u16(channels);
  u32(rate);
  u32(rate * channels * bits / 8);
  u16(static_cast<std::uint16_t>(channels * bits / 8));
  u16(bits);
  if (with_list) {
    s("LIST");
    u32(5);
    b.insert(b.end(), {'a', 'b', 'c', 'd', 'e', 0});  // odd size + pad byte
  }
  s("data");
  u32(data_bytes);
  b.resize(b.size() + data_bytes, 0);
  return b;
}
}  // namespace

TEST(Wav, AcceptsMono16) {
  auto b = wav(1, 1, 22050, 16, 1000);
  auto r = parse_wav_header(b.data(), b.size(), b.size());
  ASSERT_TRUE(r.info) << r.error;
  EXPECT_EQ(r.info->sample_rate, 22050u);
  EXPECT_EQ(r.info->data_offset, 44u);
  EXPECT_EQ(r.info->data_size, 1000u);
}

TEST(Wav, SkipsListChunkWithPadding) {
  auto b = wav(1, 1, 16000, 16, 200, true);
  auto r = parse_wav_header(b.data(), b.size(), b.size());
  ASSERT_TRUE(r.info) << r.error;
  EXPECT_EQ(r.info->data_offset, 44u + 8 + 6);
}

TEST(Wav, TruncatedFileClampsDataSize) {
  auto b = wav(1, 1, 22050, 16, 1000);
  auto r = parse_wav_header(b.data(), 64, 500);
  ASSERT_TRUE(r.info) << r.error;
  EXPECT_EQ(r.info->data_size, 500u - 44u);
}

TEST(Wav, RejectsUnsupported) {
  struct Case {
    std::vector<std::uint8_t> bytes;
    const char* error_part;
  };
  for (const auto& c : {Case{wav(1, 2, 22050, 16, 100), "mono"}, Case{wav(1, 1, 22050, 8, 100), "16-bit"},
                        Case{wav(3, 1, 22050, 32, 100), "not PCM"}, Case{wav(1, 1, 96000, 16, 100), "sample rate"},
                        Case{wav(1, 1, 22050, 16, 0), "no audio"}, Case{{'n', 'o', 'p', 'e'}, "RIFF"}}) {
    auto r = parse_wav_header(c.bytes.data(), c.bytes.size(), c.bytes.size());
    EXPECT_FALSE(r.info);
    EXPECT_NE(r.error.find(c.error_part), std::string::npos) << r.error;
  }
}

// ---------------------------------------------------------------- mixing & synthesis

TEST(Audio, MixClampsAndHandlesShortInputs) {
  const std::int16_t a[] = {30000, 30000, 100};
  const std::int16_t b[] = {30000};
  std::int16_t out[4];
  mix(a, 3, 100, b, 1, 100, out, 4);
  EXPECT_EQ(out[0], 32767);
  EXPECT_EQ(out[1], 30000);
  EXPECT_EQ(out[2], 100);
  EXPECT_EQ(out[3], 0);
  mix(a, 3, 50, b, 1, 0, out, 1);
  EXPECT_EQ(out[0], 15000);
}

TEST(Audio, ToneLengthAndEnvelope) {
  auto t = synth_tone(22050, 1000, 100);
  EXPECT_EQ(t.size(), 2205u);
  EXPECT_EQ(t.front(), 0);      // attack starts silent
  EXPECT_EQ(t.back(), 0);       // release ends silent
  EXPECT_EQ(std::abs(t[1100]), 12000);
}

TEST(Audio, BeepsHaveExpectedShapes) {
  EXPECT_EQ(synth_beep("accept", 22050).size(), 22050u * 180 / 1000);
  EXPECT_EQ(synth_beep("reject", 22050).size(), 22050u * 140 / 1000 * 2 + 22050u * 80 / 1000);
  EXPECT_FALSE(synth_beep("bonus", 22050).empty());
  EXPECT_TRUE(synth_beep("unknown", 22050).empty());
}

TEST(SoundMap, ParsesAndSkipsBadEntries) {
  auto m = parse_sound_map(R"({"accept": "sounds/accept.wav", "bad": 3, "evil": "../etc/passwd", "abs": "/x.wav"})");
  EXPECT_EQ(m.files.size(), 1u);
  EXPECT_EQ(m.files["accept"], "sounds/accept.wav");
  EXPECT_EQ(m.warnings.size(), 3u);
  EXPECT_FALSE(parse_sound_map("[]").warnings.empty());
}
