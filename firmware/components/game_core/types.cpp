#include "orca/game/types.hpp"

#include <cctype>
#include <cstdio>

namespace orca::game {

const char* to_string(Outcome o) {
  switch (o) {
    case Outcome::Accepted: return "accepted";
    case Outcome::Bonus: return "bonus";
    case Outcome::Cooldown: return "cooldown";
    case Outcome::Rejected: return "rejected";
    case Outcome::Unknown: return "unknown";
    case Outcome::Banned: return "banned";
    case Outcome::Closed: return "closed";
  }
  return "?";
}

void to_json(json& j, const Player& p) {
  j = json{{"no", p.number}, {"uid", p.uid}, {"name", p.name}, {"team", p.team}, {"banned", p.banned}};
}

void from_json(const json& j, Player& p) {
  p.number = j.at("no").get<int>();
  p.uid = j.at("uid").get<std::string>();
  p.name = j.value("name", "");
  p.team = j.value("team", "");
  p.banned = j.value("banned", false);
}

std::string format_mmss(TimeMs ms) {
  if (ms < 0) ms = 0;
  const TimeMs total_s = (ms + 999) / 1000;  // round up so "0:00" only shows when done
  char buf[24];
  std::snprintf(buf, sizeof buf, "%lld:%02lld", static_cast<long long>(total_s / 60),
                static_cast<long long>(total_s % 60));
  return buf;
}

TimeMs parse_duration(const std::string& s) {
  if (s.empty()) return -1;
  TimeMs total = 0;
  std::size_t i = 0;
  bool any = false;
  while (i < s.size()) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
    TimeMs n = 0;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      n = n * 10 + (s[i] - '0');
      if (n > 1'000'000'000'000LL) return -1;
      ++i;
    }
    std::string unit;
    while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) unit.push_back(s[i++]);
    if (unit.empty()) {
      if (any || i != s.size()) return -1;  // bare number must be the whole string
      return n;
    }
    if (unit == "ms") total += n;
    else if (unit == "s") total += n * 1000;
    else if (unit == "m") total += n * 60'000;
    else if (unit == "h") total += n * 3'600'000;
    else return -1;
    any = true;
  }
  return any ? total : -1;
}

}  // namespace orca::game
