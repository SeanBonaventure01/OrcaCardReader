#pragma once

#include <string>
#include <vector>

#include "orca/game/types.hpp"

namespace orca::game {

enum class ParamType { Int, Float, Bool, Enum, Duration, String };

// One configurable game setting. The phone UI renders a form from these, so a new game
// needs no UI changes. Durations are milliseconds.
struct ParamSpec {
  std::string key;
  std::string label;
  ParamType type = ParamType::Int;
  json def;
  double min = 0;
  double max = 0;  // for String: max length (0 = 64)
  std::vector<std::string> options;  // Enum only
  std::string help;
};

using ParamSchema = std::vector<ParamSpec>;

json schema_to_json(const ParamSchema& schema);

struct ParamResult {
  bool ok = false;
  json params;  // every key present, defaults filled in
  std::string error;
};

// Validates `input` (an object or null) against the schema. Unknown keys, wrong types and
// out-of-range values are errors. Missing keys take their defaults.
ParamResult validate_params(const ParamSchema& schema, const json& input);

}  // namespace orca::game
