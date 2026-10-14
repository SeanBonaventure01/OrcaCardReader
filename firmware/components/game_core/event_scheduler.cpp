#include "orca/game/event_scheduler.hpp"

#include <algorithm>
#include <cmath>

namespace orca::game {

const char* to_string(EventKind k) {
  switch (k) {
    case EventKind::Multiplier: return "multiplier";
    case EventKind::CooldownWaiver: return "cooldown_waiver";
    case EventKind::Custom: return "custom";
  }
  return "?";
}

std::optional<EventKind> event_kind_from_string(const std::string& s) {
  if (s == "multiplier") return EventKind::Multiplier;
  if (s == "cooldown_waiver") return EventKind::CooldownWaiver;
  if (s == "custom") return EventKind::Custom;
  return std::nullopt;
}

void to_json(json& j, const EventWindow& e) {
  j = json{{"id", e.id},       {"kind", to_string(e.kind)}, {"name", e.name},  {"multiplier", e.multiplier},
           {"start", e.start}, {"end", e.end},              {"tag", e.tag},    {"random", e.random}};
}

void from_json(const json& j, EventWindow& e) {
  e.id = j.at("id").get<int>();
  e.kind = event_kind_from_string(j.at("kind").get<std::string>()).value_or(EventKind::Custom);
  e.name = j.value("name", "");
  e.multiplier = j.value("multiplier", 1.0);
  e.start = j.at("start").get<TimeMs>();
  e.end = j.at("end").get<TimeMs>();
  e.tag = j.value("tag", "");
  e.random = j.value("random", false);
}

void EventScheduler::configure_random(const RandomSchedule& s, TimeMs now, hal::Random& rng) {
  schedule_ = s;
  schedule_.jitter = std::clamp(schedule_.jitter, 0.0, 1.0);
  next_random_ = s.enabled ? std::optional<TimeMs>(now + sample_interval(rng)) : std::nullopt;
}

void EventScheduler::disable_random() {
  schedule_.enabled = false;
  next_random_.reset();
}

TimeMs EventScheduler::sample_interval(hal::Random& rng) const {
  const double factor = 1.0 + schedule_.jitter * (2.0 * rng.unit() - 1.0);
  return std::max<TimeMs>(1000, static_cast<TimeMs>(std::llround(schedule_.mean_interval * factor)));
}

EventWindow EventScheduler::trigger(EventKind kind, const std::string& name, TimeMs duration, double multiplier,
                                    TimeMs now, const std::string& tag) {
  EventWindow w;
  w.id = next_id_++;
  w.kind = kind;
  w.name = name;
  w.multiplier = kind == EventKind::Multiplier ? multiplier : 1.0;
  w.start = now;
  w.end = now + std::max<TimeMs>(duration, 1);
  w.tag = tag;
  windows_.push_back(w);
  return w;
}

void EventScheduler::end_all(TimeMs now) {
  for (auto& w : windows_) w.end = std::min(w.end, now);
}

void EventScheduler::clear() {
  windows_.clear();
  disable_random();
}

EventScheduler::Changes EventScheduler::tick(TimeMs now, hal::Random& rng) {
  Changes ch;
  for (auto it = windows_.begin(); it != windows_.end();) {
    if (now >= it->end) {
      ch.ended.push_back(*it);
      it = windows_.erase(it);
    } else {
      ++it;
    }
  }
  if (schedule_.enabled && next_random_ && now >= *next_random_) {
    const bool random_active =
        std::any_of(windows_.begin(), windows_.end(), [](const EventWindow& w) { return w.random; });
    if (random_active) {
      // Wait for the current random window to finish, then start a fresh interval.
      TimeMs latest_end = now;
      for (const auto& w : windows_)
        if (w.random) latest_end = std::max(latest_end, w.end);
      next_random_ = latest_end + sample_interval(rng);
    } else {
      EventWindow w = trigger(schedule_.kind, schedule_.name, schedule_.duration, schedule_.multiplier, now,
                              schedule_.tag);
      windows_.back().random = true;
      w.random = true;
      ch.started.push_back(w);
      next_random_ = w.end + sample_interval(rng);
    }
  }
  return ch;
}

std::vector<EventWindow> EventScheduler::active(TimeMs now) const {
  std::vector<EventWindow> out;
  for (const auto& w : windows_)
    if (w.active_at(now)) out.push_back(w);
  return out;
}

double EventScheduler::multiplier(TimeMs now) const {
  double m = 1.0;
  for (const auto& w : windows_)
    if (w.kind == EventKind::Multiplier && w.active_at(now)) m *= w.multiplier;
  return m;
}

bool EventScheduler::cooldown_waived(TimeMs now) const {
  return std::any_of(windows_.begin(), windows_.end(),
                     [now](const EventWindow& w) { return w.kind == EventKind::CooldownWaiver && w.active_at(now); });
}

json EventScheduler::save() const {
  return json{{"windows", windows_},
              {"next_id", next_id_},
              {"next_random", next_random_ ? json(*next_random_) : json(nullptr)},
              {"schedule",
               {{"enabled", schedule_.enabled},
                {"mean_interval", schedule_.mean_interval},
                {"duration", schedule_.duration},
                {"jitter", schedule_.jitter},
                {"kind", to_string(schedule_.kind)},
                {"multiplier", schedule_.multiplier},
                {"name", schedule_.name},
                {"tag", schedule_.tag}}}};
}

void EventScheduler::restore(const json& j) {
  windows_ = j.at("windows").get<std::vector<EventWindow>>();
  next_id_ = j.value("next_id", 1);
  const auto& nr = j.at("next_random");
  next_random_ = nr.is_null() ? std::nullopt : std::optional<TimeMs>(nr.get<TimeMs>());
  const auto& s = j.at("schedule");
  schedule_.enabled = s.value("enabled", false);
  schedule_.mean_interval = s.value("mean_interval", hal::minutes(20));
  schedule_.duration = s.value("duration", hal::minutes(2));
  schedule_.jitter = s.value("jitter", 0.5);
  schedule_.kind = event_kind_from_string(s.value("kind", "multiplier")).value_or(EventKind::Multiplier);
  schedule_.multiplier = s.value("multiplier", 2.0);
  schedule_.name = s.value("name", "RUSH HOUR");
  schedule_.tag = s.value("tag", "");
}

}  // namespace orca::game
