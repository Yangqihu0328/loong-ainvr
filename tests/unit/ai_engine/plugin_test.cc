// Copyright 2026 Loong AI NVR Project
// Unit tests for AI Model Plugin System (FEAT-6.1)

#include "ai_engine/plugin/model_plugin.h"
#include "ai_engine/plugin/plugin_manager.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <thread>

namespace loong::ai_engine {
namespace {

// ============================================================
// PluginInfo Tests
// ============================================================

TEST(PluginInfoTest, DefaultValues) {
  PluginInfo info;
  EXPECT_TRUE(info.name.empty());
  EXPECT_TRUE(info.version.empty());
  EXPECT_TRUE(info.model_family.empty());
  EXPECT_EQ(info.api_version, 0);
}

// ============================================================
// PluginManager Tests
// ============================================================

class PluginManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "/tmp/loong_plugin_test_" +
                std::to_string(
                    std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::filesystem::create_directories(test_dir_);
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  std::string test_dir_;
};

TEST_F(PluginManagerTest, DefaultState) {
  PluginManager mgr;
  EXPECT_EQ(mgr.Count(), 0);
  EXPECT_TRUE(mgr.GetPluginDir().empty());
  EXPECT_TRUE(mgr.ListPlugins().empty());
}

TEST_F(PluginManagerTest, SetPluginDir) {
  PluginManager mgr;
  mgr.SetPluginDir(test_dir_);
  EXPECT_EQ(mgr.GetPluginDir(), test_dir_);
}

TEST_F(PluginManagerTest, ScanEmptyDir) {
  PluginManager mgr;
  mgr.SetPluginDir(test_dir_);
  int loaded = mgr.ScanAndLoad();
  EXPECT_EQ(loaded, 0);
  EXPECT_EQ(mgr.Count(), 0);
}

TEST_F(PluginManagerTest, ScanNonexistentDir) {
  PluginManager mgr;
  mgr.SetPluginDir("/tmp/nonexistent_plugin_dir_xyz123");
  int loaded = mgr.ScanAndLoad();
  EXPECT_EQ(loaded, 0);
}

TEST_F(PluginManagerTest, ScanEmptyPluginDir) {
  PluginManager mgr;
  int loaded = mgr.ScanAndLoad();
  EXPECT_EQ(loaded, 0);
}

TEST_F(PluginManagerTest, LoadInvalidSoFile) {
  // Create a fake .so file
  std::string fake_so = test_dir_ + "/fake_plugin.so";
  std::ofstream ofs(fake_so);
  ofs << "not a shared library";
  ofs.close();

  PluginManager mgr;
  bool loaded = mgr.LoadPlugin(fake_so);
  EXPECT_FALSE(loaded);
  EXPECT_EQ(mgr.Count(), 0);
}

TEST_F(PluginManagerTest, LoadNonexistentFile) {
  PluginManager mgr;
  bool loaded = mgr.LoadPlugin("/tmp/does_not_exist.so");
  EXPECT_FALSE(loaded);
}

TEST_F(PluginManagerTest, GetPluginNotFound) {
  PluginManager mgr;
  EXPECT_EQ(mgr.GetPlugin("nonexistent"), nullptr);
  EXPECT_EQ(mgr.GetPluginByFamily("unknown"), nullptr);
}

TEST_F(PluginManagerTest, HasPluginNotLoaded) {
  PluginManager mgr;
  EXPECT_FALSE(mgr.HasPlugin("test"));
}

TEST_F(PluginManagerTest, UnloadNonexistent) {
  PluginManager mgr;
  EXPECT_FALSE(mgr.UnloadPlugin("nonexistent"));
}

// ============================================================
// Plugin API Version Constant
// ============================================================

TEST(PluginApiTest, VersionConstant) { EXPECT_EQ(kPluginApiVersion, 1); }

}  // namespace
}  // namespace loong::ai_engine
