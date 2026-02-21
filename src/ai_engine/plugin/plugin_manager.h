// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_PLUGIN_PLUGIN_MANAGER_H_
#define LOONG_AI_ENGINE_PLUGIN_PLUGIN_MANAGER_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ai_engine/plugin/model_plugin.h"

namespace loong::ai_engine {

/// Represents a loaded plugin with its shared library handle.
struct LoadedPlugin {
  std::string file_path;
  PluginInfo info;
  ModelPlugin* instance = nullptr;
  void* dl_handle = nullptr;
  bool active = true;
};

/// Manages dynamic loading/unloading of AI model plugins.
///
/// Scans a plugin directory for .so files, loads them via dlopen,
/// and provides access to loaded plugins by name or model family.
class PluginManager {
 public:
  PluginManager();
  ~PluginManager();

  /// Set the directory to scan for plugin .so files.
  void SetPluginDir(const std::string& dir);
  std::string GetPluginDir() const;

  /// Scan the plugin directory and load all valid plugins.
  int ScanAndLoad();

  /// Load a single plugin from a .so file path.
  bool LoadPlugin(const std::string& so_path);

  /// Unload a plugin by name.
  bool UnloadPlugin(const std::string& name);

  /// Unload all loaded plugins (call during shutdown).
  void UnloadAll();

  /// Get a loaded plugin by name.
  ModelPlugin* GetPlugin(const std::string& name) const;

  /// Get a loaded plugin by model family.
  ModelPlugin* GetPluginByFamily(const std::string& family) const;

  /// List all loaded plugins.
  std::vector<PluginInfo> ListPlugins() const;

  /// Check if a plugin name is loaded.
  bool HasPlugin(const std::string& name) const;

  /// Get total loaded plugin count.
  int Count() const;

  // Non-copyable
  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

 private:
  void ClosePlugin(LoadedPlugin& p);

  std::string plugin_dir_;
  std::unordered_map<std::string, LoadedPlugin> plugins_;
  mutable std::mutex mutex_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_PLUGIN_PLUGIN_MANAGER_H_
