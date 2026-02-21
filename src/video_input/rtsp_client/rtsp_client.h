// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_VIDEO_INPUT_RTSP_CLIENT_RTSP_CLIENT_H_
#define LOONG_VIDEO_INPUT_RTSP_CLIENT_RTSP_CLIENT_H_

#include "core/common/types.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Forward-declare FFmpeg types to avoid leaking the C header.
struct AVFormatContext;
struct AVBSFContext;

namespace loong::video_input {

/// RTSP transport mode.
enum class RtspTransport {
  kTcp,  // Interleaved TCP (default, more reliable)
  kUdp,  // UDP (lower latency, may lose packets)
};

/// Connection state of an RTSP client.
enum class RtspConnectionState {
  kDisconnected,
  kConnecting,
  kConnected,
  kReconnecting,
  kError,
};

/// Convert RtspConnectionState to a human-readable string.
inline const char* RtspConnectionStateToString(RtspConnectionState s) {
  switch (s) {
    case RtspConnectionState::kDisconnected:
      return "Disconnected";
    case RtspConnectionState::kConnecting:
      return "Connecting";
    case RtspConnectionState::kConnected:
      return "Connected";
    case RtspConnectionState::kReconnecting:
      return "Reconnecting";
    case RtspConnectionState::kError:
      return "Error";
  }
  return "Unknown";
}

/// Information about the detected stream after a successful connection.
struct RtspStreamInfo {
  CodecType codec = CodecType::kUnknown;
  int width = 0;
  int height = 0;
  int framerate = 0;  // 0 = unknown / variable
};

/// Configuration for an RTSP client instance.
struct RtspClientConfig {
  std::string url;  // RTSP URL (e.g., rtsp://user:pass@ip/stream)
  RtspTransport transport = RtspTransport::kTcp;
  int connect_timeout_ms = 5000;   // Connection timeout in milliseconds
  bool auto_reconnect = true;      // Enable automatic reconnection
  int max_reconnect_attempts = 0;  // 0 = unlimited
  int reconnect_delay_ms = 3000;  // Initial delay between reconnection attempts
  int reconnect_max_delay_ms = 30000;  // Maximum backoff delay
};

/// Callback invoked for each received video packet.
/// Parameters: channel_id, packet data, packet size, pts, dts, is_keyframe,
/// codec.
using PacketCallback = std::function<void(int channel_id, const uint8_t* data,
                                          size_t size, int64_t pts, int64_t dts,
                                          bool is_keyframe, CodecType codec)>;

/// Callback invoked when connection state changes.
using StateCallback = std::function<void(
    int channel_id, RtspConnectionState new_state, const std::string& message)>;

/// A reusable RTSP client that pulls video packets from a camera/stream.
///
/// Features:
///   - FFmpeg-based RTSP demuxing (TCP or UDP transport)
///   - Automatic reconnection with exponential backoff
///   - Callback-driven packet delivery
///   - Thread-safe start/stop lifecycle
///
/// Usage:
///   RtspClient client;
///   client.SetPacketCallback([](int ch, ...) { ... });
///   client.Open(config, channel_id);
///   // ... packets flow via callback ...
///   client.Close();
class RtspClient {
 public:
  RtspClient();
  ~RtspClient();

  /// Open the RTSP stream and start pulling packets.
  /// Returns true if the initial connection succeeds.
  bool Open(const RtspClientConfig& config, int channel_id);

  /// Close the stream and release all resources.
  void Close();

  /// Check whether the client is currently connected and pulling.
  bool IsConnected() const;

  /// Get the current connection state.
  RtspConnectionState GetState() const;

  /// Get stream information (valid only after successful connection).
  RtspStreamInfo GetStreamInfo() const;

  /// Get the RTSP URL this client is configured for.
  std::string GetUrl() const;

  /// Get the channel ID associated with this client.
  int GetChannelId() const;

  /// Get the number of packets received since last Open().
  int64_t PacketsReceived() const;

  /// Get the number of reconnection attempts since last Open().
  int ReconnectAttempts() const;

  /// Set the callback for received video packets.
  void SetPacketCallback(PacketCallback callback);

  /// Set the callback for connection state changes.
  void SetStateCallback(StateCallback callback);

  // Non-copyable, movable
  RtspClient(const RtspClient&) = delete;
  RtspClient& operator=(const RtspClient&) = delete;

 private:
  /// Connect to the RTSP stream using FFmpeg.
  bool Connect();

  /// Disconnect and release the FFmpeg context.
  void Disconnect();

  /// The main pull loop running in its own thread.
  void PullLoop();

  /// Attempt reconnection with exponential backoff.
  bool TryReconnect();

  /// Update connection state and notify callback.
  void SetState(RtspConnectionState new_state, const std::string& message = "");

  // Configuration
  RtspClientConfig config_;
  int channel_id_ = -1;

  // FFmpeg state
  AVFormatContext* fmt_ctx_ = nullptr;
  AVBSFContext* bsf_ctx_ =
      nullptr;  // h264/hevc_mp4toannexb for container files
  int video_stream_index_ = -1;
  bool is_file_source_ = false;  // true for local file inputs (MP4/MKV/etc.)
  RtspStreamInfo stream_info_;

  // Threading
  std::atomic<bool> running_{false};
  std::thread pull_thread_;

  // State
  std::atomic<RtspConnectionState> state_{RtspConnectionState::kDisconnected};
  std::atomic<int64_t> packets_received_{0};
  std::atomic<int> reconnect_attempts_{0};

  // Callbacks (protected by mutex for safe setting)
  mutable std::mutex callback_mutex_;
  PacketCallback packet_callback_;
  StateCallback state_callback_;
};

}  // namespace loong::video_input

#endif  // LOONG_VIDEO_INPUT_RTSP_CLIENT_RTSP_CLIENT_H_
