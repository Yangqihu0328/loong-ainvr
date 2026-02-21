// Copyright 2026 Loong AI NVR Project

#include "core/config/hot_reload_manager.h"

#include <fstream>

#include "core/config/config_manager.h"
#include "spdlog/spdlog.h"

namespace loong::core {

HotReloadManager::HotReloadManager() = default;

HotReloadManager::~HotReloadManager() { Stop(); }

bool HotReloadManager::Start(const std::string& file_path) {
  if (file_path.empty()) {
    spdlog::info("HotReloadManager: no config file, hot-reload disabled");
    return false;
  }

  config_path_ = file_path;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_config_ = ConfigManager::Instance().GetConfig();
  }

  watcher_.OnChange(
      [this](const std::string& path) { OnFileChanged(path); });

  if (!watcher_.Watch(file_path)) {
    spdlog::warn("HotReloadManager: failed to watch '{}'", file_path);
    return false;
  }

  spdlog::info("HotReloadManager: started, watching '{}'", file_path);
  return true;
}

void HotReloadManager::Stop() {
  watcher_.Stop();
  spdlog::info("HotReloadManager: stopped");
}

void HotReloadManager::RegisterSection(const std::string& section,
                                       SectionHandler handler) {
  section_handlers_.push_back({section, std::move(handler)});
  spdlog::debug("HotReloadManager: registered handler for '{}'", section);
}

void HotReloadManager::SetBroadcastCallback(BroadcastCallback callback) {
  broadcast_cb_ = std::move(callback);
}

bool HotReloadManager::ApplyPatch(const nlohmann::json& patch) {
  std::lock_guard<std::mutex> lock(mutex_);

  nlohmann::json old_config = current_config_;

  // Merge the patch into current config (shallow merge at top level)
  for (auto& [key, value] : patch.items()) {
    current_config_[key] = value;
  }

  // Also update the singleton ConfigManager
  ConfigManager::Instance().LoadFromFile(config_path_);

  // Save to file if we have a path
  if (!config_path_.empty()) {
    try {
      std::ofstream file(config_path_);
      if (file.is_open()) {
        file << current_config_.dump(2);
        spdlog::info("HotReloadManager: saved config to '{}'", config_path_);
      }
    } catch (const std::exception& e) {
      spdlog::error("HotReloadManager: failed to save config: {}", e.what());
    }
  }

  DiffAndNotify(old_config, current_config_);
  return true;
}

nlohmann::json HotReloadManager::GetCurrentConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_config_;
}

void HotReloadManager::OnFileChanged(const std::string& path) {
  nlohmann::json new_config;
  try {
    std::ifstream file(path);
    if (!file.is_open()) {
      spdlog::error("HotReloadManager: cannot open '{}'", path);
      return;
    }
    new_config = nlohmann::json::parse(file);
  } catch (const std::exception& e) {
    spdlog::error("HotReloadManager: parse error: {}", e.what());
    return;
  }

  nlohmann::json old_config;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    old_config = current_config_;
    current_config_ = new_config;
  }

  // Reload the singleton config manager too
  ConfigManager::Instance().LoadFromFile(path);

  DiffAndNotify(old_config, new_config);
}

void HotReloadManager::DiffAndNotify(const nlohmann::json& old_config,
                                     const nlohmann::json& new_config) {
  // Find which top-level sections changed
  std::vector<std::string> changed_sections;
  for (auto& [key, value] : new_config.items()) {
    auto it = old_config.find(key);
    if (it == old_config.end() || *it != value) {
      changed_sections.push_back(key);
    }
  }
  // Detect removed sections
  for (auto& [key, value] : old_config.items()) {
    if (!new_config.contains(key)) {
      changed_sections.push_back(key);
    }
  }

  if (changed_sections.empty()) {
    spdlog::debug("HotReloadManager: no config sections changed");
    return;
  }

  spdlog::info("HotReloadManager: {} section(s) changed: [{}]",
               changed_sections.size(),
               [&]() {
                 std::string s;
                 for (size_t i = 0; i < changed_sections.size(); ++i) {
                   if (i > 0) s += ", ";
                   s += changed_sections[i];
                 }
                 return s;
               }());

  // Notify section handlers
  for (const auto& section : changed_sections) {
    nlohmann::json section_cfg =
        new_config.contains(section) ? new_config[section] : nlohmann::json{};

    for (const auto& entry : section_handlers_) {
      if (entry.section == section) {
        try {
          entry.handler(section, section_cfg);
        } catch (const std::exception& e) {
          spdlog::error("HotReloadManager: handler error for '{}': {}",
                        section, e.what());
        }
      }
    }

    // Broadcast change event
    if (broadcast_cb_) {
      try {
        broadcast_cb_(section, section_cfg);
      } catch (const std::exception& e) {
        spdlog::error("HotReloadManager: broadcast error: {}", e.what());
      }
    }
  }
}

}  // namespace loong::core
