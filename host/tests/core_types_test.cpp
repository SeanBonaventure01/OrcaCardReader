#include <gtest/gtest.h>

#include "orca/game/types.hpp"
#include "orca/hal/card.hpp"

using orca::game::format_mmss;
using orca::game::parse_duration;
using orca::hal::CardUid;

TEST(FormatMmss, RoundsUpToWholeSeconds) {
  EXPECT_EQ(format_mmss(0), "0:00");
  EXPECT_EQ(format_mmss(1), "0:01");
  EXPECT_EQ(format_mmss(59'000), "0:59");
  EXPECT_EQ(format_mmss(60'000), "1:00");
  EXPECT_EQ(format_mmss(192'000), "3:12");
  EXPECT_EQ(format_mmss(299'999), "5:00");
  EXPECT_EQ(format_mmss(3'600'000), "60:00");
}

TEST(FormatMmss, NegativeClampsToZero) { EXPECT_EQ(format_mmss(-5000), "0:00"); }

TEST(ParseDuration, Units) {
  EXPECT_EQ(parse_duration("250ms"), 250);
  EXPECT_EQ(parse_duration("30s"), 30'000);
  EXPECT_EQ(parse_duration("5m"), 300'000);
  EXPECT_EQ(parse_duration("1h"), 3'600'000);
  EXPECT_EQ(parse_duration("1m30s"), 90'000);
  EXPECT_EQ(parse_duration("5m1s"), 301'000);
  EXPECT_EQ(parse_duration("0s"), 0);
}

TEST(ParseDuration, BareNumberIsMilliseconds) { EXPECT_EQ(parse_duration("1500"), 1500); }

TEST(ParseDuration, RejectsMalformed) {
  for (const char* bad : {"", "m", "5x", "5 m", "-5s", "1.5s", "5m30", "abc"}) EXPECT_EQ(parse_duration(bad), -1) << bad;
}

TEST(CardUid, ParsesHexWithSeparators) {
  auto a = CardUid::from_hex("04:a1:b2:c3:d4:e5:f6");
  ASSERT_TRUE(a);
  EXPECT_EQ(a->hex(), "04A1B2C3D4E5F6");
  EXPECT_EQ(a->size(), 7u);
  auto b = CardUid::from_hex("04 A1 B2 C3");
  ASSERT_TRUE(b);
  EXPECT_EQ(b->hex(), "04A1B2C3");
}

TEST(CardUid, EqualityByBytes) {
  EXPECT_EQ(*CardUid::from_hex("04a1b2c3"), *CardUid::from_hex("04A1B2C3"));
  EXPECT_NE(*CardUid::from_hex("04a1b2c3"), *CardUid::from_hex("04a1b2c4"));
}

TEST(CardUid, RejectsBadInput) {
  EXPECT_FALSE(CardUid::from_hex(""));
  EXPECT_FALSE(CardUid::from_hex("04A1B2"));                  // 3 bytes: too short
  EXPECT_FALSE(CardUid::from_hex("04A1B2C3D4E5F60708090A0B"));  // 12 bytes: too long
  EXPECT_FALSE(CardUid::from_hex("04A1B2C"));                 // odd digit count
  EXPECT_FALSE(CardUid::from_hex("04A1B2CZ"));
}

TEST(CardUid, FromBytes) {
  const std::uint8_t raw[] = {0xDE, 0xAD, 0xBE, 0xEF};
  auto u = CardUid::from_bytes(raw, 4);
  ASSERT_TRUE(u);
  EXPECT_EQ(u->hex(), "DEADBEEF");
  EXPECT_FALSE(CardUid::from_bytes(raw, 3));
}
