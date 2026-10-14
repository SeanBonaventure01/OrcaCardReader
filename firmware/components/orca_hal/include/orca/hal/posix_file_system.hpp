#pragma once

#include <cstdio>
#include <fstream>
#include <sstream>

#include "orca/hal/file_system.hpp"

namespace orca::hal {

// FileSystem over C++ streams, rooted at a directory. Works on the host and on the device
// (ESP-IDF's VFS routes it to LittleFS or FAT), so it's shared by both.
class PosixFileSystem : public FileSystem {
 public:
  explicit PosixFileSystem(std::string root = "") : root_(std::move(root)) {}

  std::optional<std::string> read(const std::string& path) override {
    std::ifstream in(full(path), std::ios::binary);
    if (!in) return std::nullopt;
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
  }
  bool write(const std::string& path, const std::string& data) override {
    std::ofstream out(full(path), std::ios::binary | std::ios::trunc);
    out << data;
    out.flush();
    return static_cast<bool>(out);
  }
  bool append(const std::string& path, const std::string& data) override {
    std::ofstream out(full(path), std::ios::binary | std::ios::app);
    out << data;
    out.flush();
    return static_cast<bool>(out);
  }
  // FAT refuses to rename onto an existing file, so remove the target first.
  bool rename(const std::string& from, const std::string& to) override {
    if (!exists(from)) return false;
    std::remove(full(to).c_str());
    return std::rename(full(from).c_str(), full(to).c_str()) == 0;
  }
  bool remove(const std::string& path) override { return std::remove(full(path).c_str()) == 0; }
  bool exists(const std::string& path) override { return static_cast<bool>(std::ifstream(full(path))); }

 private:
  std::string full(const std::string& path) const { return root_ + path; }
  std::string root_;
};

}  // namespace orca::hal
