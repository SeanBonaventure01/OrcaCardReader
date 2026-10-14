#pragma once

#include <optional>
#include <string>

#include "orca/game/types.hpp"
#include "orca/hal/file_system.hpp"

namespace orca::game {

// Saves JSON snapshots so a crash mid-write never loses the last good copy:
// write <path>.tmp, move <path> to <path>.bak, move <path>.tmp to <path>.
// load() tries <path>, then <path>.bak.
class SnapshotStore {
 public:
  SnapshotStore(hal::FileSystem& fs, std::string path) : fs_(fs), path_(std::move(path)) {}

  bool save(const json& snapshot);
  std::optional<json> load();
  // Which file the last load() used: "main", "backup" or "" if none.
  const std::string& loaded_from() const { return loaded_from_; }

 private:
  std::optional<json> try_load(const std::string& path);

  hal::FileSystem& fs_;
  std::string path_;
  std::string loaded_from_;
};

}  // namespace orca::game
