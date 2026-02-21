// Copyright 2026 Loong AI NVR Project

#include "channel/channel_monitor/channel_monitor.h"

#include "channel/channel_manager/channel_manager.h"
#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

namespace loong::channel {

ChannelMonitor::ChannelMonitor(ChannelManager& manager) : manager_(manager) {}

ChannelMonitor::~ChannelMonitor() { Stop(); }

void ChannelMonitor::Start(int interval_seconds) {
  if (running_) return;
  interval_seconds_ = interval_seconds;
  running_ = true;
  worker_ = std::thread(&ChannelMonitor::MonitorLoop, this);
  spdlog::info("ChannelMonitor: started (interval={}s)", interval_seconds);
}

void ChannelMonitor::Stop() {
  if (!running_) return;
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
  spdlog::info("ChannelMonitor: stopped");
}

ChannelMetrics ChannelMonitor::GetMetrics(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = latest_metrics_.find(channel_id);
  if (it != latest_metrics_.end()) {
    return it->second;
  }
  ChannelMetrics not_found;
  not_found.channel_id = -1;
  return not_found;
}

std::vector<ChannelMetrics> ChannelMonitor::GetAllMetrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelMetrics> result;
  result.reserve(latest_metrics_.size());
  for (const auto& [id, metrics] : latest_metrics_) {
    result.push_back(metrics);
  }
  return result;
}

void ChannelMonitor::SetAutoRecovery(bool enable) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto_recovery_ = enable;
  spdlog::info("ChannelMonitor: auto-recovery {}",
               enable ? "enabled" : "disabled");
}

void ChannelMonitor::SetErrorThreshold(int threshold) {
  std::lock_guard<std::mutex> lock(mutex_);
  error_threshold_ = threshold;
}

void ChannelMonitor::SetStallTimeout(int seconds) {
  std::lock_guard<std::mutex> lock(mutex_);
  stall_timeout_seconds_ = seconds;
}

void ChannelMonitor::MonitorLoop() {
  while (running_) {
    auto all_status = manager_.GetAllStatus();
    auto now = std::chrono::steady_clock::now();

    std::vector<int> recovery_candidates;

    {
      std::lock_guard<std::mutex> lock(mutex_);

      // Remove trackers for channels that no longer exist.
      std::vector<int> stale_ids;
      for (const auto& [id, _] : trackers_) {
        bool found = false;
        for (const auto& s : all_status) {
          if (s.id == id) {
            found = true;
            break;
          }
        }
        if (!found) {
          stale_ids.push_back(id);
        }
      }
      for (int id : stale_ids) {
        trackers_.erase(id);
        latest_metrics_.erase(id);
      }

      for (const auto& status : all_status) {
        int id = status.id;

        // Initialize tracker for new channels.
        if (trackers_.find(id) == trackers_.end()) {
          ChannelTracker tracker;
          tracker.last_progress_time = now;
          tracker.start_time = now;
          trackers_[id] = tracker;
        }

        auto& tracker = trackers_[id];
        ChannelMetrics metrics;
        metrics.channel_id = id;
        metrics.state = status.state;
        metrics.frames_processed = status.frames_processed;
        metrics.frames_dropped = status.frames_dropped;

        // Compute throughput FPS from frame count deltas.
        int64_t frames_delta =
            status.frames_processed - tracker.prev_frames_processed;
        metrics.fps_in =
            ComputeFps(status.frames_processed, tracker.prev_frames_processed,
                       interval_seconds_);

        // Use status-provided FPS values when available.
        metrics.fps_decode = status.fps_decode;
        metrics.fps_ai = status.fps_ai;

        // Check for progress (stall detection).
        if (frames_delta > 0) {
          tracker.last_progress_time = now;
          tracker.consecutive_errors = 0;
        }

        // Update previous counters.
        tracker.prev_frames_processed = status.frames_processed;
        tracker.prev_frames_dropped = status.frames_dropped;

        // Calculate uptime.
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            now - tracker.start_time);
        metrics.uptime_seconds = uptime.count();

        // Determine health.
        bool is_error = (status.state == ChannelState::kError);
        bool is_stalled = false;

        if (status.state == ChannelState::kRunning) {
          auto since_progress =
              std::chrono::duration_cast<std::chrono::seconds>(
                  now - tracker.last_progress_time);
          is_stalled = (since_progress.count() > stall_timeout_seconds_);
        }

        if (is_error) {
          ++tracker.consecutive_errors;
        }

        metrics.is_healthy = !is_error && !is_stalled;
        metrics.consecutive_errors = tracker.consecutive_errors;
        metrics.recovery_attempts = tracker.recovery_attempts;

        if (!status.error_message.empty()) {
          spdlog::warn("ChannelMonitor: ch {} error: {}", id,
                       status.error_message);
        }

        latest_metrics_[id] = metrics;

        // Collect recovery candidates.
        if (auto_recovery_ && (is_error || is_stalled)) {
          if (tracker.consecutive_errors >= error_threshold_ || is_stalled) {
            recovery_candidates.push_back(id);
          }
        }
      }
    }

    // Publish aggregated metrics via EventBus.
    core::EventBus::Instance().Publish("channel.metrics");

    // Attempt recovery outside the lock.
    for (int id : recovery_candidates) {
      AttemptRecovery(id);
    }

    // Sleep in small increments for responsive shutdown.
    for (int i = 0; i < interval_seconds_ * 10 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

int ChannelMonitor::ComputeFps(int64_t current, int64_t previous,
                               int interval_seconds) const {
  if (interval_seconds <= 0) return 0;
  int64_t delta = current - previous;
  if (delta < 0) delta = 0;
  return static_cast<int>(delta / interval_seconds);
}

void ChannelMonitor::AttemptRecovery(int channel_id) {
  static constexpr int kMaxRecoveryAttempts = 10;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = trackers_.find(channel_id);
    if (it != trackers_.end()) {
      if (it->second.recovery_attempts >= kMaxRecoveryAttempts) {
        spdlog::error(
            "ChannelMonitor: ch {} exceeded max recovery attempts ({}), "
            "giving up",
            channel_id, kMaxRecoveryAttempts);
        return;
      }
      ++it->second.recovery_attempts;
      it->second.consecutive_errors = 0;
      it->second.last_progress_time = std::chrono::steady_clock::now();
    }
  }

  spdlog::warn("ChannelMonitor: attempting recovery for ch {}", channel_id);

  // Stop then restart the channel with a brief delay.
  manager_.StopChannel(channel_id);
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  bool ok = manager_.StartChannel(channel_id);

  if (ok) {
    spdlog::info("ChannelMonitor: ch {} recovered successfully", channel_id);
    core::EventBus::Instance().Publish("channel.recovered",
                                       std::any(channel_id));
  } else {
    spdlog::error("ChannelMonitor: ch {} recovery failed", channel_id);
    core::EventBus::Instance().Publish("channel.recovery_failed",
                                       std::any(channel_id));
  }
}

}  // namespace loong::channel
