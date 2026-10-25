#pragma once

// What's written on programmed cards: an NDEF Text record "orca-card-game-07" in NTAG user
// memory (from page 4). It's the standard format phone NFC apps (e.g. NFC Tools) read and
// write, so a card can be checked, or even programmed, with a phone.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orca::nfc {

inline constexpr std::uint8_t kUserMemoryFirstPage = 4;
inline constexpr const char* kCardTextPrefix = "orca-card-game-";

// "orca-card-game-07": player numbers are zero-padded to two digits.
std::string card_text(int player);
// The player number in a card's text: the prefix (any case, surrounding spaces ignored)
// followed by 1-4 digits, 1-9999. nullopt for anything else.
std::optional<int> parse_card_text(const std::string& text);

// NDEF message TLV holding one Text record (language "en"), plus a terminator TLV, padded
// with zeros to whole 4-byte pages.
std::vector<std::uint8_t> encode_ndef_text(const std::string& text);
// The first Text record in the NDEF message in `mem` (user memory from page 4). nullopt if
// there's no NDEF message, no Text record, or `mem` is too short to hold it.
std::optional<std::string> decode_ndef_text(const std::vector<std::uint8_t>& mem);
// Given the first bytes of user memory, how many bytes must be read to hold the whole NDEF
// message: 0 if there's no NDEF message, never more than `max`.
std::size_t ndef_bytes_needed(const std::vector<std::uint8_t>& start, std::size_t max = 128);

// encode_ndef_text(card_text(player)) / parse_card_text(decode_ndef_text(mem)).
std::vector<std::uint8_t> encode_card(int player);
std::optional<int> decode_card(const std::vector<std::uint8_t>& mem);

}  // namespace orca::nfc
