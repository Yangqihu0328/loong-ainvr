// Copyright 2026 Loong AI NVR Project

#include "network/hls_stream/hls_service.h"

#include "network/hls_stream/ts_muxer.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace loong::network {

HlsService::HlsService(const HlsConfig& config) : config_(config) {}

HlsService::~HlsService() { StopAll(); }

void HlsService::RegisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto stream = std::make_unique<ChannelHls>();
  stream->muxer = std::make_unique<TsMuxer>();
  channels_[channel_id] = std::move(stream);
  spdlog::info("HlsService: registered channel {}", channel_id);
}

void HlsService::UnregisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  {
    std::lock_guard<std::mutex> slock(it->second->mtx);
    it->second->active = false;
  }

  channels_.erase(it);
  spdlog::info("HlsService: unregistered channel {}", channel_id);
}

void HlsService::PushFrame(int channel_id, const uint8_t* data, size_t size,
                           int64_t pts, bool is_keyframe) {
  if (!running_) return;

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  auto& stream = *it->second;
  std::lock_guard<std::mutex> slock(stream.mtx);

  if (!stream.active) return;

  // Wait for the first keyframe before starting a segment
  if (stream.waiting_for_keyframe) {
    if (!is_keyframe) return;
    stream.waiting_for_keyframe = false;
  }

  // Check if we should start a new segment (keyframe + duration exceeded)
  if (is_keyframe && stream.current_segment && stream.segment_start_pts >= 0) {
    int64_t elapsed_us = pts - stream.segment_start_pts;
    int64_t target_us =
        static_cast<int64_t>(config_.segment_duration_ms) * 1000;
    if (elapsed_us >= target_us) {
      FinalizeSegment(stream, pts);
    }
  }

  // Start a new segment if needed
  if (!stream.current_segment) {
    stream.current_segment = std::make_shared<std::vector<uint8_t>>();
    stream.current_segment->reserve(
        static_cast<size_t>(config_.max_segment_size_kb) * 1024);
    stream.segment_start_pts = pts;

    // Write PAT + PMT at segment start
    stream.muxer->Reset();
    auto psi = stream.muxer->WritePsiTables();
    stream.current_segment->insert(stream.current_segment->end(), psi.begin(),
                                   psi.end());
  }

  // Mux the access unit into TS packets
  auto ts_data = stream.muxer->WriteAccessUnit(data, size, pts, is_keyframe);
  if (!ts_data.empty()) {
    stream.current_segment->insert(stream.current_segment->end(),
                                   ts_data.begin(), ts_data.end());
  }
}

void HlsService::FinalizeSegment(ChannelHls& stream, int64_t end_pts) {
  if (!stream.current_segment || stream.current_segment->empty()) return;

  HlsSegment segment;
  segment.sequence = stream.next_sequence++;
  segment.data = std::move(stream.current_segment);

  if (stream.segment_start_pts >= 0 && end_pts > stream.segment_start_pts) {
    segment.duration_sec =
        static_cast<double>(end_pts - stream.segment_start_pts) / 1'000'000.0;
  } else {
    segment.duration_sec =
        static_cast<double>(config_.segment_duration_ms) / 1000.0;
  }

  stream.segments.push_back(std::move(segment));

  // Sliding window: remove old segments
  while (stream.segments.size() > static_cast<size_t>(config_.max_segments)) {
    stream.segments.erase(stream.segments.begin());
  }

  stream.current_segment = nullptr;
  stream.segment_start_pts = -1;
}

std::string HlsService::GeneratePlaylist(int channel_id) const {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return {};

  auto& stream = *it->second;
  std::lock_guard<std::mutex> slock(stream.mtx);

  if (stream.segments.empty()) return {};

  // Compute max segment duration for EXT-X-TARGETDURATION
  double max_duration = 0.0;
  for (const auto& seg : stream.segments) {
    max_duration = std::max(max_duration, seg.duration_sec);
  }
  int target_duration = static_cast<int>(max_duration) + 1;

  std::ostringstream oss;
  oss << std::fixed << std::setprecision(3);
  oss << "#EXTM3U\n";
  oss << "#EXT-X-VERSION:3\n";
  oss << "#EXT-X-TARGETDURATION:" << target_duration << "\n";
  oss << "#EXT-X-MEDIA-SEQUENCE:" << stream.segments.front().sequence << "\n";

  for (const auto& seg : stream.segments) {
    oss << "#EXTINF:" << seg.duration_sec << ",\n";
    oss << seg.sequence << ".ts\n";
  }

  return oss.str();
}

std::shared_ptr<std::vector<uint8_t>> HlsService::GetSegment(
    int channel_id, uint64_t sequence) const {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return nullptr;

  auto& stream = *it->second;
  std::lock_guard<std::mutex> slock(stream.mtx);

  for (const auto& seg : stream.segments) {
    if (seg.sequence == sequence) {
      return seg.data;
    }
  }
  return nullptr;
}

bool HlsService::HasChannel(int channel_id) const {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  return channels_.find(channel_id) != channels_.end();
}

void HlsService::UpdateConfig(const HlsConfig& new_config) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  config_ = new_config;
  spdlog::info("HlsService: config updated (segment={}ms, window={})",
               config_.segment_duration_ms, config_.max_segments);
}

void HlsService::StopAll() {
  running_ = false;

  std::lock_guard<std::mutex> lock(channels_mutex_);
  for (auto& [id, stream] : channels_) {
    std::lock_guard<std::mutex> slock(stream->mtx);
    stream->active = false;
  }
}

}  // namespace loong::network
