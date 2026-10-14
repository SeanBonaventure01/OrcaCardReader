#include "orca/game/snapshot_store.hpp"

namespace orca::game {

bool SnapshotStore::save(const json& snapshot) {
  const std::string tmp = path_ + ".tmp";
  const std::string bak = path_ + ".bak";
  if (!fs_.write(tmp, snapshot.dump())) return false;
  if (fs_.exists(path_) && !fs_.rename(path_, bak)) return false;
  return fs_.rename(tmp, path_);
}

std::optional<json> SnapshotStore::try_load(const std::string& path) {
  auto data = fs_.read(path);
  if (!data) return std::nullopt;
  json j = json::parse(*data, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) return std::nullopt;
  return j;
}

std::optional<json> SnapshotStore::load() {
  loaded_from_.clear();
  if (auto j = try_load(path_)) {
    loaded_from_ = "main";
    return j;
  }
  if (auto j = try_load(path_ + ".bak")) {
    loaded_from_ = "backup";
    return j;
  }
  return std::nullopt;
}

}  // namespace orca::game
