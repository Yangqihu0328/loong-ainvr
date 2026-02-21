// Copyright 2026 Loong AI NVR Project

#include "core/config/config_manager.h"

#include <fstream>

#include "spdlog/spdlog.h"

namespace loong::core {

ConfigManager& ConfigManager::Instance() {
  static ConfigManager instance;
  return instance;
}

bool ConfigManager::LoadFromFile(const std::string& file_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    std::ifstream file(file_path);
    if (!file.is_open()) {
      spdlog::error("Cannot open config file: {}", file_path);
      return false;
    }
    config_ = nlohmann::json::parse(file);
    spdlog::info("Configuration loaded from: {}", file_path);
    return true;
  } catch (const nlohmann::json::parse_error& e) {
    spdlog::error("Config parse error: {}", e.what());
    return false;
  }
}

const nlohmann::json& ConfigManager::GetConfig() const {
  return config_;
}

}  // namespace loong::core
