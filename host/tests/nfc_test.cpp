#include <gtest/gtest.h>

#include <deque>

#include "orca/nfc/card_payload.hpp"
#include "orca/nfc/pn532.hpp"
#include "orca/nfc/tap_debouncer.hpp"

using namespace orca::nfc;

// ---------------------------------------------------------------- frames

TEST(Pn532Frame, BuildMatchesDatasheetExample) {
  // GetFirmwareVersion: 00 00 FF 02 FE D4 02 2A 00
  EXPECT_EQ(build_frame(cmd::kGetFirmwareVersion), (Bytes{0x00, 0x00, 0xFF, 0x02, 0xFE, 0xD4, 0x02, 0x2A, 0x00}));
  // SAMConfiguration normal mode: 00 00 FF 05 FB D4 14 01 00 01 16 00
  EXPECT_EQ(build_frame(cmd::kSamConfiguration, {0x01, 0x00, 0x01}),
            (Bytes{0x00, 0x00, 0xFF, 0x05, 0xFB, 0xD4, 0x14, 0x01, 0x00, 0x01, 0x16, 0x00}));
}

TEST(Pn532Frame, ParseAckNackError) {
  EXPECT_EQ(parse_frame(kAckFrame).kind, FrameKind::Ack);
  EXPECT_EQ(parse_frame(kNackFrame).kind, FrameKind::Nack);
  EXPECT_EQ(parse_frame({0x00, 0x00, 0xFF, 0x01, 0xFF, 0x7F, 0x81, 0x00}).kind, FrameKind::Error);
}

TEST(Pn532Frame, ParseFirmwareResponse) {
  // Response from a real PN532 v1.6: D5 03 32 01 06 07
  const Bytes raw = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
  auto p = parse_frame(raw);
  ASSERT_EQ(p.kind, FrameKind::Data);
  EXPECT_EQ(p.tfi, kPn532ToHost);
  EXPECT_EQ(p.data, (Bytes{0x03, 0x32, 0x01, 0x06, 0x07}));
  EXPECT_EQ(p.consumed, raw.size());
  auto v = parse_firmware_version(Bytes(p.data.begin() + 1, p.data.end()));
  ASSERT_TRUE(v);
  EXPECT_EQ(v->ic, 0x32);
  EXPECT_EQ(v->ver, 1);
  EXPECT_EQ(v->rev, 6);
}

TEST(Pn532Frame, SkipsLeadingGarbageAndDetectsBadChecksums) {
  Bytes raw = {0x55, 0x01, 0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
  EXPECT_EQ(parse_frame(raw).kind, FrameKind::Data);
  raw[11] ^= 0x01;  // corrupt payload -> DCS fails
  EXPECT_EQ(parse_frame(raw).kind, FrameKind::Invalid);
  EXPECT_EQ(parse_frame({0x00, 0x00, 0xFF, 0x06, 0xF0}).kind, FrameKind::Invalid);  // LCS wrong
}

TEST(Pn532Frame, IncompleteUntilAllBytesArrive) {
  const Bytes raw = build_frame(0x4A, {0x01, 0x00});
  for (std::size_t n = 0; n < raw.size() - 1; ++n)
    EXPECT_EQ(parse_frame(Bytes(raw.begin(), raw.begin() + static_cast<long>(n))).kind, FrameKind::Incomplete) << n;
}

TEST(Pn532Frame, BuildThenParseRoundTrip) {
  const Bytes params = {0x01, 0x30, 0x04};
  auto p = parse_frame(build_frame(cmd::kInDataExchange, params));
  ASSERT_EQ(p.kind, FrameKind::Data);
  EXPECT_EQ(p.tfi, kHostToPn532);
  EXPECT_EQ(p.data, (Bytes{0x40, 0x01, 0x30, 0x04}));
}

TEST(Pn532Parse, PassiveTargetSevenByteUid) {
  // NbTg=1, Tg=1, SENS_RES=0044, SEL_RES=00, len 7, UID 04 A1 B2 C3 D4 E5 F6 (NTAG215)
  auto t = parse_passive_target({0x01, 0x01, 0x00, 0x44, 0x00, 0x07, 0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6});
  ASSERT_TRUE(t);
  EXPECT_EQ(t->sens_res, 0x0044);
  EXPECT_EQ(t->uid, (Bytes{0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6}));
  EXPECT_FALSE(parse_passive_target({0x00}));                                  // no card
  EXPECT_FALSE(parse_passive_target({0x01, 0x01, 0x00, 0x44, 0x00, 0x07, 0x04}));  // truncated
  EXPECT_FALSE(parse_passive_target({0x01, 0x01, 0x00, 0x44, 0x00, 0x02, 0x04, 0x05}));  // uid too short
}

// ---------------------------------------------------------------- driver over a fake transport

class ScriptedTransport : public Transport {
 public:
  bool write(const Bytes& d) override {
    writes.push_back(d);
    return write_ok;
  }
  bool wait_ready(std::uint32_t) override {
    if (ready.empty()) return false;
    bool r = ready.front();
    ready.pop_front();
    return r;
  }
  Bytes read(std::size_t len) override {
    Bytes out;
    while (out.size() < len && !incoming.empty()) {
      out.push_back(incoming.front());
      incoming.pop_front();
    }
    return out.size() == len ? out : Bytes{};
  }
  // Queue an ACK then a response frame (PN532 -> host) for `data` (response code + payload).
  void respond(const Bytes& data) {
    ready.push_back(true);
    incoming.insert(incoming.end(), kAckFrame.begin(), kAckFrame.end());
    ready.push_back(true);
    const std::size_t len = data.size() + 1;
    Bytes f = {0x00, 0x00, 0xFF, static_cast<std::uint8_t>(len), static_cast<std::uint8_t>(0x100 - len), kPn532ToHost};
    std::uint8_t sum = kPn532ToHost;
    for (auto b : data) {
      f.push_back(b);
      sum = static_cast<std::uint8_t>(sum + b);
    }
    f.push_back(static_cast<std::uint8_t>(0x100 - sum));
    f.push_back(0x00);
    incoming.insert(incoming.end(), f.begin(), f.end());
  }

  std::vector<Bytes> writes;
  std::deque<bool> ready;
  std::deque<std::uint8_t> incoming;
  bool write_ok = true;
};

TEST(Pn532Driver, FirmwareVersion) {
  ScriptedTransport t;
  t.respond({0x03, 0x32, 0x01, 0x06, 0x07});
  Pn532 pn(t);
  auto v = pn.firmware_version();
  ASSERT_TRUE(v);
  EXPECT_EQ(v->ver, 1);
  EXPECT_EQ(t.writes.at(0), build_frame(cmd::kGetFirmwareVersion));
}

TEST(Pn532Driver, PollCardFoundAndEmpty) {
  ScriptedTransport t;
  t.respond({0x4B, 0x01, 0x01, 0x00, 0x44, 0x00, 0x07, 0x04, 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6});
  t.respond({0x4B, 0x00});
  Pn532 pn(t);
  auto card = pn.poll_card();
  ASSERT_TRUE(card);
  EXPECT_EQ(card->uid.size(), 7u);
  EXPECT_FALSE(pn.poll_card());
  EXPECT_EQ(pn.last_error(), Pn532Error::None);  // empty field is not an error
}

TEST(Pn532Driver, SetPassiveRetries) {
  ScriptedTransport t;
  t.respond({0x33});
  Pn532 pn(t);
  EXPECT_TRUE(pn.set_passive_retries(2));
  EXPECT_EQ(t.writes.at(0), build_frame(cmd::kRfConfiguration, {0x05, 0xFF, 0x01, 0x02}));
}

TEST(Pn532Driver, ErrorPaths) {
  ScriptedTransport t;
  Pn532 pn(t);
  t.write_ok = false;
  EXPECT_FALSE(pn.firmware_version());
  EXPECT_EQ(pn.last_error(), Pn532Error::WriteFailed);

  t.write_ok = true;
  EXPECT_FALSE(pn.firmware_version());  // never ready
  EXPECT_EQ(pn.last_error(), Pn532Error::NoAck);

  t.ready = {true, false};
  t.incoming.assign(kAckFrame.begin(), kAckFrame.end());
  EXPECT_FALSE(pn.firmware_version());
  EXPECT_EQ(pn.last_error(), Pn532Error::Timeout);

  t.ready.clear();
  t.incoming.clear();
  t.respond({0x15});  // SAMConfiguration response to a GetFirmwareVersion command
  EXPECT_FALSE(pn.firmware_version());
  EXPECT_EQ(pn.last_error(), Pn532Error::WrongResponse);
}

TEST(Pn532Driver, NtagReadAndWrite) {
  ScriptedTransport t;
  Bytes page = {0x41, 0x00};
  for (int i = 0; i < 16; ++i) page.push_back(static_cast<std::uint8_t>(i));
  t.respond(page);
  t.respond({0x41, 0x00});
  t.respond({0x41, 0x01});  // card reported an error status
  Pn532 pn(t);
  auto data = pn.ntag_read(4);
  ASSERT_TRUE(data);
  EXPECT_EQ(data->size(), 16u);
  EXPECT_EQ((*data)[15], 15);
  EXPECT_TRUE(pn.ntag_write(4, {'O', 'R', 'C', 'A'}));
  EXPECT_EQ(t.writes[1], build_frame(cmd::kInDataExchange, {0x01, 0xA2, 0x04, 'O', 'R', 'C', 'A'}));
  EXPECT_FALSE(pn.ntag_write(5, {1, 2, 3, 4}));
  EXPECT_EQ(pn.last_error(), Pn532Error::CardError);
  EXPECT_FALSE(pn.ntag_write(5, {1, 2, 3}));  // wrong size, nothing sent
  EXPECT_EQ(t.writes.size(), 3u);
}

// ---------------------------------------------------------------- card text (NDEF)

TEST(CardText, FormatAndParse) {
  EXPECT_EQ(card_text(7), "orca-card-game-07");
  EXPECT_EQ(card_text(20), "orca-card-game-20");
  EXPECT_EQ(card_text(123), "orca-card-game-123");
  EXPECT_EQ(parse_card_text("orca-card-game-07"), 7);
  EXPECT_EQ(parse_card_text("  ORCA-Card-Game-7\n"), 7);  // typed by hand in a phone app
  EXPECT_EQ(parse_card_text("orca-card-game-9999"), 9999);
  EXPECT_FALSE(parse_card_text("orca-card-game-00"));
  EXPECT_FALSE(parse_card_text("orca-card-game-"));
  EXPECT_FALSE(parse_card_text("orca-card-game-12345"));
  EXPECT_FALSE(parse_card_text("orca-card-game-7a"));
  EXPECT_FALSE(parse_card_text("hello"));
}

TEST(CardText, EncodesAStandardNdefTextRecord) {
  // TLV 03 len | D1 01 plen 'T' | 02 'e' 'n' | text | FE | zero padding to whole pages
  const Bytes b = encode_ndef_text("hi");
  EXPECT_EQ(b, (Bytes{0x03, 0x09, 0xD1, 0x01, 0x05, 'T', 0x02, 'e', 'n', 'h', 'i', 0xFE}));
  const Bytes card = encode_card(7);
  EXPECT_EQ(card.size() % 4, 0u);
  EXPECT_EQ(card.size(), 28u);  // 7 pages
  EXPECT_EQ(decode_card(card), 7);
}

TEST(CardText, ReadsWhatPhoneAppsWrite) {
  // Lock control TLV first and a NULL TLV (as some tags ship), then a two-record message:
  // a URI record before our Text record, language "en-US".
  Bytes mem = {0x01, 0x03, 0xA0, 0x0C, 0x34, 0x00, 0x03, 0x00};
  const Bytes uri = {0x91, 0x01, 0x04, 'U', 0x04, 'a', '.', 'b'};  // MB, SR, not ME
  const std::string text = "orca-card-game-12";
  Bytes txt = {0x51, 0x01, static_cast<std::uint8_t>(6 + text.size()), 'T', 0x05, 'e', 'n', '-', 'U', 'S'};  // ME, SR
  txt.insert(txt.end(), text.begin(), text.end());
  mem[7] = static_cast<std::uint8_t>(uri.size() + txt.size());
  mem.insert(mem.end(), uri.begin(), uri.end());
  mem.insert(mem.end(), txt.begin(), txt.end());
  mem.push_back(0xFE);
  EXPECT_EQ(decode_ndef_text(mem), text);
  EXPECT_EQ(decode_card(mem), 12);
  EXPECT_EQ(ndef_bytes_needed(Bytes(mem.begin(), mem.begin() + 16)), 8 + uri.size() + txt.size());
}

TEST(CardText, RejectsBlankTruncatedAndForeignCards) {
  EXPECT_FALSE(decode_card(Bytes(16, 0x00)));  // factory-blank user memory
  EXPECT_EQ(ndef_bytes_needed(Bytes(16, 0x00)), 0u);
  EXPECT_FALSE(decode_card({0x03, 0x00, 0xFE, 0x00}));  // empty NDEF message
  Bytes card = encode_card(3);
  EXPECT_EQ(ndef_bytes_needed(Bytes(card.begin(), card.begin() + 16)), 26u);
  card.resize(16);  // only the first read came back
  EXPECT_FALSE(decode_card(card));
  EXPECT_FALSE(decode_card(encode_ndef_text("hello world")));  // someone else's text
}

// ---------------------------------------------------------------- debouncer

TEST(TapDebouncer, HoldingACardCountsOnce) {
  TapDebouncer d;
  EXPECT_TRUE(d.card_present("A", 0));
  for (int t = 50; t < 5000; t += 50) EXPECT_FALSE(d.card_present("A", t));
}

TEST(TapDebouncer, MissedPollsWhileHeldDoNotRetrigger) {
  TapDebouncer d;
  EXPECT_TRUE(d.card_present("A", 0));
  d.field_empty(50);
  d.field_empty(100);
  EXPECT_FALSE(d.card_present("A", 150));
}

TEST(TapDebouncer, MustLeaveAndRespectRepeatWindow) {
  TapDebouncer d(300, 1500);
  EXPECT_TRUE(d.card_present("A", 0));
  d.field_empty(400);                     // left the field
  EXPECT_FALSE(d.card_present("A", 1000));  // bounced back within 1.5 s
  d.field_empty(1400);
  EXPECT_TRUE(d.card_present("A", 2000));
}

TEST(TapDebouncer, DifferentCardsAreIndependent) {
  TapDebouncer d;
  EXPECT_TRUE(d.card_present("A", 0));
  EXPECT_TRUE(d.card_present("B", 100));  // swapped cards quickly
  // A again right after B counts: the repeat window only guards against one card bouncing.
  EXPECT_TRUE(d.card_present("A", 200));
}
