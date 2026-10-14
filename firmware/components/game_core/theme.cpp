#include "orca/game/theme.hpp"

namespace orca::game {

ThemeEntry orca_theme(Outcome outcome) {
  using hal::Tone;
  switch (outcome) {
    case Outcome::Accepted:
      return {Tone::Success, "Fare paid", "+{points_text} · Balance {balance}", "accept", "accept_flash", 2500};
    case Outcome::Bonus:
      return {Tone::Bonus, "Bonus fare", "+{points_text} ({mult}x bonus) · Balance {balance}", "bonus", "bonus_chase", 3000};
    case Outcome::Cooldown:
      return {Tone::Warning, "Transfer", "Card already tapped", "reject", "cooldown_pulse", 2500};
    case Outcome::Rejected:
      return {Tone::Error, "Insufficient funds", "", "reject", "reject_flash", 2500};
    case Outcome::Unknown:
      return {Tone::Error, "Card not recognized", "Please see the operator", "reject", "reject_flash", 2500};
    case Outcome::Banned:
      return {Tone::Error, "See customer service", "This card has been suspended", "reject", "reject_flash", 2500};
    case Outcome::Closed:
      return {Tone::Neutral, "Not in service", "Waiting for the next trip", "reject", "reject_flash", 2000};
  }
  return {Tone::Neutral, "", "", "", "", 2000};
}

}  // namespace orca::game
