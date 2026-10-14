#pragma once

// What we write onto "programmed" NTAG cards, so a card still knows its player number on a
// freshly wiped costume. Stored in NTAG user pages 4-5 (8 bytes):
//   page 4: 'O' 'R' 'C' 'A'
//   page 5: version(=1), player number (big-endian u16), checksum (xor of bytes 0-6)

#include <cstdint>
#include <optional>
#include <vector>

namespace orca::nfc {

inline constexpr std::uint8_t kPayloadFirstPage = 4;

struct CardPayload {
  std::uint16_t player = 0;
};

std::vector<std::uint8_t> encode_payload(const CardPayload& p);  // 8 bytes
// Accepts at least 8 bytes (e.g. a 16-byte NTAG READ of page 4). nullopt if not ours.
std::optional<CardPayload> decode_payload(const std::vector<std::uint8_t>& bytes);

}  // namespace orca::nfc
