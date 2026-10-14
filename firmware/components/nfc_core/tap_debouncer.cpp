#include "orca/nfc/tap_debouncer.hpp"

namespace orca::nfc {

bool TapDebouncer::card_present(const std::string& uid, std::int64_t now) {
  if (uid == present_uid_) {
    last_seen_ = now;
    return false;  // still held in the field
  }
  // A different card (or the first card after the field emptied).
  present_uid_ = uid;
  last_seen_ = now;
  if (have_tap_ && uid == last_tap_uid_ && now - last_tap_at_ < repeat_ms_) return false;
  have_tap_ = true;
  last_tap_uid_ = uid;
  last_tap_at_ = now;
  return true;
}

void TapDebouncer::field_empty(std::int64_t now) {
  // Single missed polls happen while a card is held; only forget it after `leave_ms`.
  if (!present_uid_.empty() && now - last_seen_ >= leave_ms_) present_uid_.clear();
}

}  // namespace orca::nfc
