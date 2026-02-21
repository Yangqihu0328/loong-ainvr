// Copyright 2026 Loong AI NVR Project

#include "network/flv_stream/http_flv_service.h"

#include <algorithm>
#include <chrono>

#include "network/flv_stream/flv_muxer.h"
#include "spdlog/spdlog.h"

namespace loong::network {

HttpFlvService::HttpFlvService() = default;

HttpFlvService::~HttpFlvService() {
  StopAll();
}

void HttpFlvService::RegisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  channels_[channel_id] = std::make_unique<ChannelStream>();
  spdlog::info("HttpFlvService: registered channel {}", channel_id);
}

void HttpFlvService::UnregisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  // Notify all viewers that the channel is gone
  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);
  stream->active = false;
  for (auto& weak_viewer : stream->viewers) {
    auto viewer = weak_viewer.lock();
    if (viewer) {
      std::lock_guard<std::mutex> vlock(viewer->mtx);
      viewer->active = false;
      viewer->cv.notify_all();
    }
  }

  channels_.erase(it);
  spdlog::info("HttpFlvService: unregistered channel {}", channel_id);
}

void HttpFlvService::PushFrame(int channel_id, const uint8_t* data,
                                size_t size, int64_t pts,
                                bool is_keyframe) {
  if (!running_) return;

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);

  // Set base PTS on first frame
  if (stream->base_pts < 0) {
    stream->base_pts = pts;
  }
  int64_t timestamp_ms = (pts - stream->base_pts) / 1000;  // us → ms
  if (timestamp_ms < 0) timestamp_ms = 0;

  // Extract SPS/PPS from keyframes and build sequence header
  if (is_keyframe) {
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
    if (FlvMuxer::ExtractSpsPps(data, size, sps, pps)) {
      if (sps != stream->sps || pps != stream->pps) {
        stream->sps = std::move(sps);
        stream->pps = std::move(pps);
        stream->sequence_header_tag =
            FlvMuxer::MakeSequenceHeaderTag(stream->sps, stream->pps);
        spdlog::debug("HttpFlvService: ch{} updated SPS/PPS", channel_id);
      }
    }
  }

  // Mux to FLV video tag
  auto tag = FlvMuxer::MakeVideoTag(data, size, timestamp_ms, is_keyframe);
  if (tag.empty()) return;

  // Broadcast to all active viewers
  auto viewer_it = stream->viewers.begin();
  while (viewer_it != stream->viewers.end()) {
    auto viewer = viewer_it->lock();
    if (!viewer) {
      viewer_it = stream->viewers.erase(viewer_it);
      continue;
    }

    std::lock_guard<std::mutex> vlock(viewer->mtx);
    if (!viewer->active) {
      viewer_it = stream->viewers.erase(viewer_it);
      continue;
    }

    // Wait for keyframe before sending data to new viewers
    if (!viewer->got_keyframe) {
      if (is_keyframe) {
        viewer->got_keyframe = true;
      } else {
        ++viewer_it;
        continue;
      }
    }

    // Backpressure: if queue is too large, drop non-keyframe tags
    if (viewer->pending_tags.size() >= FlvViewer::kMaxPendingTags) {
      if (!is_keyframe) {
        ++viewer_it;
        continue;
      }
      // For keyframes, drop oldest non-keyframes to make room
      while (viewer->pending_tags.size() > FlvViewer::kMaxPendingTags / 2) {
        viewer->pending_tags.pop_front();
      }
    }

    viewer->pending_tags.push_back(tag);
    viewer->cv.notify_one();
    ++viewer_it;
  }
}

std::shared_ptr<FlvViewer> HttpFlvService::CreateViewer(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return nullptr;

  auto viewer = std::make_shared<FlvViewer>();
  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);
  stream->viewers.push_back(viewer);

  spdlog::info("HttpFlvService: new viewer for channel {} (total: {})",
               channel_id, stream->viewers.size());
  return viewer;
}

void HttpFlvService::RemoveViewer(int channel_id,
                                   std::shared_ptr<FlvViewer> viewer) {
  if (!viewer) return;

  {
    std::lock_guard<std::mutex> vlock(viewer->mtx);
    viewer->active = false;
    viewer->cv.notify_all();
  }

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);

  auto& viewers = stream->viewers;
  viewers.erase(
      std::remove_if(viewers.begin(), viewers.end(),
                     [&viewer](const std::weak_ptr<FlvViewer>& wp) {
                       auto sp = wp.lock();
                       return !sp || sp == viewer;
                     }),
      viewers.end());
}

bool HttpFlvService::StreamToViewer(int channel_id,
                                     std::shared_ptr<FlvViewer> viewer,
                                     httplib::DataSink& sink) {
  if (!viewer || !running_) return false;

  // Send FLV header on first call
  if (!viewer->got_header) {
    auto header = FlvMuxer::MakeFlvHeader();
    sink.write(reinterpret_cast<const char*>(header.data()), header.size());

    // Send sequence header if available
    std::lock_guard<std::mutex> lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      std::lock_guard<std::mutex> slock(it->second->mtx);
      if (!it->second->sequence_header_tag.empty()) {
        auto& seq = it->second->sequence_header_tag;
        sink.write(reinterpret_cast<const char*>(seq.data()), seq.size());
      }
    }

    viewer->got_header = true;
    return sink.is_writable();
  }

  // Wait for pending tags
  std::unique_lock<std::mutex> vlock(viewer->mtx);
  viewer->cv.wait_for(vlock, std::chrono::milliseconds(500), [&viewer]() {
    return !viewer->pending_tags.empty() || !viewer->active;
  });

  if (!viewer->active) return false;
  if (!sink.is_writable()) return false;

  // Drain all pending tags
  while (!viewer->pending_tags.empty()) {
    auto& tag = viewer->pending_tags.front();
    if (!sink.is_writable()) return false;
    sink.write(reinterpret_cast<const char*>(tag.data()), tag.size());
    viewer->pending_tags.pop_front();
  }

  return sink.is_writable();
}

void HttpFlvService::StopAll() {
  running_ = false;

  std::lock_guard<std::mutex> lock(channels_mutex_);
  for (auto& [id, stream] : channels_) {
    std::lock_guard<std::mutex> slock(stream->mtx);
    stream->active = false;
    for (auto& weak_viewer : stream->viewers) {
      auto viewer = weak_viewer.lock();
      if (viewer) {
        std::lock_guard<std::mutex> vlock(viewer->mtx);
        viewer->active = false;
        viewer->cv.notify_all();
      }
    }
  }
}

}  // namespace loong::network
