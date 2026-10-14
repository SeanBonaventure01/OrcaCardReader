#include <cctype>

#include "orca/hal/card.hpp"
#include "orca/hal/outputs.hpp"

namespace orca::hal {

const char* to_string(Tone t) {
  switch (t) {
    case Tone::Neutral: return "neutral";
    case Tone::Success: return "success";
    case Tone::Warning: return "warning";
    case Tone::Error: return "error";
    case Tone::Bonus: return "bonus";
  }
  return "?";
}

const char* to_string(ScreenKind k) {
  switch (k) {
    case ScreenKind::Idle: return "idle";
    case ScreenKind::Result: return "result";
    case ScreenKind::Banner: return "banner";
    case ScreenKind::Fault: return "fault";
  }
  return "?";
}

std::optional<CardUid> CardUid::from_bytes(const std::uint8_t* data, std::size_t len) {
  if (len < 4 || len > kMaxLen) return std::nullopt;
  CardUid uid;
  for (std::size_t i = 0; i < len; ++i) uid.bytes_[i] = data[i];
  uid.len_ = len;
  return uid;
}

std::optional<CardUid> CardUid::from_hex(const std::string& hex) {
  std::array<std::uint8_t, kMaxLen> buf{};
  std::size_t n = 0;
  int pending = -1;
  for (char c : hex) {
    if (c == ':' || c == ' ' || c == '-') continue;
    if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
    const int v = std::isdigit(static_cast<unsigned char>(c)) ? c - '0'
                                                              : std::toupper(static_cast<unsigned char>(c)) - 'A' + 10;
    if (pending < 0) {
      pending = v;
    } else {
      if (n == kMaxLen) return std::nullopt;
      buf[n++] = static_cast<std::uint8_t>(pending << 4 | v);
      pending = -1;
    }
  }
  if (pending >= 0) return std::nullopt;
  return from_bytes(buf.data(), n);
}

std::string CardUid::hex() const {
  static const char* digits = "0123456789ABCDEF";
  std::string s;
  s.reserve(len_ * 2);
  for (std::size_t i = 0; i < len_; ++i) {
    s.push_back(digits[bytes_[i] >> 4]);
    s.push_back(digits[bytes_[i] & 0xF]);
  }
  return s;
}

}  // namespace orca::hal
