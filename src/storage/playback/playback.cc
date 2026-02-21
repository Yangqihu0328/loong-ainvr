// Copyright 2026 Loong AI NVR Project

#include "storage/playback/playback.h"

#include "spdlog/spdlog.h"

#include <algorithm>

namespace loong::storage {

Playback::Playback(std::shared_ptr<RecordIndex> index)
    : index_(std::move(index)) {}

Playback::~Playback() { Close(); }

bool Playback::Open(int channel_id, int64_t start_time_ms,
                    int64_t end_time_ms) {
  if (state_ != PlaybackState::kIdle) {
    Close();
  }

  channel_id_ = channel_id;
  start_time_ms_ = start_time_ms;
  end_time_ms_ = end_time_ms;

  // Query segments covering the requested time range
  segments_ = index_->QuerySegments(channel_id, start_time_ms, end_time_ms);
  if (segments_.empty()) {
    spdlog::warn("Playback: no segments found for ch{} in [{}, {}]", channel_id,
                 start_time_ms, end_time_ms);
    state_ = PlaybackState::kError;
    return false;
  }

  spdlog::info("Playback: found {} segments for ch{}", segments_.size(),
               channel_id);

  // Open the first segment
  current_segment_idx_ = 0;
  if (!OpenSegmentFile(segments_[0].file_path)) {
    state_ = PlaybackState::kError;
    return false;
  }

  state_ = PlaybackState::kPlaying;
  current_position_ms_ = segments_[0].start_time;
  return true;
}

void Playback::Close() {
  CloseSegmentFile();
  segments_.clear();
  current_segment_idx_ = 0;
  state_ = PlaybackState::kIdle;
  current_position_ms_ = 0;
  channel_id_ = -1;
}

bool Playback::ReadNextPacket(PlaybackFrame* frame) {
  if (state_ != PlaybackState::kPlaying) {
    return false;
  }

  AVPacket* pkt = av_packet_alloc();
  if (!pkt) return false;

  while (true) {
    int ret = av_read_frame(fmt_ctx_, pkt);
    if (ret < 0) {
      av_packet_free(&pkt);

      // Try next segment
      if (!AdvanceToNextSegment()) {
        state_ = PlaybackState::kEndOfStream;
        return false;
      }

      pkt = av_packet_alloc();
      if (!pkt) return false;
      continue;
    }

    // Only process the video stream
    if (pkt->stream_index != video_stream_idx_) {
      av_packet_unref(pkt);
      continue;
    }

    // Apply speed: for >1x speed, skip non-keyframes
    int speed_val = static_cast<int>(speed_);
    if (speed_val > 1 && !(pkt->flags & AV_PKT_FLAG_KEY)) {
      av_packet_unref(pkt);
      continue;
    }

    // Build output frame
    frame->data.assign(pkt->data, pkt->data + pkt->size);
    frame->is_keyframe = (pkt->flags & AV_PKT_FLAG_KEY) != 0;

    // Convert PTS to absolute timestamp
    AVStream* st = fmt_ctx_->streams[video_stream_idx_];
    int64_t pts_us = av_rescale_q(pkt->pts, st->time_base, {1, 1000000});
    frame->pts = pts_us;
    frame->dts = av_rescale_q(pkt->dts, st->time_base, {1, 1000000});

    // Update position: segment base time + relative PTS
    current_position_ms_ = SegmentTimeToAbsoluteMs(pts_us);

    frame->width = st->codecpar->width;
    frame->height = st->codecpar->height;

    av_packet_free(&pkt);
    return true;
  }
}

bool Playback::Seek(int64_t timestamp_ms) {
  if (state_ == PlaybackState::kIdle) return false;

  PlaybackState prev_state = state_;
  state_ = PlaybackState::kSeeking;

  // Find the segment containing this timestamp
  size_t target_idx = 0;
  bool found = false;
  for (size_t i = 0; i < segments_.size(); ++i) {
    if (timestamp_ms >= segments_[i].start_time &&
        timestamp_ms <= segments_[i].end_time) {
      target_idx = i;
      found = true;
      break;
    }
  }

  if (!found) {
    // Seek to the closest segment
    for (size_t i = 0; i < segments_.size(); ++i) {
      if (segments_[i].start_time > timestamp_ms) {
        target_idx = (i > 0) ? i - 1 : 0;
        found = true;
        break;
      }
    }
    if (!found) {
      target_idx = segments_.size() - 1;
    }
  }

  // Open the target segment if different from current
  if (target_idx != current_segment_idx_) {
    CloseSegmentFile();
    current_segment_idx_ = target_idx;
    if (!OpenSegmentFile(segments_[target_idx].file_path)) {
      state_ = PlaybackState::kError;
      return false;
    }
  }

  // Seek within the segment file
  int64_t relative_ms = timestamp_ms - segments_[target_idx].start_time;
  if (relative_ms < 0) relative_ms = 0;

  AVStream* st = fmt_ctx_->streams[video_stream_idx_];
  int64_t seek_ts = av_rescale_q(relative_ms * 1000,  // ms to us
                                 {1, 1000000}, st->time_base);

  int ret =
      av_seek_frame(fmt_ctx_, video_stream_idx_, seek_ts, AVSEEK_FLAG_BACKWARD);
  if (ret < 0) {
    spdlog::warn("Playback: seek failed for ts={}", timestamp_ms);
    state_ = prev_state;
    return false;
  }

  current_position_ms_ = timestamp_ms;
  state_ = PlaybackState::kPlaying;
  spdlog::debug("Playback: seeked to {}ms", timestamp_ms);
  return true;
}

void Playback::SetSpeed(PlaybackSpeed speed) {
  speed_ = speed;
  spdlog::debug("Playback: speed set to {}x", static_cast<int>(speed));
}

int64_t Playback::GetDuration() const {
  if (segments_.empty()) return 0;
  return segments_.back().end_time - segments_.front().start_time;
}

bool Playback::StepForward(PlaybackFrame* frame) {
  if (state_ != PlaybackState::kPlaying && state_ != PlaybackState::kPaused) {
    return false;
  }

  // Temporarily set normal speed to get exactly one frame
  PlaybackSpeed saved = speed_;
  speed_ = PlaybackSpeed::kNormal;
  bool ok = ReadNextPacket(frame);
  speed_ = saved;

  if (ok) {
    state_ = PlaybackState::kPaused;
  }
  return ok;
}

bool Playback::OpenSegmentFile(const std::string& path) {
  CloseSegmentFile();

  int ret = avformat_open_input(&fmt_ctx_, path.c_str(), nullptr, nullptr);
  if (ret < 0) {
    spdlog::error("Playback: failed to open '{}'", path);
    return false;
  }

  ret = avformat_find_stream_info(fmt_ctx_, nullptr);
  if (ret < 0) {
    spdlog::error("Playback: failed to find stream info in '{}'", path);
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  // Find the video stream
  video_stream_idx_ = -1;
  for (unsigned int i = 0; i < fmt_ctx_->nb_streams; ++i) {
    if (fmt_ctx_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
      video_stream_idx_ = static_cast<int>(i);
      break;
    }
  }

  if (video_stream_idx_ < 0) {
    spdlog::error("Playback: no video stream in '{}'", path);
    avformat_close_input(&fmt_ctx_);
    return false;
  }

  spdlog::debug("Playback: opened segment '{}'", path);
  return true;
}

void Playback::CloseSegmentFile() {
  if (fmt_ctx_) {
    avformat_close_input(&fmt_ctx_);
    fmt_ctx_ = nullptr;
  }
  video_stream_idx_ = -1;
}

bool Playback::AdvanceToNextSegment() {
  CloseSegmentFile();

  ++current_segment_idx_;
  if (current_segment_idx_ >= segments_.size()) {
    return false;
  }

  spdlog::debug("Playback: advancing to segment {}/{}",
                current_segment_idx_ + 1, segments_.size());
  return OpenSegmentFile(segments_[current_segment_idx_].file_path);
}

int64_t Playback::SegmentTimeToAbsoluteMs(int64_t pts_us) const {
  if (current_segment_idx_ >= segments_.size()) return 0;

  const auto& seg = segments_[current_segment_idx_];
  // pts_us is the PTS in microseconds relative to the segment start
  int64_t relative_ms = pts_us / 1000;
  return seg.start_time + relative_ms;
}

}  // namespace loong::storage
