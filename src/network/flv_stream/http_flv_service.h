// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_FLV_STREAM_HTTP_FLV_SERVICE_H_
#define LOONG_NETWORK_FLV_STREAM_HTTP_FLV_SERVICE_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Suppress warnings from third-party header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <httplib.h>
#pragma GCC diagnostic pop

namespace loong::network {

/// Viewer — represents a single connected FLV streaming client.
/// Each viewer has its own pending tag queue to handle different
/// consumption speeds independently.
struct FlvViewer {
  std::mutex mtx;
  std::condition_variable cv;
  std::deque<std::vector<uint8_t>> pending_tags;
  bool active = true;
  bool got_header = false;
  bool got_keyframe = false;

  static constexpr size_t kMaxPendingTags = 600;
};

/// HTTP-FLV streaming service for low-latency web preview.
/// Manages per-channel frame muxing and per-viewer data delivery.
///
/// Architecture:
///   OutputStage → PushFrame() → FLV muxer → broadcast to all viewers
///   HTTP GET /live/ch{id}.flv → CreateViewer() → StreamToViewer() loop
class HttpFlvService : public std::enable_shared_from_this<HttpFlvService> {
 public:
  HttpFlvService();
  ~HttpFlvService();

  /// Register a channel for FLV streaming.
  void RegisterChannel(int channel_id);

  /// Unregister a channel.
  void UnregisterChannel(int channel_id);

  /// Push an encoded H.264 frame (Annex B format) for a channel.
  /// Called from OutputStage when a new encoded frame is produced.
  void PushFrame(int channel_id, const uint8_t* data, size_t size, int64_t pts,
                 bool is_keyframe);

  /// Create a new viewer for a channel.
  /// Returns nullptr if the channel is not registered.
  std::shared_ptr<FlvViewer> CreateViewer(int channel_id);

  /// Remove a viewer from a channel.
  void RemoveViewer(int channel_id, std::shared_ptr<FlvViewer> viewer);

  /// Stream data to a viewer via cpp-httplib DataSink.
  /// Called repeatedly by the chunked content provider.
  /// Returns true to continue streaming, false to end.
  bool StreamToViewer(int channel_id, std::shared_ptr<FlvViewer> viewer,
                      httplib::DataSink& sink);

  /// Stop all streaming (called during shutdown).
  void StopAll();

 private:
  /// Per-channel streaming state.
  struct ChannelStream {
    std::mutex mtx;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    std::vector<uint8_t> sequence_header_tag;
    int64_t base_pts = -1;
    bool active = true;
    std::vector<std::weak_ptr<FlvViewer>> viewers;
  };

  std::mutex channels_mutex_;
  std::unordered_map<int, std::unique_ptr<ChannelStream>> channels_;
  std::atomic<bool> running_{true};
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_FLV_STREAM_HTTP_FLV_SERVICE_H_
