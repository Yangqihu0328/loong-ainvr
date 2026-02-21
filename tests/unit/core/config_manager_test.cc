// Copyright 2026 Loong AI NVR Project

#include "core/config/config_manager.h"

#include "gtest/gtest.h"

#include <fstream>

namespace loong {
namespace core {
namespace {

TEST(ConfigManager, DefaultValues) {
  auto& config = ConfigManager::Instance();
  EXPECT_EQ(config.Get<int>("nonexistent.key", 42), 42);
  EXPECT_EQ(config.Get<std::string>("missing", "default"), "default");
}

TEST(ConfigManager, LoadFromFile) {
  // Create a temporary config file
  const std::string test_config = R"({
    "server": {
      "port": 8080,
      "host": "0.0.0.0"
    },
    "max_channels": 64
  })";

  const std::string test_path = "/tmp/loong_test_config.json";
  {
    std::ofstream file(test_path);
    file << test_config;
  }

  auto& config = ConfigManager::Instance();
  EXPECT_TRUE(config.LoadFromFile(test_path));
  EXPECT_EQ(config.Get<int>("server.port", 0), 8080);
  EXPECT_EQ(config.Get<std::string>("server.host", ""), "0.0.0.0");
  EXPECT_EQ(config.Get<int>("max_channels", 0), 64);
}

}  // namespace
}  // namespace core
}  // namespace loong
