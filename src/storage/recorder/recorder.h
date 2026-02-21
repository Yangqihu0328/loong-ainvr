// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_RECORDER_RECORDER_H_
#define LOONG_STORAGE_RECORDER_RECORDER_H_

#include "storage/indexer/indexer.h"
#include "storage/record_index/record_index.h"
#include "storage/record_writer/record_writer.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace loong::storage {

/// Schedule entry for time-based recording.
struct RecordSchedule {
  int start_hour = 0;  // 0-23
  int start_minute = 0;
  int end_hour = 23;
  int end_minute = 59;
  bool weekdays[7] = {true, true, true, true, true, true, true};
};

/// Per-channel recording state tracked by the Recorder.
struct RecordingState {
  bool is_recording = false;
  int64_t current_segment_id = -1;
  int64_t segment_start_time_ms = 0;
  int64_t total_frames = 0;
};

/// Recording manager that orchestrates multiple RecordWriter instances.
/// Handles per-channel start/stop, schedule-based and event-triggered
/// recording, and automatic segment tracking via the Indexer.
class Recorder {
 public:
  Recorder(std::shared_ptr<RecordIndex> index,
           std::shared_ptr<Indexer> indexer);
  ~Recorder();

  /// Start recording for a channel.
  bool StartRecording(int channel_id, const RecordConfig& config);

  /// Stop recording for a channel.
  bool StopRecording(int channel_id);

  /// Check if a channel is currently recording.
  bool IsRecording(int channel_id) const;

  /// Write an encoded frame to the channel's recorder.
  /// Automatically manages segment tracking in the index.
  bool WriteFrame(int channel_id, const uint8_t* data, size_t size, int64_t pts,
                  int64_t dts, bool is_keyframe);

  /// Set a recording schedule for a channel.
  void SetSchedule(int channel_id, const RecordSchedule& schedule);

  /// Remove the recording schedule for a channel.
  void RemoveSchedule(int channel_id);

  /// Trigger event-based recording for a channel.
  /// Records pre_sec seconds before and post_sec seconds after the event.
  bool TriggerEventRecording(int channel_id, int pre_sec, int post_sec);

  /// Stop all active recordings.
  void StopAll();

  /// Get recording state for a channel.
  RecordingState GetState(int channel_id) const;

  /// Get IDs of all currently recording channels.
  std::vector<int> GetActiveChannels() const;

  // Non-copyable
  Recorder(const Recorder&) = delete;
  Recorder& operator=(const Recorder&) = delete;

 private:
  struct ChannelRecorder {
    std::unique_ptr<RecordWriter> writer;
    RecordConfig config;
    RecordingState state;
    std::unique_ptr<RecordSchedule> schedule;
  };

  int64_t NowMs() const;
  void FinalizeSegment(ChannelRecorder& cr);

  mutable std::mutex mutex_;
  std::shared_ptr<RecordIndex> index_;
  std::shared_ptr<Indexer> indexer_;
  std::unordered_map<int, ChannelRecorder> recorders_;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_RECORDER_RECORDER_H_
