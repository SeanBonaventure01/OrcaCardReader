#include "orca/game/params.hpp"

#include <algorithm>
#include <cmath>

namespace orca::game {

namespace {

const char* type_name(ParamType t) {
  switch (t) {
    case ParamType::Int: return "int";
    case ParamType::Float: return "float";
    case ParamType::Bool: return "bool";
    case ParamType::Enum: return "enum";
    case ParamType::Duration: return "duration";
    case ParamType::String: return "string";
  }
  return "?";
}

std::string range_error(const ParamSpec& s) {
  auto fmt = [](double v) {
    std::string out = std::to_string(v);
    out.erase(out.find_last_not_of('0') + 1);
    if (!out.empty() && out.back() == '.') out.pop_back();
    return out;
  };
  return s.key + " must be between " + fmt(s.min) + " and " + fmt(s.max);
}

// Returns an error message, or "" if `v` is valid for `s`.
std::string check(const ParamSpec& s, const json& v) {
  switch (s.type) {
    case ParamType::Int:
    case ParamType::Duration: {
      if (!v.is_number_integer() && !(v.is_number_float() && std::floor(v.get<double>()) == v.get<double>()))
        return s.key + " must be an integer";
      const double d = v.get<double>();
      if (d < s.min || d > s.max) return range_error(s);
      return "";
    }
    case ParamType::Float: {
      if (!v.is_number()) return s.key + " must be a number";
      const double d = v.get<double>();
      if (d < s.min || d > s.max) return range_error(s);
      return "";
    }
    case ParamType::Bool:
      return v.is_boolean() ? "" : s.key + " must be true or false";
    case ParamType::Enum:
      if (!v.is_string() || std::find(s.options.begin(), s.options.end(), v.get<std::string>()) == s.options.end())
        return s.key + " must be one of the listed options";
      return "";
    case ParamType::String: {
      if (!v.is_string()) return s.key + " must be text";
      const std::size_t max_len = s.max > 0 ? static_cast<std::size_t>(s.max) : 64;
      return v.get<std::string>().size() <= max_len ? "" : s.key + " is too long";
    }
  }
  return "bad schema";
}

}  // namespace

json schema_to_json(const ParamSchema& schema) {
  json out = json::array();
  for (const auto& s : schema) {
    json j{{"key", s.key}, {"label", s.label}, {"type", type_name(s.type)}, {"default", s.def}};
    if (s.type == ParamType::Int || s.type == ParamType::Float || s.type == ParamType::Duration) {
      j["min"] = s.min;
      j["max"] = s.max;
    }
    if (s.type == ParamType::String) j["max_length"] = s.max > 0 ? s.max : 64;
    if (s.type == ParamType::Enum) j["options"] = s.options;
    if (!s.help.empty()) j["help"] = s.help;
    out.push_back(std::move(j));
  }
  return out;
}

ParamResult validate_params(const ParamSchema& schema, const json& input) {
  ParamResult r;
  if (!input.is_null() && !input.is_object()) {
    r.error = "params must be an object";
    return r;
  }
  const json in = input.is_null() ? json::object() : input;
  for (auto it = in.begin(); it != in.end(); ++it) {
    const bool known = std::any_of(schema.begin(), schema.end(), [&](const ParamSpec& s) { return s.key == it.key(); });
    if (!known) {
      r.error = "unknown param: " + it.key();
      return r;
    }
  }
  r.params = json::object();
  for (const auto& s : schema) {
    const json& v = in.contains(s.key) ? in.at(s.key) : s.def;
    if (auto err = check(s, v); !err.empty()) {
      r.error = err;
      return r;
    }
    // Normalize integral types so 300000.0 is stored as 300000.
    if (s.type == ParamType::Int || s.type == ParamType::Duration)
      r.params[s.key] = static_cast<std::int64_t>(v.get<double>());
    else
      r.params[s.key] = v;
  }
  r.ok = true;
  return r;
}

}  // namespace orca::game
