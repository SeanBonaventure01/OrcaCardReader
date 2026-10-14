#pragma once

// Scenario files: timed scripts of taps, API calls, reboots and expectations, run against a
// simulated costume on a fake clock. See host/scenarios/README.md for the format.

#include <string>
#include <vector>

#include "nlohmann/json.hpp"

namespace orca::sim {

struct ScenarioResult {
  std::vector<nlohmann::json> transcript;  // one JSON object per line of the golden file
  std::vector<std::string> failures;       // failed `expect` steps and script errors
};

ScenarioResult run_scenario(const nlohmann::json& spec);

std::string to_jsonl(const std::vector<nlohmann::json>& lines);
std::vector<nlohmann::json> from_jsonl(const std::string& text);

}  // namespace orca::sim
