#pragma once

#include <optional>
#include <string>
#include <vector>

#include "orca/game/types.hpp"
#include "orca/hal/random.hpp"

namespace orca::game {

enum class EventKind {
  Multiplier,      // scale points by `multiplier`
  CooldownWaiver,  // games should ignore cooldowns
  Custom,          // game-defined; passed to Game::on_event with `tag`
};

const char* to_string(EventKind k);
std::optional<EventKind> event_kind_from_string(const std::string& s);

struct EventWindow {
  int id = 0;
  EventKind kind = EventKind::Multiplier;
  std::string name;
  double multiplier = 1.0;
  TimeMs start = 0;
  TimeMs end = 0;
  std::string tag;
  bool random = false;  // started by the random schedule rather than by hand

  bool active_at(TimeMs t) const { return t >= start && t < end; }
};

void to_json(json& j, const EventWindow& e);
void from_json(const json& j, EventWindow& e);

// Random windows: on average one every `mean_interval`, uniformly jittered by
// +/- `jitter` (0..1) of the interval. Random windows never overlap each other.
struct RandomSchedule {
  bool enabled = false;
  TimeMs mean_interval = hal::minutes(20);
  TimeMs duration = hal::minutes(2);
  double jitter = 0.5;
  EventKind kind = EventKind::Multiplier;
  double multiplier = 2.0;
  std::string name = "Rush hour";
  std::string tag;
};

class EventScheduler {
 public:
  struct Changes {
    std::vector<EventWindow> started;
    std::vector<EventWindow> ended;
    bool empty() const { return started.empty() && ended.empty(); }
  };

  void configure_random(const RandomSchedule& s, TimeMs now, hal::Random& rng);
  void disable_random();
  const RandomSchedule& random_schedule() const { return schedule_; }
  std::optional<TimeMs> next_random_at() const { return next_random_; }

  // Starts a window immediately. It is reported by the caller, not by tick().
  EventWindow trigger(EventKind kind, const std::string& name, TimeMs duration, double multiplier, TimeMs now,
                      const std::string& tag = "");
  // Ends every active window now; they are reported as ended by the next tick().
  void end_all(TimeMs now);
  // Drops all windows and the random schedule without reporting anything.
  void clear();

  // Ends expired windows and starts due random windows.
  Changes tick(TimeMs now, hal::Random& rng);

  std::vector<EventWindow> active(TimeMs now) const;
  double multiplier(TimeMs now) const;  // product of active Multiplier windows
  bool cooldown_waived(TimeMs now) const;

  json save() const;
  void restore(const json& j);

 private:
  TimeMs sample_interval(hal::Random& rng) const;

  RandomSchedule schedule_;
  std::optional<TimeMs> next_random_;
  std::vector<EventWindow> windows_;  // active, not yet reported as ended
  int next_id_ = 1;
};

}  // namespace orca::game
