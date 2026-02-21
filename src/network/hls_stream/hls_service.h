// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_HLS_STREAM_HLS_SERVICE_H_
#define LOONG_NETWORK_HLS_STREAM_HLS_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loong::network {

class TsMuxer;

/// Configuration for HLS streaming behavior.
struct HlsConfig {
  int segment_duration_ms = 2000;  // Target segment duration in ms
  int max_segments = 5;            // Sliding window size in the M3U8 playlist
  int max_segment_size_kb = 4096;  // Safety cap per segment (4 MB)
};

/// A single HLS TS segment stored in memory.
struct HlsSegment {
  uint64_t sequence = 0;
  double duration_sec = 0.0;
  std::shared_ptr<std::vector<uint8_t>> data;
};

/// HLS live streaming service for one or more channels.
///
/// Architecture:
///   OutputStage → PushFrame() → TsMuxer → segment accumulation
///   HTTP GET /live/ch{id}/index.m3u8 → GeneratePlaylist()
///   HTTP GET /live/ch{id}/{seq}.ts   → GetSegment()
///
/// Segments are stored in a sliding-window ring buffer (in memory).
/// New segments are created at keyframe boundaries that exceed the
/// target segment duration, ensuring proper seek behavior.
class HlsService : public std::enable_shared_from_this<HlsService> {
 public:
  explicit HlsService(const HlsConfig& config = {});
  ~HlsService();

  /// Register a channel for HLS streaming.
  void RegisterChannel(int channel_id);

  /// Unregister a channel.
  void UnregisterChannel(int channel_id);

  /// Push an encoded H.264 frame (Annex B format) for a channel.
  /// Called from OutputStage when a new encoded frame is produced.
  void PushFrame(int channel_id, const uint8_t* data, size_t size, int64_t pts,
                 bool is_keyframe);

  /// Generate an M3U8 playlist for the given channel.
  /// Returns empty string if the channel is not registered or has no segments.
  std::string GeneratePlaylist(int channel_id) const;

  /// Get a specific TS segment by sequence number.
  /// Returns nullptr if not found.
  std::shared_ptr<std::vector<uint8_t>> GetSegment(int channel_id,
                                                   uint64_t sequence) const;

  /// Check if a channel is registered.
  bool HasChannel(int channel_id) const;

  /// Update runtime configuration (e.g., from hot-reload).
  void UpdateConfig(const HlsConfig& new_config);

  /// Stop all streaming.
  void StopAll();

 private:
  /// Per-channel HLS state.
  struct ChannelHls {
    mutable std::mutex mtx;
    std::unique_ptr<TsMuxer> muxer;
    std::vector<HlsSegment> segments;
    uint64_t next_sequence = 0;

    // Current segment accumulation
    std::shared_ptr<std::vector<uint8_t>> current_segment;
    int64_t segment_start_pts = -1;
    bool waiting_for_keyframe = true;
    bool active = true;
  };

  void FinalizeSegment(ChannelHls& stream, int64_t end_pts);

  HlsConfig config_;
  mutable std::mutex channels_mutex_;
  std::unordered_map<int, std::unique_ptr<ChannelHls>> channels_;
  std::atomic<bool> running_{true};
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_HLS_STREAM_HLS_SERVICE_H_
