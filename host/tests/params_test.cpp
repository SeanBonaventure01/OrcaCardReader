#include <gtest/gtest.h>

#include "orca/game/params.hpp"

using namespace orca::game;

namespace {

ParamSchema schema() {
  return {
      {"cooldown", "Cooldown", ParamType::Duration, 300000, 0, 3600000, {}, ""},
      {"points", "Points", ParamType::Int, 1, 1, 100, {}, ""},
      {"mult", "Multiplier", ParamType::Float, 2.0, 1, 10, {}, ""},
      {"teams", "Teams on", ParamType::Bool, false, 0, 0, {}, ""},
      {"mode", "Mode", ParamType::Enum, "fast", 0, 0, {"fast", "slow"}, ""},
      {"label", "Label", ParamType::String, "ORCA", 0, 8, {}, ""},
  };
}

}  // namespace

TEST(Params, NullOrEmptyGivesDefaults) {
  for (const json& in : {json(nullptr), json::object()}) {
    auto r = validate_params(schema(), in);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.params["cooldown"], 300000);
    EXPECT_EQ(r.params["mode"], "fast");
    EXPECT_EQ(r.params["teams"], false);
  }
}

TEST(Params, AcceptsValidOverrides) {
  auto r = validate_params(schema(), {{"cooldown", 60000}, {"mult", 1.5}, {"mode", "slow"}, {"label", "BUS"}});
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.params["cooldown"], 60000);
  EXPECT_DOUBLE_EQ(r.params["mult"].get<double>(), 1.5);
  EXPECT_EQ(r.params["points"], 1);
}

TEST(Params, IntegralFloatsNormalizeToIntegers) {
  auto r = validate_params(schema(), {{"points", 5.0}});
  ASSERT_TRUE(r.ok);
  EXPECT_TRUE(r.params["points"].is_number_integer());
}

struct BadCase {
  json input;
  const char* expect_in_error;
};

class ParamsRejects : public ::testing::TestWithParam<BadCase> {};

TEST_P(ParamsRejects, WithMessage) {
  auto r = validate_params(schema(), GetParam().input);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find(GetParam().expect_in_error), std::string::npos) << r.error;
}

INSTANTIATE_TEST_SUITE_P(
    Cases, ParamsRejects,
    ::testing::Values(BadCase{json::array(), "must be an object"}, BadCase{{{"bogus", 1}}, "unknown param: bogus"},
                      BadCase{{{"points", 0}}, "between 1 and 100"}, BadCase{{{"points", 101}}, "between 1 and 100"},
                      BadCase{{{"points", 1.5}}, "integer"}, BadCase{{{"points", "3"}}, "integer"},
                      BadCase{{{"cooldown", -1}}, "between"}, BadCase{{{"mult", 10.5}}, "between 1 and 10"},
                      BadCase{{{"mult", "x"}}, "number"}, BadCase{{{"teams", 1}}, "true or false"},
                      BadCase{{{"mode", "medium"}}, "one of"}, BadCase{{{"label", "TOOLONGNAME"}}, "too long"},
                      BadCase{{{"label", 5}}, "text"}));

TEST(Params, SchemaJsonHasUiFields) {
  json j = schema_to_json(schema());
  ASSERT_EQ(j.size(), 6u);
  EXPECT_EQ(j[0]["type"], "duration");
  EXPECT_EQ(j[0]["max"], 3600000);
  EXPECT_EQ(j[4]["options"], json({"fast", "slow"}));
  EXPECT_EQ(j[5]["max_length"], 8);
  EXPECT_FALSE(j[3].contains("min"));
}
