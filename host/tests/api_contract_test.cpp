// Replays the shared API fixtures (host/tests/api_fixtures/*.json) against the real API.
// The phone UI's tests use the same files as mocks; see the README there.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "orca/fakes/fake_clock.hpp"
#include "orca/fakes/seeded_random.hpp"
#include "world.hpp"

#ifndef ORCA_API_FIXTURES_DIR
#error "ORCA_API_FIXTURES_DIR must be defined by the build"
#endif

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

// Returns "" on match, else a description of the first mismatch.
std::string match(const json& pattern, const json& actual, const std::string& where) {
  if (pattern.is_string()) {
    const auto& p = pattern.get_ref<const std::string&>();
    if (p == "<any>") return "";
    if (p == "<number>") return actual.is_number() ? "" : where + ": expected a number, got " + actual.dump();
    if (p == "<string>") return actual.is_string() ? "" : where + ": expected a string, got " + actual.dump();
    if (p == "<bool>") return actual.is_boolean() ? "" : where + ": expected a bool, got " + actual.dump();
    if (p == "<array>") return actual.is_array() ? "" : where + ": expected an array, got " + actual.dump();
    if (p == "<object>") return actual.is_object() ? "" : where + ": expected an object, got " + actual.dump();
    if (p == "<null>") return actual.is_null() ? "" : where + ": expected null, got " + actual.dump();
  }
  if (pattern.is_object()) {
    if (!actual.is_object()) return where + ": expected an object, got " + actual.dump();
    for (auto it = pattern.begin(); it != pattern.end(); ++it) {
      if (!actual.contains(it.key())) return where + "." + it.key() + ": missing";
      if (auto m = match(*it, actual.at(it.key()), where + "." + it.key()); !m.empty()) return m;
    }
    return "";
  }
  if (pattern.is_array()) {
    if (!actual.is_array() || actual.size() != pattern.size())
      return where + ": expected array of " + std::to_string(pattern.size()) + ", got " + actual.dump();
    for (std::size_t i = 0; i < pattern.size(); ++i)
      if (auto m = match(pattern[i], actual[i], where + "[" + std::to_string(i) + "]"); !m.empty()) return m;
    return "";
  }
  if (pattern.is_number() && actual.is_number())  // 3 == 3.0
    return pattern.get<double>() == actual.get<double>() ? "" : where + ": expected " + pattern.dump() + ", got " + actual.dump();
  return pattern == actual ? "" : where + ": expected " + pattern.dump() + ", got " + actual.dump();
}

struct FixtureCase {
  std::string file;
  json spec;
};

// Keeps googletest from dumping raw bytes into the test list (which confuses ctest discovery).
void PrintTo(const FixtureCase& c, std::ostream* os) { *os << c.file << ":" << c.spec.value("name", "?"); }

std::vector<FixtureCase> load_cases() {
  std::vector<FixtureCase> cases;
  std::vector<fs::path> files;
  for (const auto& entry : fs::directory_iterator(ORCA_API_FIXTURES_DIR))
    if (entry.path().extension() == ".json") files.push_back(entry.path());
  std::sort(files.begin(), files.end());
  for (const auto& path : files) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    for (const auto& c : json::parse(ss.str())) cases.push_back({path.filename().string(), c});
  }
  return cases;
}

orca::api::ApiResponse send(orca::sim::World& world, const json& step) {
  return world.call(step.at("method"), step.at("path"), step.value("body", json(nullptr)));
}

class ApiContract : public ::testing::TestWithParam<FixtureCase> {};

TEST_P(ApiContract, MatchesFixture) {
  const json& spec = GetParam().spec;
  orca::fakes::FakeClock clock(1000);
  orca::fakes::SeededRandom rng(1);
  orca::sim::World world(clock, rng);

  for (const auto& step : spec.value("setup", json::array())) {
    auto r = send(world, step);
    ASSERT_GE(r.status, 200) << "setup " << step.dump() << " -> " << r.body;
    ASSERT_LT(r.status, 300) << "setup " << step.dump() << " -> " << r.body;
  }
  auto r = send(world, spec.at("request"));
  const json& expect = spec.at("response");
  EXPECT_EQ(r.status, expect.at("status").get<int>()) << r.body;
  if (expect.contains("body")) {
    const json actual = json::parse(r.body);
    const std::string m = match(expect.at("body"), actual, "body");
    EXPECT_TRUE(m.empty()) << m << "\nfull response: " << actual.dump(2);
  }
}

std::string case_name(const ::testing::TestParamInfo<FixtureCase>& info) { return info.param.spec.at("name"); }

INSTANTIATE_TEST_SUITE_P(Fixtures, ApiContract, ::testing::ValuesIn(load_cases()), case_name);

TEST(ApiContractMatcher, Placeholders) {
  EXPECT_EQ(match("<number>", 3, "x"), "");
  EXPECT_NE(match("<number>", "3", "x"), "");
  EXPECT_EQ(match(json{{"a", 1}}, json{{"a", 1}, {"b", 2}}, "x"), "");
  EXPECT_NE(match(json{{"c", 1}}, json{{"a", 1}}, "x"), "");
  EXPECT_NE(match(json::array({1}), json::array({1, 2}), "x"), "");
  EXPECT_EQ(match(3, 3.0, "x"), "");
}

TEST(ApiContractFixtures, NamesAreUnique) {
  std::set<std::string> names;
  for (const auto& c : load_cases()) EXPECT_TRUE(names.insert(c.spec.at("name")).second) << c.spec.at("name");
}

}  // namespace
