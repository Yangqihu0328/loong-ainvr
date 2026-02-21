// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_VIDEO_INPUT_STREAM_MANAGER_STREAM_MANAGER_H_
#define LOONG_VIDEO_INPUT_STREAM_MANAGER_STREAM_MANAGER_H_

#include <functional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "video_input/rtsp_client/rtsp_client.h"

namespace loong::video_input {

/// Per-stream runtime statistics.
struct StreamStats {
  int channel_id = -1;
  std::string url;
  RtspConnectionState state = RtspConnectionState::kDisconnected;
  RtspStreamInfo stream_info;
  int64_t packets_received = 0;
  int reconnect_attempts = 0;
};

/// Callback invoked when a stream's connection state changes.
using StreamStateCallback = std::function<void(
    int channel_id, RtspConnectionState new_state,
    const std::string& message)>;

/// Manages the lifecycle of multiple RTSP client streams.
///
/// StreamManager centralises the creation, start, stop, and monitoring
/// of up to `kMaxStreams` concurrent RTSP streams.  Each stream is backed
/// by an `RtspClient` instance.  The manager provides:
///
///   - Dynamic add / remove of streams at runtime
///   - Bulk start / stop operations
///   - Aggregated statistics for all streams
///   - A single packet callback dispatched for every received video packet
///   - A state callback for connection-level events
///
/// Thread-safety: all public methods are safe to call from any thread.
class StreamManager {
 public:
  static constexpr int kMaxStreams = 64;

  StreamManager();
  ~StreamManager();

  /// Add a new stream. Returns true on success.
  /// The stream is created but not yet started.
  bool AddStream(int channel_id, const RtspClientConfig& config);

  /// Remove a stream (stops it first if running).
  bool RemoveStream(int channel_id);

  /// Start pulling packets for the given stream.
  bool StartStream(int channel_id);

  /// Stop pulling packets for the given stream.
  bool StopStream(int channel_id);

  /// Update the RTSP configuration for a stream.
  /// Stops the stream if running, applies the new config, and optionally
  /// restarts it.
  bool UpdateStream(int channel_id, const RtspClientConfig& new_config,
                    bool restart = true);

  /// Start all added streams.
  void StartAll();

  /// Stop all running streams.
  void StopAll();

  /// Get statistics for a single stream.
  StreamStats GetStats(int channel_id) const;

  /// Get statistics for all streams.
  std::vector<StreamStats> GetAllStats() const;

  /// Check whether a stream exists.
  bool HasStream(int channel_id) const;

  /// Get the number of managed streams.
  size_t StreamCount() const;

  /// Set the callback for video packets from any stream.
  void SetPacketCallback(PacketCallback callback);

  /// Set the callback for stream-level state changes.
  void SetStateCallback(StreamStateCallback callback);

  // Non-copyable
  StreamManager(const StreamManager&) = delete;
  StreamManager& operator=(const StreamManager&) = delete;

 private:
  struct StreamEntry {
    RtspClientConfig config;
    std::unique_ptr<RtspClient> client;
    bool started = false;
  };

  /// Build the internal packet callback that forwards to the user callback.
  PacketCallback MakePacketCallback();

  /// Build the internal state callback that forwards to the user callback.
  StateCallback MakeStateCallback();

  mutable std::shared_mutex streams_mutex_;
  std::unordered_map<int, std::unique_ptr<StreamEntry>> streams_;

  // User-facing callbacks (protected by their own mutex).
  mutable std::mutex callback_mutex_;
  PacketCallback packet_callback_;
  StreamStateCallback state_callback_;
};

}  // namespace loong::video_input

#endif  // LOONG_VIDEO_INPUT_STREAM_MANAGER_STREAM_MANAGER_H_
