// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_PLAYBACK_PLAYBACK_H_
#define LOONG_STORAGE_PLAYBACK_PLAYBACK_H_

#include "storage/record_index/record_index.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace loong::storage {

/// Playback speed multiplier.
enum class PlaybackSpeed {
  kHalf = -2,   // 0.5x
  kNormal = 1,  // 1x
  kDouble = 2,  // 2x
  kQuad = 4,    // 4x
  kOcta = 8,    // 8x
};

/// State of the playback session.
enum class PlaybackState {
  kIdle,
  kPlaying,
  kPaused,
  kSeeking,
  kEndOfStream,
  kError,
};

/// A decoded or raw packet from playback.
struct PlaybackFrame {
  std::vector<uint8_t> data;
  int64_t pts = 0;  // Presentation timestamp (microseconds)
  int64_t dts = 0;  // Decode timestamp
  bool is_keyframe = false;
  int width = 0;
  int height = 0;
};

/// Callback invoked for each frame during playback.
using PlaybackFrameCallback = std::function<void(const PlaybackFrame& frame)>;

/// Recording playback engine.
/// Opens MP4 segment files, supports seek, speed control, and frame-by-frame
/// stepping. Uses RecordIndex to locate segments by time range.
class Playback {
 public:
  explicit Playback(std::shared_ptr<RecordIndex> index);
  ~Playback();

  /// Open a playback session for a channel within a time range.
  /// Finds all relevant segments from the index.
  bool Open(int channel_id, int64_t start_time_ms, int64_t end_time_ms);

  /// Close the current playback session.
  void Close();

  /// Read the next packet (encoded). Returns false at end of stream.
  bool ReadNextPacket(PlaybackFrame* frame);

  /// Seek to a specific timestamp (milliseconds since epoch).
  bool Seek(int64_t timestamp_ms);

  /// Set playback speed.
  void SetSpeed(PlaybackSpeed speed);

  /// Get current playback speed.
  PlaybackSpeed GetSpeed() const { return speed_; }

  /// Get current playback state.
  PlaybackState GetState() const { return state_; }

  /// Get the current playback position (milliseconds since epoch).
  int64_t GetPosition() const { return current_position_ms_; }

  /// Get the total duration of the playback range (milliseconds).
  int64_t GetDuration() const;

  /// Step forward one frame (for frame-by-frame mode).
  bool StepForward(PlaybackFrame* frame);

  /// Check if the session is open.
  bool IsOpen() const { return state_ != PlaybackState::kIdle; }

  // Non-copyable
  Playback(const Playback&) = delete;
  Playback& operator=(const Playback&) = delete;

 private:
  bool OpenSegmentFile(const std::string& path);
  void CloseSegmentFile();
  bool AdvanceToNextSegment();
  int64_t SegmentTimeToAbsoluteMs(int64_t pts) const;

  std::shared_ptr<RecordIndex> index_;

  // Segment list for the playback range
  std::vector<SegmentInfo> segments_;
  size_t current_segment_idx_ = 0;

  // FFmpeg demuxer state
  AVFormatContext* fmt_ctx_ = nullptr;
  int video_stream_idx_ = -1;

  // Playback control
  PlaybackState state_ = PlaybackState::kIdle;
  PlaybackSpeed speed_ = PlaybackSpeed::kNormal;
  int64_t current_position_ms_ = 0;

  // Time range
  int channel_id_ = -1;
  int64_t start_time_ms_ = 0;
  int64_t end_time_ms_ = 0;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_PLAYBACK_PLAYBACK_H_
