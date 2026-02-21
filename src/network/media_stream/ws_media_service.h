// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_MEDIA_STREAM_WS_MEDIA_SERVICE_H_
#define LOONG_NETWORK_MEDIA_STREAM_WS_MEDIA_SERVICE_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "core/common/types.h"

namespace loong::network {

/// A viewer connected via WebSocket for fMP4 media streaming.
struct MediaViewer {
  int fd = -1;
  std::mutex mtx;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> pending_segments;
  std::atomic<bool> active{true};
  bool got_init_segment = false;
  bool got_keyframe = false;

  static constexpr size_t kMaxPendingSegments = 300;
};

/// WebSocket-based fMP4 media streaming service.
///
/// Provides low-latency video streaming via WebSocket binary frames
/// using Fragmented MP4 (fMP4) container format. Supports both
/// H.264 and H.265 codecs.
///
/// Architecture:
///   OutputStage → PushFrame() → fMP4 muxer → broadcast to all viewers
///   WebSocket connection → /ws/live/ch{id} → init segment + media segments
///
/// Usage flow:
///   1. RegisterChannel() when a channel starts.
///   2. PushFrame() called by output stage for each encoded frame.
///   3. Client connects via WebSocket, receives init segment + media segments.
///   4. UnregisterChannel() when channel stops.
class WsMediaService : public std::enable_shared_from_this<WsMediaService> {
 public:
  /// @param host  Bind address for the WebSocket server.
  /// @param port  Port number for the WebSocket server.
  WsMediaService(std::string  host, int port);
  ~WsMediaService();

  /// Start the WebSocket media server.
  bool Start();

  /// Stop the server and disconnect all viewers.
  void Stop();

  /// Register a channel for media streaming.
  /// @param channel_id  Channel ID.
  /// @param codec  Codec type (H.264 or H.265).
  /// @param width  Video width.
  /// @param height  Video height.
  /// @param framerate  Stream framerate (0 = unknown, defaults to 30).
  void RegisterChannel(int channel_id, CodecType codec,
                       uint16_t width, uint16_t height,
                       int framerate = 0);

  /// Unregister a channel and disconnect all its viewers.
  void UnregisterChannel(int channel_id);

  /// Push an encoded video frame for muxing and streaming.
  /// Called from OutputStage when a new encoded frame is produced.
  /// @param channel_id  Channel ID.
  /// @param data  Annex B encoded frame data.
  /// @param size  Frame data size.
  /// @param pts  Presentation timestamp (microseconds).
  /// @param is_keyframe  Whether this is a keyframe.
  void PushFrame(int channel_id, const uint8_t* data, size_t size,
                 int64_t pts, bool is_keyframe);

  /// Get the number of active viewer connections.
  size_t ViewerCount() const;

  // Non-copyable
  WsMediaService(const WsMediaService&) = delete;
  WsMediaService& operator=(const WsMediaService&) = delete;

 private:
  /// Per-channel media streaming state.
  struct ChannelStream {
    std::mutex mtx;
    CodecType codec = CodecType::kUnknown;
    uint16_t width = 0;
    uint16_t height = 0;

    // H.264 parameter sets
    std::vector<uint8_t> h264_sps;
    std::vector<uint8_t> h264_pps;

    // H.265 parameter sets
    std::vector<uint8_t> h265_vps;
    std::vector<uint8_t> h265_sps;
    std::vector<uint8_t> h265_pps;

    // Cached init segment (regenerated when params change)
    std::vector<uint8_t> init_segment;

    int64_t base_pts = -1;
    uint32_t framerate = 30;
    uint32_t sequence_number = 1;
    bool active = true;

    std::vector<std::weak_ptr<MediaViewer>> viewers;
  };

  void AcceptLoop();
  void HandleClient(std::shared_ptr<MediaViewer> viewer, int channel_id);
  bool PerformHandshake(int fd, int& channel_id);
  int ParseChannelId(const std::string& path);
  void SendBinaryFrame(const std::shared_ptr<MediaViewer>& viewer,
                       const std::vector<uint8_t>& data);
  void RemoveViewer(int channel_id, const std::shared_ptr<MediaViewer>& viewer);

  /// Build a WebSocket binary frame (opcode 0x2).
  static std::vector<uint8_t> BuildWsBinaryFrame(const uint8_t* data,
                                                  size_t size);

  std::string host_;
  int port_;
  int listen_fd_ = -1;

  std::atomic<bool> running_{false};
  std::thread accept_thread_;

  mutable std::mutex channels_mutex_;
  std::unordered_map<int, std::unique_ptr<ChannelStream>> channels_;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_MEDIA_STREAM_WS_MEDIA_SERVICE_H_
