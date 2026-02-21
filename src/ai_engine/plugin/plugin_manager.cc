// Copyright 2026 Loong AI NVR Project

#include "ai_engine/plugin/plugin_manager.h"

#include <dlfcn.h>
#include <dirent.h>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace loong::ai_engine {

PluginManager::PluginManager() = default;

PluginManager::~PluginManager() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [name, p] : plugins_) {
    ClosePlugin(p);
  }
  plugins_.clear();
}

void PluginManager::SetPluginDir(const std::string& dir) {
  std::lock_guard<std::mutex> lock(mutex_);
  plugin_dir_ = dir;
}

std::string PluginManager::GetPluginDir() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plugin_dir_;
}

int PluginManager::ScanAndLoad() {
  std::string dir;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    dir = plugin_dir_;
  }

  if (dir.empty()) return 0;

  DIR* dp = opendir(dir.c_str());
  if (!dp) {
    spdlog::warn("PluginManager: cannot open directory '{}'", dir);
    return 0;
  }

  int loaded = 0;
  struct dirent* entry = nullptr;
  while ((entry = readdir(dp)) != nullptr) {
    std::string filename = entry->d_name;
    if (filename.size() > 3 &&
        filename.substr(filename.size() - 3) == ".so") {
      std::string full_path = dir + "/" + filename;
      if (LoadPlugin(full_path)) {
        loaded++;
      }
    }
  }
  closedir(dp);

  spdlog::info("PluginManager: scanned '{}', loaded {} plugins", dir, loaded);
  return loaded;
}

bool PluginManager::LoadPlugin(const std::string& so_path) {
  void* handle = dlopen(so_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!handle) {
    spdlog::error("PluginManager: dlopen failed for '{}': {}", so_path,
                  dlerror());
    return false;
  }

  dlerror();  // Clear errors

  auto create_fn = reinterpret_cast<CreatePluginFn>(
      dlsym(handle, "loong_create_plugin"));
  if (!create_fn) {
    spdlog::error("PluginManager: loong_create_plugin not found in '{}'",
                  so_path);
    dlclose(handle);
    return false;
  }

  auto destroy_fn = reinterpret_cast<DestroyPluginFn>(
      dlsym(handle, "loong_destroy_plugin"));

  ModelPlugin* instance = create_fn();
  if (!instance) {
    spdlog::error("PluginManager: loong_create_plugin returned null for '{}'",
                  so_path);
    dlclose(handle);
    return false;
  }

  PluginInfo info = instance->GetInfo();
  if (info.api_version != kPluginApiVersion) {
    spdlog::error(
        "PluginManager: plugin '{}' API version {} != expected {}",
        info.name, info.api_version, kPluginApiVersion);
    if (destroy_fn) {
      destroy_fn(instance);
    }
    dlclose(handle);
    return false;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  if (plugins_.count(info.name) > 0) {
    spdlog::warn("PluginManager: plugin '{}' already loaded, skipping",
                 info.name);
    if (destroy_fn) {
      destroy_fn(instance);
    }
    dlclose(handle);
    return false;
  }

  LoadedPlugin lp;
  lp.file_path = so_path;
  lp.info = info;
  lp.instance = instance;
  lp.dl_handle = handle;
  lp.active = true;
  plugins_[info.name] = lp;

  spdlog::info("PluginManager: loaded '{}' v{} (family: {})",
               info.name, info.version, info.model_family);
  return true;
}

bool PluginManager::UnloadPlugin(const std::string& name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = plugins_.find(name);
  if (it == plugins_.end()) return false;

  ClosePlugin(it->second);
  plugins_.erase(it);
  spdlog::info("PluginManager: unloaded '{}'", name);
  return true;
}

void PluginManager::UnloadAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [name, p] : plugins_) {
    ClosePlugin(p);
  }
  plugins_.clear();
  spdlog::info("PluginManager: all plugins unloaded");
}

ModelPlugin* PluginManager::GetPlugin(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = plugins_.find(name);
  if (it != plugins_.end() && it->second.active) {
    return it->second.instance;
  }
  return nullptr;
}

ModelPlugin* PluginManager::GetPluginByFamily(
    const std::string& family) const {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& [_, p] : plugins_) {
    if (p.active && p.info.model_family == family) {
      return p.instance;
    }
  }
  return nullptr;
}

std::vector<PluginInfo> PluginManager::ListPlugins() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PluginInfo> result;
  result.reserve(plugins_.size());
  for (const auto& [_, p] : plugins_) {
    result.push_back(p.info);
  }
  return result;
}

bool PluginManager::HasPlugin(const std::string& name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return plugins_.count(name) > 0;
}

int PluginManager::Count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(plugins_.size());
}

void PluginManager::ClosePlugin(LoadedPlugin& p) {
  if (p.instance) {
    p.instance->Shutdown();

    auto destroy_fn = reinterpret_cast<DestroyPluginFn>(
        dlsym(p.dl_handle, "loong_destroy_plugin"));
    if (destroy_fn) {
      destroy_fn(p.instance);
    }
    p.instance = nullptr;
  }
  if (p.dl_handle) {
    dlclose(p.dl_handle);
    p.dl_handle = nullptr;
  }
  p.active = false;
}

}  // namespace loong::ai_engine
