#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>

#include "orca/hal/file_system.hpp"

namespace orca::sim {

// hal::FileSystem over the host's real files (orca-sim --state).
class PosixFileSystem : public hal::FileSystem {
 public:
  std::optional<std::string> read(const std::string& path) override {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  bool write(const std::string& path, const std::string& data) override {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
    return static_cast<bool>(out);
  }
  bool append(const std::string& path, const std::string& data) override {
    std::ofstream out(path, std::ios::binary | std::ios::app);
    out << data;
    return static_cast<bool>(out);
  }
  bool rename(const std::string& from, const std::string& to) override { return std::rename(from.c_str(), to.c_str()) == 0; }
  bool remove(const std::string& path) override { return std::remove(path.c_str()) == 0; }
  bool exists(const std::string& path) override { return static_cast<bool>(std::ifstream(path)); }
};

}  // namespace orca::sim
