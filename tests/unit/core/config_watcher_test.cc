// Copyright 2026 Loong AI NVR Project

#include "core/config/config_watcher.h"

#include "core/config/config_manager.h"
#include "core/config/hot_reload_manager.h"
#include "gtest/gtest.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace loong::core {
namespace {

class ConfigWatcherTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ =
        "/tmp/loong_watcher_test_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_dir_);
    test_file_ = test_dir_ + "/test.json";

    // Create initial file
    std::ofstream f(test_file_);
    f << R"({"key": "original"})";
    f.close();
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  std::string test_dir_;
  std::string test_file_;
};

TEST_F(ConfigWatcherTest, ConstructAndDestroy) {
  ConfigWatcher watcher;
  EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(ConfigWatcherTest, WatchNonexistentFile) {
  ConfigWatcher watcher;
  EXPECT_FALSE(watcher.Watch("/nonexistent/path/config.json"));
  EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(ConfigWatcherTest, WatchValidFile) {
  ConfigWatcher watcher;
  EXPECT_TRUE(watcher.Watch(test_file_));
  EXPECT_TRUE(watcher.IsWatching());
  watcher.Stop();
  EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(ConfigWatcherTest, StopIdempotent) {
  ConfigWatcher watcher;
  watcher.Stop();  // no crash on double stop
  EXPECT_TRUE(watcher.Watch(test_file_));
  watcher.Stop();
  watcher.Stop();
  EXPECT_FALSE(watcher.IsWatching());
}

TEST_F(ConfigWatcherTest, DetectFileChange) {
  ConfigWatcher watcher;
  std::atomic<int> change_count{0};
  watcher.OnChange([&](const std::string&) { change_count.fetch_add(1); });

  ASSERT_TRUE(watcher.Watch(test_file_));

  // Give watcher time to set up
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // Modify file
  {
    std::ofstream f(test_file_);
    f << R"({"key": "modified"})";
  }

  // Wait for detection (up to 2s)
  for (int i = 0; i < 40 && change_count.load() == 0; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  watcher.Stop();
  EXPECT_GT(change_count.load(), 0);
}

// ---- HotReloadManager tests ----

class HotReloadManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ =
        "/tmp/loong_hotreload_test_" +
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(test_dir_);
    test_file_ = test_dir_ + "/config.json";

    nlohmann::json cfg = {
        {"ai", {{"backend", "opencv_dnn"}, {"confidence", 0.5}}},
        {"hls", {{"segment_duration_ms", 2000}, {"max_segments", 5}}},
    };
    std::ofstream f(test_file_);
    f << cfg.dump(2);
    f.close();

    // Load into ConfigManager
    ConfigManager::Instance().LoadFromFile(test_file_);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  std::string test_dir_;
  std::string test_file_;
};

TEST_F(HotReloadManagerTest, StartAndStop) {
  HotReloadManager mgr;
  EXPECT_TRUE(mgr.Start(test_file_));
  mgr.Stop();
}

TEST_F(HotReloadManagerTest, StartWithEmptyPath) {
  HotReloadManager mgr;
  EXPECT_FALSE(mgr.Start(""));
}

TEST_F(HotReloadManagerTest, GetCurrentConfig) {
  HotReloadManager mgr;
  mgr.Start(test_file_);

  auto cfg = mgr.GetCurrentConfig();
  EXPECT_TRUE(cfg.contains("ai"));
  EXPECT_TRUE(cfg.contains("hls"));
  EXPECT_EQ(cfg["ai"]["backend"], "opencv_dnn");
  mgr.Stop();
}

TEST_F(HotReloadManagerTest, ApplyPatchUpdatesConfig) {
  HotReloadManager mgr;
  mgr.Start(test_file_);

  nlohmann::json patch = {
      {"ai", {{"backend", "onnxruntime"}, {"confidence", 0.7}}}};
  EXPECT_TRUE(mgr.ApplyPatch(patch));

  auto cfg = mgr.GetCurrentConfig();
  EXPECT_EQ(cfg["ai"]["backend"], "onnxruntime");
  EXPECT_DOUBLE_EQ(cfg["ai"]["confidence"].get<double>(), 0.7);
  mgr.Stop();
}

TEST_F(HotReloadManagerTest, SectionHandlerInvoked) {
  HotReloadManager mgr;
  mgr.Start(test_file_);

  std::string received_section;
  nlohmann::json received_cfg;
  mgr.RegisterSection(
      "ai", [&](const std::string& section, const nlohmann::json& cfg) {
        received_section = section;
        received_cfg = cfg;
      });

  nlohmann::json patch = {{"ai", {{"backend", "tensorrt"}}}};
  mgr.ApplyPatch(patch);

  EXPECT_EQ(received_section, "ai");
  EXPECT_EQ(received_cfg["backend"], "tensorrt");
  mgr.Stop();
}

TEST_F(HotReloadManagerTest, BroadcastCallbackInvoked) {
  HotReloadManager mgr;
  mgr.Start(test_file_);

  std::vector<std::string> broadcast_sections;
  mgr.SetBroadcastCallback(
      [&](const std::string& section, const nlohmann::json&) {
        broadcast_sections.push_back(section);
      });

  nlohmann::json patch = {
      {"ai", {{"backend", "onnxruntime"}}},
      {"hls", {{"max_segments", 10}}},
  };
  mgr.ApplyPatch(patch);

  EXPECT_EQ(broadcast_sections.size(), 2u);
  mgr.Stop();
}

TEST_F(HotReloadManagerTest, UnchangedSectionNotNotified) {
  HotReloadManager mgr;
  mgr.Start(test_file_);

  int ai_calls = 0;
  mgr.RegisterSection(
      "ai", [&](const std::string&, const nlohmann::json&) { ++ai_calls; });

  // Patch only hls, not ai
  nlohmann::json patch = {{"hls", {{"max_segments", 10}}}};
  mgr.ApplyPatch(patch);

  EXPECT_EQ(ai_calls, 0);
  mgr.Stop();
}

}  // namespace
}  // namespace loong::core
