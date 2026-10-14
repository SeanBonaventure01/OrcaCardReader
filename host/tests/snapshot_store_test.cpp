#include <gtest/gtest.h>

#include "orca/fakes/mem_file_system.hpp"
#include "orca/game/snapshot_store.hpp"

using orca::fakes::MemFileSystem;
using orca::game::json;
using orca::game::SnapshotStore;

TEST(SnapshotStore, SaveThenLoad) {
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  ASSERT_TRUE(store.save({{"n", 1}}));
  auto j = store.load();
  ASSERT_TRUE(j);
  EXPECT_EQ((*j)["n"], 1);
  EXPECT_EQ(store.loaded_from(), "main");
  EXPECT_FALSE(fs.exists("/s.json.tmp"));
}

TEST(SnapshotStore, SecondSaveKeepsPreviousAsBackup) {
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  store.save({{"n", 1}});
  store.save({{"n", 2}});
  EXPECT_EQ(json::parse(fs.files["/s.json"])["n"], 2);
  EXPECT_EQ(json::parse(fs.files["/s.json.bak"])["n"], 1);
}

TEST(SnapshotStore, CorruptMainFallsBackToBackup) {
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  store.save({{"n", 1}});
  store.save({{"n", 2}});
  fs.files["/s.json"] = "{\"n\": 2, trunc";
  auto j = store.load();
  ASSERT_TRUE(j);
  EXPECT_EQ((*j)["n"], 1);
  EXPECT_EQ(store.loaded_from(), "backup");
}

TEST(SnapshotStore, CrashBetweenRenamesStillLoadsBackup) {
  // Simulate power loss after main -> bak but before tmp -> main.
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  store.save({{"n", 1}});
  fs.fail_on.insert("rename:/s.json.tmp");
  EXPECT_FALSE(store.save({{"n", 2}}));
  EXPECT_FALSE(fs.exists("/s.json"));
  auto j = store.load();
  ASSERT_TRUE(j);
  EXPECT_EQ((*j)["n"], 1);
}

TEST(SnapshotStore, FailedWriteLeavesExistingSnapshot) {
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  store.save({{"n", 1}});
  fs.fail_on.insert("write:/s.json.tmp");
  EXPECT_FALSE(store.save({{"n", 2}}));
  EXPECT_EQ((*store.load())["n"], 1);
}

TEST(SnapshotStore, NothingSaved) {
  MemFileSystem fs;
  SnapshotStore store(fs, "/s.json");
  EXPECT_FALSE(store.load());
  EXPECT_EQ(store.loaded_from(), "");
  fs.files["/s.json"] = "[1,2,3]";  // valid JSON but not an object
  EXPECT_FALSE(store.load());
}
