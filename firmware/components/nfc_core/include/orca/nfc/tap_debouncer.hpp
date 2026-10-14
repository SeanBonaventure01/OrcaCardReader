#pragma once

#include <cstdint>
#include <string>

namespace orca::nfc {

// Turns raw "card present / no card" polls into taps:
// - a card counts once when it enters the field, however long it's held there;
// - it must leave the field (absent for `leave_ms`) before it can tap again;
// - the same card tapping again within `repeat_ms` of its last tap is ignored
//   (people bounce cards on the reader).
// Different cards are independent of each other's repeat window.
class TapDebouncer {
 public:
  TapDebouncer(std::int64_t leave_ms = 300, std::int64_t repeat_ms = 1500) : leave_ms_(leave_ms), repeat_ms_(repeat_ms) {}

  // A card was read in this poll. Returns true if this is a new tap.
  bool card_present(const std::string& uid, std::int64_t now);
  // No card was in the field during this poll.
  void field_empty(std::int64_t now);

 private:
  std::int64_t leave_ms_;
  std::int64_t repeat_ms_;
  std::string present_uid_;       // card currently considered in the field
  std::int64_t last_seen_ = 0;    // last poll that saw present_uid_
  std::string last_tap_uid_;
  std::int64_t last_tap_at_ = 0;
  bool have_tap_ = false;
};

}  // namespace orca::nfc
