// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_CONFIG_CONFIG_MANAGER_H_
#define LOONG_CORE_CONFIG_CONFIG_MANAGER_H_

#include <mutex>
#include <string>

#include "nlohmann/json.hpp"

namespace loong::core {

/// Singleton configuration manager.
/// Loads and provides access to JSON configuration.
class ConfigManager {
 public:
  static ConfigManager& Instance();

  /// Load configuration from a JSON file.
  bool LoadFromFile(const std::string& file_path);

  /// Get the full configuration JSON object.
  const nlohmann::json& GetConfig() const;

  /// Get a configuration value by key path (e.g., "server.port").
  template <typename T>
  T Get(const std::string& key, const T& default_value) const;

  /// Set a configuration value by key path (e.g., "storage.retention_days").
  template <typename T>
  void Set(const std::string& key, const T& value);

  // Non-copyable
  ConfigManager(const ConfigManager&) = delete;
  ConfigManager& operator=(const ConfigManager&) = delete;

 private:
  ConfigManager() = default;

  nlohmann::json config_;
  mutable std::mutex mutex_;
};

// Template implementation
template <typename T>
T ConfigManager::Get(const std::string& key, const T& default_value) const {
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    // Support dot-separated key paths
    nlohmann::json::json_pointer ptr("/" + key);
    // Replace dots with slashes for json_pointer
    std::string path = "/" + key;
    for (auto& ch : path) {
      if (ch == '.') ch = '/';
    }
    return config_.at(nlohmann::json::json_pointer(path)).get<T>();
  } catch (...) {
    return default_value;
  }
}

template <typename T>
void ConfigManager::Set(const std::string& key, const T& value) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string path = "/" + key;
  for (auto& ch : path) {
    if (ch == '.') ch = '/';
  }
  config_[nlohmann::json::json_pointer(path)] = value;
}

}  // namespace loong::core

#endif  // LOONG_CORE_CONFIG_CONFIG_MANAGER_H_
