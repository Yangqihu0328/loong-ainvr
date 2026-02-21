// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_CONFIG_CONFIG_WATCHER_H_
#define LOONG_CORE_CONFIG_CONFIG_WATCHER_H_

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace loong::core {

/// Watches a config file for modifications using Linux inotify.
/// When a change is detected, invokes all registered callbacks.
class ConfigWatcher {
 public:
  using ChangeCallback = std::function<void(const std::string& path)>;

  ConfigWatcher();
  ~ConfigWatcher();

  /// Start watching the given file path. Returns false on failure.
  bool Watch(const std::string& file_path);

  /// Stop watching.
  void Stop();

  /// Register a callback to be invoked when the file changes.
  void OnChange(ChangeCallback callback);

  /// Check if the watcher is currently active.
  bool IsWatching() const { return running_.load(); }

  ConfigWatcher(const ConfigWatcher&) = delete;
  ConfigWatcher& operator=(const ConfigWatcher&) = delete;

 private:
  void WatchLoop();

  std::string watch_path_;
  int inotify_fd_ = -1;
  int watch_fd_ = -1;

  std::atomic<bool> running_{false};
  std::thread watch_thread_;

  std::vector<ChangeCallback> callbacks_;
};

}  // namespace loong::core

#endif  // LOONG_CORE_CONFIG_CONFIG_WATCHER_H_
