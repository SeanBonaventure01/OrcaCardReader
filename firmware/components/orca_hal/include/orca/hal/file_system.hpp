#pragma once

#include <optional>
#include <string>

namespace orca::hal {

// Minimal whole-file interface. The device has SD/flash implementations; tests use memory.
class FileSystem {
 public:
  virtual ~FileSystem() = default;
  virtual std::optional<std::string> read(const std::string& path) = 0;
  virtual bool write(const std::string& path, const std::string& data) = 0;
  virtual bool append(const std::string& path, const std::string& data) = 0;
  // Replaces `to` if it exists.
  virtual bool rename(const std::string& from, const std::string& to) = 0;
  virtual bool remove(const std::string& path) = 0;
  virtual bool exists(const std::string& path) = 0;
};

}  // namespace orca::hal
