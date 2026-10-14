#include "orca/nfc/card_payload.hpp"

namespace orca::nfc {

namespace {
constexpr std::uint8_t kVersion = 1;
std::uint8_t checksum(const std::vector<std::uint8_t>& b) {
  std::uint8_t x = 0;
  for (std::size_t i = 0; i < 7; ++i) x ^= b[i];
  return x;
}
}  // namespace

std::vector<std::uint8_t> encode_payload(const CardPayload& p) {
  std::vector<std::uint8_t> b = {'O', 'R', 'C', 'A', kVersion, static_cast<std::uint8_t>(p.player >> 8),
                                 static_cast<std::uint8_t>(p.player & 0xFF), 0};
  b[7] = checksum(b);
  return b;
}

std::optional<CardPayload> decode_payload(const std::vector<std::uint8_t>& b) {
  if (b.size() < 8) return std::nullopt;
  if (b[0] != 'O' || b[1] != 'R' || b[2] != 'C' || b[3] != 'A' || b[4] != kVersion) return std::nullopt;
  if (checksum(b) != b[7]) return std::nullopt;
  CardPayload p;
  p.player = static_cast<std::uint16_t>(b[5] << 8 | b[6]);
  if (p.player == 0) return std::nullopt;
  return p;
}

}  // namespace orca::nfc
