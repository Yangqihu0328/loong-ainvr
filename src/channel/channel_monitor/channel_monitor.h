// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CHANNEL_CHANNEL_MONITOR_CHANNEL_MONITOR_H_
#define LOONG_CHANNEL_CHANNEL_MONITOR_CHANNEL_MONITOR_H_

#include "core/common/types.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace loong::channel {

class ChannelManager;

/// Per-channel health metrics snapshot.
struct ChannelMetrics {
  int channel_id = -1;
  ChannelState state = ChannelState::kCreated;

  // Throughput
  int fps_in = 0;
  int fps_decode = 0;
  int fps_ai = 0;
  int fps_output = 0;

  // Cumulative counters
  int64_t frames_processed = 0;
  int64_t frames_dropped = 0;

  // Health indicators
  bool is_healthy = true;
  int consecutive_errors = 0;
  int recovery_attempts = 0;

  // Timing
  int64_t uptime_seconds = 0;
  int64_t last_frame_time_ms = 0;  // Timestamp of last processed frame
};

/// Monitors all active channels' health and publishes status events.
///
/// Periodically queries the ChannelManager for channel status, computes
/// health metrics (FPS deltas, stall detection, error accumulation), and
/// publishes "channel.metrics" events via EventBus for WebSocket consumers.
///
/// Also supports configurable auto-recovery: when a channel enters an error
/// state, the monitor can automatically attempt to restart it.
class ChannelMonitor {
 public:
  /// @param manager  Reference to the ChannelManager to query.
  explicit ChannelMonitor(ChannelManager& manager);
  ~ChannelMonitor();

  /// Start the monitoring loop.
  /// @param interval_seconds  Polling interval (default 3 s).
  void Start(int interval_seconds = 3);

  /// Stop the monitoring loop.
  void Stop();

  /// Get the latest metrics for a specific channel.
  ChannelMetrics GetMetrics(int channel_id) const;

  /// Get metrics for all monitored channels.
  std::vector<ChannelMetrics> GetAllMetrics() const;

  /// Enable/disable automatic recovery for stalled/error channels.
  void SetAutoRecovery(bool enable);

  /// Set the maximum consecutive errors before triggering recovery.
  void SetErrorThreshold(int threshold);

  /// Set the stall timeout — if a channel produces no frames for this
  /// many seconds while in Running state, it is considered stalled.
  void SetStallTimeout(int seconds);

  // Non-copyable
  ChannelMonitor(const ChannelMonitor&) = delete;
  ChannelMonitor& operator=(const ChannelMonitor&) = delete;

 private:
  /// Internal state tracked per channel across polling cycles.
  struct ChannelTracker {
    int64_t prev_frames_processed = 0;
    int64_t prev_frames_dropped = 0;
    std::chrono::steady_clock::time_point last_progress_time;
    std::chrono::steady_clock::time_point start_time;
    int consecutive_errors = 0;
    int recovery_attempts = 0;
  };

  void MonitorLoop();

  /// Compute FPS as delta of frame counts between two polling cycles.
  int ComputeFps(int64_t current, int64_t previous, int interval_seconds) const;

  /// Attempt to recover a channel that is in error/stalled state.
  void AttemptRecovery(int channel_id);

  ChannelManager& manager_;

  mutable std::mutex mutex_;
  std::unordered_map<int, ChannelMetrics> latest_metrics_;
  std::unordered_map<int, ChannelTracker> trackers_;

  std::atomic<bool> running_{false};
  int interval_seconds_ = 3;
  std::thread worker_;

  // Auto-recovery settings
  bool auto_recovery_ = false;
  int error_threshold_ = 3;
  int stall_timeout_seconds_ = 30;
};

}  // namespace loong::channel

#endif  // LOONG_CHANNEL_CHANNEL_MONITOR_CHANNEL_MONITOR_H_
