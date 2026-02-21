// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_RECORD_WRITER_RECORD_WRITER_H_
#define LOONG_STORAGE_RECORD_WRITER_RECORD_WRITER_H_

#include "core/common/types.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace loong::storage {

/// Configuration for recording.
struct RecordConfig {
  std::string base_path = "/recordings";
  int segment_duration_sec = 1800;  // 30 minutes
  CodecType codec = CodecType::kH264;
  int width = 1920;
  int height = 1080;
  int framerate = 25;
  int bitrate_kbps = 4000;
};

/// Per-channel recording writer.
/// Writes encoded frames to segmented MP4 files via FFmpeg muxer.
class RecordWriter {
 public:
  RecordWriter(int channel_id, RecordConfig config);
  ~RecordWriter();

  /// Write an encoded frame to the current segment.
  bool WriteFrame(const uint8_t* data, size_t size, int64_t pts, int64_t dts,
                  bool is_keyframe);

  /// Force start a new segment file.
  void StartNewSegment();

  /// Flush and close the current segment.
  void Close();

  /// Get the current segment file path.
  std::string CurrentSegmentPath() const;

  /// Get total bytes written.
  int64_t TotalBytesWritten() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_written_;
  }

  // Non-copyable
  RecordWriter(const RecordWriter&) = delete;
  RecordWriter& operator=(const RecordWriter&) = delete;

 private:
  bool OpenSegment();
  void CloseSegment();
  std::string GenerateSegmentPath() const;

  mutable std::mutex mutex_;
  int channel_id_;
  RecordConfig config_;
  AVFormatContext* fmt_ctx_ = nullptr;
  AVStream* video_stream_ = nullptr;
  int64_t segment_start_pts_ = -1;
  int64_t frame_count_ = 0;
  int64_t total_bytes_written_ = 0;
  std::string current_segment_path_;
  bool is_open_ = false;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_RECORD_WRITER_RECORD_WRITER_H_
