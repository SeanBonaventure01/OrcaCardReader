#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace orca::hal {

// NFC card UID (4, 7 or 10 bytes). Its uppercase hex string is the player key.
class CardUid {
 public:
  static constexpr std::size_t kMaxLen = 10;

  CardUid() = default;
  static std::optional<CardUid> from_bytes(const std::uint8_t* data, std::size_t len);
  // Accepts hex with optional ':' or ' ' separators, any case. Requires 4..10 bytes.
  static std::optional<CardUid> from_hex(const std::string& hex);

  std::string hex() const;
  std::size_t size() const { return len_; }
  const std::uint8_t* data() const { return bytes_.data(); }

  bool operator==(const CardUid& o) const { return len_ == o.len_ && bytes_ == o.bytes_; }
  bool operator!=(const CardUid& o) const { return !(*this == o); }

 private:
  std::array<std::uint8_t, kMaxLen> bytes_{};
  std::size_t len_ = 0;
};

}  // namespace orca::hal
