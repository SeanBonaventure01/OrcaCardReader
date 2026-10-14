#include "orca/game/theme.hpp"

namespace orca::game {

ThemeEntry orca_theme(Outcome outcome) {
  using hal::Tone;
  switch (outcome) {
    case Outcome::Accepted:
      return {Tone::Success, "FARE PAID", "+{points} pts · Balance {balance}", "accept", "accept_flash", 2500};
    case Outcome::Bonus:
      return {Tone::Bonus, "BONUS FARE", "+{points} pts ({mult}x) · Balance {balance}", "bonus", "bonus_chase", 3000};
    case Outcome::Cooldown:
      return {Tone::Warning, "TRANSFER", "Card already tapped", "reject", "cooldown_pulse", 2500};
    case Outcome::Rejected:
      return {Tone::Error, "INSUFFICIENT FUNDS", "", "reject", "reject_flash", 2500};
    case Outcome::Unknown:
      return {Tone::Error, "CARD NOT RECOGNIZED", "Please see the operator", "reject", "reject_flash", 2500};
    case Outcome::Banned:
      return {Tone::Error, "SEE CUSTOMER SERVICE", "This card has been suspended", "reject", "reject_flash", 2500};
    case Outcome::Closed:
      return {Tone::Neutral, "NOT IN SERVICE", "Waiting for the next trip", "reject", "reject_flash", 2000};
  }
  return {Tone::Neutral, "", "", "", "", 2000};
}

}  // namespace orca::game
