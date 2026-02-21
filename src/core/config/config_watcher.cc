// Copyright 2026 Loong AI NVR Project

#include "core/config/config_watcher.h"

#include "spdlog/spdlog.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace loong::core {

namespace {
constexpr size_t kEventBufLen = 4096;
constexpr int kPollIntervalMs = 500;
}  // namespace

ConfigWatcher::ConfigWatcher() = default;

ConfigWatcher::~ConfigWatcher() { Stop(); }

bool ConfigWatcher::Watch(const std::string& file_path) {
  if (running_) {
    spdlog::warn("ConfigWatcher: already watching '{}'", watch_path_);
    return false;
  }

  inotify_fd_ = inotify_init1(IN_NONBLOCK);
  if (inotify_fd_ < 0) {
    spdlog::error("ConfigWatcher: inotify_init1 failed: {}",
                  std::strerror(errno));
    return false;
  }

  // Watch for modifications, close-after-write, and move-to (editor save)
  watch_fd_ = inotify_add_watch(inotify_fd_, file_path.c_str(),
                                IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO);
  if (watch_fd_ < 0) {
    spdlog::error("ConfigWatcher: inotify_add_watch failed for '{}': {}",
                  file_path, std::strerror(errno));
    close(inotify_fd_);
    inotify_fd_ = -1;
    return false;
  }

  watch_path_ = file_path;
  running_ = true;
  watch_thread_ = std::thread(&ConfigWatcher::WatchLoop, this);

  spdlog::info("ConfigWatcher: watching '{}'", file_path);
  return true;
}

void ConfigWatcher::Stop() {
  if (!running_) return;
  running_ = false;

  if (watch_thread_.joinable()) {
    watch_thread_.join();
  }

  if (watch_fd_ >= 0) {
    inotify_rm_watch(inotify_fd_, watch_fd_);
    watch_fd_ = -1;
  }
  if (inotify_fd_ >= 0) {
    close(inotify_fd_);
    inotify_fd_ = -1;
  }

  spdlog::info("ConfigWatcher: stopped");
}

void ConfigWatcher::OnChange(ChangeCallback callback) {
  callbacks_.push_back(std::move(callback));
}

void ConfigWatcher::WatchLoop() {
  alignas(struct inotify_event) char buf[kEventBufLen];

  while (running_) {
    ssize_t len = read(inotify_fd_, buf, sizeof(buf));
    if (len < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollIntervalMs));
        continue;
      }
      spdlog::error("ConfigWatcher: read error: {}", std::strerror(errno));
      break;
    }

    bool changed = false;
    for (ssize_t i = 0; i < len;) {
      auto* event = reinterpret_cast<struct inotify_event*>(&buf[i]);
      if ((event->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO)) != 0) {
        changed = true;
      }
      i += static_cast<ssize_t>(sizeof(struct inotify_event)) + event->len;
    }

    if (changed) {
      spdlog::info("ConfigWatcher: detected change in '{}'", watch_path_);
      for (const auto& cb : callbacks_) {
        try {
          cb(watch_path_);
        } catch (const std::exception& e) {
          spdlog::error("ConfigWatcher: callback error: {}", e.what());
        }
      }
    }
  }
}

}  // namespace loong::core
