#pragma once

#include <map>
#include <set>
#include <string>

#include "orca/hal/file_system.hpp"

namespace orca::fakes {

// In-memory file system. `fail_on` makes the named operation+path fail, e.g. {"rename:/s.json.tmp"}
// for testing crash/fault paths.
class MemFileSystem : public hal::FileSystem {
 public:
  std::optional<std::string> read(const std::string& path) override {
    if (fails("read", path)) return std::nullopt;
    auto it = files.find(path);
    if (it == files.end()) return std::nullopt;
    return it->second;
  }
  bool write(const std::string& path, const std::string& data) override {
    if (fails("write", path)) return false;
    files[path] = data;
    return true;
  }
  bool append(const std::string& path, const std::string& data) override {
    if (fails("append", path)) return false;
    files[path] += data;
    return true;
  }
  bool rename(const std::string& from, const std::string& to) override {
    if (fails("rename", from)) return false;
    auto it = files.find(from);
    if (it == files.end()) return false;
    files[to] = it->second;
    files.erase(from);
    return true;
  }
  bool remove(const std::string& path) override { return files.erase(path) > 0; }
  bool exists(const std::string& path) override { return files.count(path) > 0; }

  std::map<std::string, std::string> files;
  std::set<std::string> fail_on;

 private:
  bool fails(const char* op, const std::string& path) const { return fail_on.count(std::string(op) + ":" + path) > 0; }
};

}  // namespace orca::fakes
