// Runs every host/scenarios/*.scenario.json and compares with its golden transcript.
// After an intentional behaviour change: make scenarios-update (then review the diff).

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "scenario.hpp"

#ifndef ORCA_SCENARIOS_DIR
#error "ORCA_SCENARIOS_DIR must be defined by the build"
#endif

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::string read(const fs::path& p) {
  std::ifstream in(p);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

std::vector<std::string> scenario_names() {
  std::vector<std::string> names;
  for (const auto& e : fs::directory_iterator(ORCA_SCENARIOS_DIR)) {
    const std::string f = e.path().filename().string();
    const std::string suffix = ".scenario.json";
    if (f.size() > suffix.size() && f.compare(f.size() - suffix.size(), suffix.size(), suffix) == 0)
      names.push_back(f.substr(0, f.size() - suffix.size()));
  }
  std::sort(names.begin(), names.end());
  return names;
}

class Scenarios : public ::testing::TestWithParam<std::string> {};

TEST_P(Scenarios, ExpectationsPassAndTranscriptMatchesGolden) {
  const fs::path dir(ORCA_SCENARIOS_DIR);
  const auto result = orca::sim::run_scenario(json::parse(read(dir / (GetParam() + ".scenario.json"))));
  for (const auto& f : result.failures) ADD_FAILURE() << f;

  const fs::path golden = dir / (GetParam() + ".expected.jsonl");
  ASSERT_TRUE(fs::exists(golden)) << "no golden file; run: make scenarios-update";
  const auto expected = orca::sim::from_jsonl(read(golden));
  const auto& actual = result.transcript;
  for (std::size_t i = 0; i < std::min(expected.size(), actual.size()); ++i)
    ASSERT_EQ(actual[i], expected[i]) << "first difference at transcript line " << i + 1;
  ASSERT_EQ(actual.size(), expected.size()) << "transcript length differs";
}

INSTANTIATE_TEST_SUITE_P(Golden, Scenarios, ::testing::ValuesIn(scenario_names()),
                         [](const auto& info) { return info.param; });

TEST(ScenarioRunner, ReportsFailedExpectations) {
  auto r = orca::sim::run_scenario(json::parse(R"({
    "game": "fare_paid", "players": [{"name": "A"}],
    "steps": [{"at": "0s", "tap": "A", "expect": {"score": {"A": 99}, "outcome": "cooldown"}}]
  })"));
  ASSERT_EQ(r.failures.size(), 2u);
  EXPECT_NE(r.failures[0].find("score of A = 99, got 1"), std::string::npos) << r.failures[0];
  EXPECT_EQ(r.transcript.back()["ok"], false);
}

TEST(ScenarioRunner, ReportsScriptErrors) {
  auto r = orca::sim::run_scenario(json::parse(R"({"steps": [{"at": "1m"}, {"at": "30s"}]})"));
  ASSERT_EQ(r.failures.size(), 1u);
  EXPECT_NE(r.failures[0].find("backwards"), std::string::npos);
  r = orca::sim::run_scenario(json::parse(R"({"game": "nope"})"));
  ASSERT_EQ(r.failures.size(), 1u);
  EXPECT_NE(r.failures[0].find("404"), std::string::npos);
}

}  // namespace
