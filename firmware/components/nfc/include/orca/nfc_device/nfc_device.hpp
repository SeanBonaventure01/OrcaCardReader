#pragma once

// The PN532 card reader on the I2C header (docs/ARCHITECTURE.md §6.1). nfc_task polls for
// ISO14443A cards, debounces them, reads the player number programmed onto NTAG cards and
// reports each new tap; when asked, it programs a player number onto the card.

#include <functional>
#include <optional>
#include <string>

#include "esp_err.h"

namespace orca::nfc_device {

enum class WriteResult { Ok, Failed, Unsupported };

// Both run on nfc_task while the card is still in the field.
struct CardHandler {
  // A new tap. `card_player` is the number programmed onto the card, if any. Returns the
  // player number to program onto the card, or nullopt for an ordinary tap.
  std::function<std::optional<int>(const std::string& uid_hex, std::optional<int> card_player)> seen;
  // The outcome of programming `number`, after reading it back.
  std::function<void(const std::string& uid_hex, int number, WriteResult result)> written;
};

// Creates the I2C bus and starts nfc_task. Returns an error only if the I2C bus can't be
// created; a missing or silent reader is retried in the background.
esp_err_t start(int sda_pin, int scl_pin, CardHandler handler);

// True once the reader has answered and been configured, false while it's missing or failing.
bool reader_ok();

}  // namespace orca::nfc_device
