// Copyright 2026 Loong AI NVR Project

#include "storage/recorder/recorder.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <sstream>

namespace loong::storage {

Recorder::Recorder(std::shared_ptr<RecordIndex> index,
                   std::shared_ptr<Indexer> indexer)
    : index_(std::move(index)), indexer_(std::move(indexer)) {}

Recorder::~Recorder() { StopAll(); }

bool Recorder::StartRecording(int channel_id, const RecordConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = recorders_.find(channel_id);
  if (it != recorders_.end() && it->second.state.is_recording) {
    spdlog::warn("Recorder: ch{} is already recording", channel_id);
    return false;
  }

  ChannelRecorder cr;
  cr.config = config;
  cr.writer = std::make_unique<RecordWriter>(channel_id, config);
  cr.state.is_recording = true;
  cr.state.segment_start_time_ms = NowMs();

  recorders_[channel_id] = std::move(cr);

  spdlog::info("Recorder: started recording ch{}", channel_id);
  return true;
}

bool Recorder::StopRecording(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = recorders_.find(channel_id);
  if (it == recorders_.end() || !it->second.state.is_recording) {
    spdlog::warn("Recorder: ch{} is not recording", channel_id);
    return false;
  }

  FinalizeSegment(it->second);
  it->second.writer->Close();
  it->second.state.is_recording = false;

  spdlog::info("Recorder: stopped recording ch{} ({} frames)", channel_id,
               it->second.state.total_frames);

  recorders_.erase(it);
  return true;
}

bool Recorder::IsRecording(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = recorders_.find(channel_id);
  return it != recorders_.end() && it->second.state.is_recording;
}

bool Recorder::WriteFrame(int channel_id, const uint8_t* data, size_t size,
                          int64_t pts, int64_t dts, bool is_keyframe) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = recorders_.find(channel_id);
  if (it == recorders_.end() || !it->second.state.is_recording) {
    return false;
  }

  auto& cr = it->second;
  std::string old_path = cr.writer->CurrentSegmentPath();

  bool ok = cr.writer->WriteFrame(data, size, pts, dts, is_keyframe);
  if (!ok) return false;

  std::string new_path = cr.writer->CurrentSegmentPath();

  // Detect segment rotation: path changed means a new segment was opened
  if (new_path != old_path) {
    // Finalize old segment if it existed
    if (!old_path.empty() && cr.state.current_segment_id >= 0) {
      FinalizeSegment(cr);
    }

    // Register new segment in the index
    SegmentInfo seg;
    seg.channel_id = channel_id;
    seg.start_time = NowMs();
    seg.end_time = seg.start_time;
    seg.file_path = new_path;
    seg.codec = (cr.config.codec == CodecType::kH264) ? "h264" : "h265";
    seg.resolution = std::to_string(cr.config.width) + "x" +
                     std::to_string(cr.config.height);

    int64_t seg_id = index_->InsertSegment(seg);
    cr.state.current_segment_id = seg_id;
    cr.state.segment_start_time_ms = seg.start_time;
  }

  ++cr.state.total_frames;
  return true;
}

void Recorder::SetSchedule(int channel_id, const RecordSchedule& schedule) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = recorders_.find(channel_id);
  if (it != recorders_.end()) {
    it->second.schedule = std::make_unique<RecordSchedule>(schedule);
    spdlog::info(
        "Recorder: schedule set for ch{} ({:02d}:{:02d}-{:02d}:{:02d})",
        channel_id, schedule.start_hour, schedule.start_minute,
        schedule.end_hour, schedule.end_minute);
  }
}

void Recorder::RemoveSchedule(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = recorders_.find(channel_id);
  if (it != recorders_.end()) {
    it->second.schedule.reset();
    spdlog::info("Recorder: schedule removed for ch{}", channel_id);
  }
}

bool Recorder::TriggerEventRecording(int channel_id, int pre_sec,
                                     int post_sec) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = recorders_.find(channel_id);
  if (it == recorders_.end()) {
    spdlog::warn("Recorder: ch{} not found for event recording", channel_id);
    return false;
  }

  // If already recording, the event is captured in the current segment.
  // Insert an event marker in the index.
  if (it->second.state.is_recording) {
    EventInfo event;
    event.channel_id = channel_id;
    event.segment_id = it->second.state.current_segment_id;
    event.event_type = "event_trigger";
    event.event_time = NowMs();
    event.metadata = "{\"pre_sec\":" + std::to_string(pre_sec) +
                     ",\"post_sec\":" + std::to_string(post_sec) + "}";

    if (indexer_) {
      indexer_->QueueEventInsert(event);
    } else {
      index_->InsertEvent(event);
    }

    spdlog::info("Recorder: event trigger for ch{} (pre={}s, post={}s)",
                 channel_id, pre_sec, post_sec);
    return true;
  }

  spdlog::warn("Recorder: ch{} not recording, cannot trigger event",
               channel_id);
  return false;
}

void Recorder::StopAll() {
  std::lock_guard<std::mutex> lock(mutex_);

  for (auto& [channel_id, cr] : recorders_) {
    if (cr.state.is_recording) {
      FinalizeSegment(cr);
      cr.writer->Close();
      cr.state.is_recording = false;
      spdlog::info("Recorder: stopped ch{} during StopAll", channel_id);
    }
  }
  recorders_.clear();
}

RecordingState Recorder::GetState(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = recorders_.find(channel_id);
  if (it != recorders_.end()) {
    return it->second.state;
  }
  return {};
}

std::vector<int> Recorder::GetActiveChannels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<int> channels;
  channels.reserve(recorders_.size());
  for (const auto& [id, cr] : recorders_) {
    if (cr.state.is_recording) {
      channels.push_back(id);
    }
  }
  return channels;
}

int64_t Recorder::NowMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void Recorder::FinalizeSegment(ChannelRecorder& cr) {
  if (cr.state.current_segment_id < 0) return;

  int64_t end_time = NowMs();
  int64_t bytes = cr.writer->TotalBytesWritten();

  if (indexer_) {
    indexer_->QueueSegmentUpdate(cr.state.current_segment_id, end_time, bytes);
  } else {
    index_->UpdateSegmentEnd(cr.state.current_segment_id, end_time, bytes);
  }

  cr.state.current_segment_id = -1;
}

}  // namespace loong::storage
