// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_CONFIG_HOT_RELOAD_MANAGER_H_
#define LOONG_CORE_CONFIG_HOT_RELOAD_MANAGER_H_

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/config/config_watcher.h"

namespace loong::core {

/// Manages configuration hot-reloading.
/// Watches the config file, detects changed sections, and notifies
/// registered section handlers so components can update without restart.
class HotReloadManager {
 public:
  /// Handler receives the section name and new config JSON for that section.
  using SectionHandler =
      std::function<void(const std::string& section, const nlohmann::json& cfg)>;

  /// Callback for broadcasting config changes (e.g., via WebSocket).
  using BroadcastCallback =
      std::function<void(const std::string& section, const nlohmann::json& cfg)>;

  HotReloadManager();
  ~HotReloadManager();

  /// Start watching the config file. If file_path is empty, does nothing.
  bool Start(const std::string& file_path);

  /// Stop watching.
  void Stop();

  /// Register a handler for a specific config section (e.g., "hls", "ai").
  void RegisterSection(const std::string& section, SectionHandler handler);

  /// Set the broadcast callback for pushing change events.
  void SetBroadcastCallback(BroadcastCallback callback);

  /// Apply a partial config update (from API). Merges the patch into current
  /// config and triggers affected section handlers.
  bool ApplyPatch(const nlohmann::json& patch);

  /// Get the current config snapshot.
  nlohmann::json GetCurrentConfig() const;

  HotReloadManager(const HotReloadManager&) = delete;
  HotReloadManager& operator=(const HotReloadManager&) = delete;

 private:
  void OnFileChanged(const std::string& path);
  void DiffAndNotify(const nlohmann::json& old_config,
                     const nlohmann::json& new_config);

  ConfigWatcher watcher_;
  std::string config_path_;
  nlohmann::json current_config_;
  mutable std::mutex mutex_;

  struct SectionEntry {
    std::string section;
    SectionHandler handler;
  };
  std::vector<SectionEntry> section_handlers_;
  BroadcastCallback broadcast_cb_;
};

}  // namespace loong::core

#endif  // LOONG_CORE_CONFIG_HOT_RELOAD_MANAGER_H_
