#include "orca/nfc/card_payload.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace orca::nfc {

namespace {

constexpr std::uint8_t kTlvNull = 0x00;
constexpr std::uint8_t kTlvNdef = 0x03;
constexpr std::uint8_t kTlvTerminator = 0xFE;

struct Tlv {
  std::size_t value_at = 0;  // offset of the value
  std::size_t length = 0;
};

// Finds the NDEF message TLV, skipping NULL and other (lock/memory control) TLVs. A length
// that runs past `mem` is still returned, so callers can tell how much more to read.
std::optional<Tlv> find_ndef_tlv(const std::vector<std::uint8_t>& mem) {
  std::size_t i = 0;
  while (i < mem.size()) {
    const std::uint8_t t = mem[i];
    if (t == kTlvNull) {
      ++i;
      continue;
    }
    if (t == kTlvTerminator || i + 1 >= mem.size()) return std::nullopt;
    Tlv tlv;
    if (mem[i + 1] == 0xFF) {  // 3-byte length format
      if (i + 3 >= mem.size()) return std::nullopt;
      tlv.length = static_cast<std::size_t>(mem[i + 2] << 8 | mem[i + 3]);
      tlv.value_at = i + 4;
    } else {
      tlv.length = mem[i + 1];
      tlv.value_at = i + 2;
    }
    if (t == kTlvNdef) return tlv;
    i = tlv.value_at + tlv.length;
  }
  return std::nullopt;
}

}  // namespace

std::string card_text(int player) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%s%02d", kCardTextPrefix, player);
  return buf;
}

std::optional<int> parse_card_text(const std::string& raw) {
  std::size_t b = 0, e = raw.size();
  while (b < e && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(raw[e - 1]))) --e;
  const std::string prefix = kCardTextPrefix;
  if (e - b <= prefix.size() || e - b > prefix.size() + 4) return std::nullopt;
  for (std::size_t k = 0; k < prefix.size(); ++k)
    if (std::tolower(static_cast<unsigned char>(raw[b + k])) != prefix[k]) return std::nullopt;
  int n = 0;
  for (std::size_t k = b + prefix.size(); k < e; ++k) {
    if (!std::isdigit(static_cast<unsigned char>(raw[k]))) return std::nullopt;
    n = n * 10 + (raw[k] - '0');
  }
  return n >= 1 ? std::optional<int>(n) : std::nullopt;
}

std::vector<std::uint8_t> encode_ndef_text(const std::string& text) {
  // Record: MB|ME|SR, TNF 1 (well-known), type "T"; payload = status (UTF-8, language
  // length 2) + "en" + text.
  std::vector<std::uint8_t> record = {0xD1, 0x01, static_cast<std::uint8_t>(3 + text.size()), 'T', 0x02, 'e', 'n'};
  record.insert(record.end(), text.begin(), text.end());
  std::vector<std::uint8_t> out = {kTlvNdef, static_cast<std::uint8_t>(record.size())};
  out.insert(out.end(), record.begin(), record.end());
  out.push_back(kTlvTerminator);
  while (out.size() % 4) out.push_back(0x00);
  return out;
}

std::optional<std::string> decode_ndef_text(const std::vector<std::uint8_t>& mem) {
  const auto tlv = find_ndef_tlv(mem);
  if (!tlv || tlv->value_at + tlv->length > mem.size()) return std::nullopt;
  std::size_t i = tlv->value_at;
  const std::size_t end = tlv->value_at + tlv->length;
  while (i + 3 <= end) {
    const std::uint8_t header = mem[i];
    const bool last = header & 0x40, short_record = header & 0x10, has_id = header & 0x08;
    const std::uint8_t tnf = header & 0x07;
    const std::size_t type_len = mem[i + 1];
    std::size_t p = i + 2;
    std::size_t payload_len;
    if (short_record) {
      payload_len = mem[p++];
    } else {
      if (p + 4 > end) return std::nullopt;
      payload_len = static_cast<std::size_t>(mem[p]) << 24 | mem[p + 1] << 16 | mem[p + 2] << 8 | mem[p + 3];
      p += 4;
    }
    std::size_t id_len = 0;
    if (has_id) {
      if (p >= end) return std::nullopt;
      id_len = mem[p++];
    }
    const std::size_t type_at = p, payload_at = p + type_len + id_len;
    if (payload_at + payload_len > end) return std::nullopt;
    if (tnf == 0x01 && type_len == 1 && mem[type_at] == 'T' && payload_len >= 1) {
      const std::uint8_t status = mem[payload_at];
      const std::size_t lang_len = status & 0x3F;
      if ((status & 0x80) || 1 + lang_len > payload_len) return std::nullopt;  // UTF-16 isn't used by us
      return std::string(mem.begin() + static_cast<long>(payload_at + 1 + lang_len),
                         mem.begin() + static_cast<long>(payload_at + payload_len));
    }
    if (last) break;
    i = payload_at + payload_len;
  }
  return std::nullopt;
}

std::size_t ndef_bytes_needed(const std::vector<std::uint8_t>& start, std::size_t max) {
  const auto tlv = find_ndef_tlv(start);
  if (!tlv) return 0;
  return std::min(max, tlv->value_at + tlv->length);
}

std::vector<std::uint8_t> encode_card(int player) { return encode_ndef_text(card_text(player)); }

std::optional<int> decode_card(const std::vector<std::uint8_t>& mem) {
  const auto text = decode_ndef_text(mem);
  return text ? parse_card_text(*text) : std::nullopt;
}

}  // namespace orca::nfc
