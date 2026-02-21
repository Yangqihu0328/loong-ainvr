// Copyright 2026 Loong AI NVR Project

#include "video_input/stream_manager/stream_manager.h"

#include "spdlog/spdlog.h"

namespace loong {
namespace video_input {

StreamManager::StreamManager() = default;

StreamManager::~StreamManager() { StopAll(); }

bool StreamManager::AddStream(int channel_id, const RtspClientConfig& config) {
  std::unique_lock lock(streams_mutex_);

  if (streams_.size() >= static_cast<size_t>(kMaxStreams)) {
    spdlog::error("StreamManager: max streams ({}) reached", kMaxStreams);
    return false;
  }

  if (streams_.count(channel_id) > 0) {
    spdlog::warn("StreamManager: stream {} already exists", channel_id);
    return false;
  }

  auto entry = std::make_unique<StreamEntry>();
  entry->config = config;
  entry->client = std::make_unique<RtspClient>();

  // Wire internal callbacks.
  entry->client->SetPacketCallback(MakePacketCallback());
  entry->client->SetStateCallback(MakeStateCallback());

  streams_[channel_id] = std::move(entry);
  spdlog::info("StreamManager: stream {} added ({})", channel_id, config.url);
  return true;
}

bool StreamManager::RemoveStream(int channel_id) {
  std::unique_lock lock(streams_mutex_);
  auto it = streams_.find(channel_id);
  if (it == streams_.end()) {
    spdlog::warn("StreamManager: stream {} not found", channel_id);
    return false;
  }

  auto& entry = it->second;
  if (entry->started) {
    entry->client->Close();
  }

  streams_.erase(it);
  spdlog::info("StreamManager: stream {} removed", channel_id);
  return true;
}

bool StreamManager::StartStream(int channel_id) {
  std::unique_lock lock(streams_mutex_);
  auto it = streams_.find(channel_id);
  if (it == streams_.end()) {
    spdlog::warn("StreamManager: stream {} not found", channel_id);
    return false;
  }

  auto& entry = it->second;
  if (entry->started) {
    spdlog::debug("StreamManager: stream {} already started", channel_id);
    return true;
  }

  if (!entry->client->Open(entry->config, channel_id)) {
    spdlog::error("StreamManager: failed to start stream {}", channel_id);
    return false;
  }

  entry->started = true;
  spdlog::info("StreamManager: stream {} started", channel_id);
  return true;
}

bool StreamManager::StopStream(int channel_id) {
  std::unique_lock lock(streams_mutex_);
  auto it = streams_.find(channel_id);
  if (it == streams_.end()) {
    spdlog::warn("StreamManager: stream {} not found", channel_id);
    return false;
  }

  auto& entry = it->second;
  if (!entry->started) {
    return true;
  }

  entry->client->Close();
  entry->started = false;
  spdlog::info("StreamManager: stream {} stopped", channel_id);
  return true;
}

bool StreamManager::UpdateStream(int channel_id,
                                 const RtspClientConfig& new_config,
                                 bool restart) {
  std::unique_lock lock(streams_mutex_);
  auto it = streams_.find(channel_id);
  if (it == streams_.end()) {
    spdlog::warn("StreamManager: stream {} not found", channel_id);
    return false;
  }

  auto& entry = it->second;
  bool was_started = entry->started;

  // Stop if running.
  if (entry->started) {
    entry->client->Close();
    entry->started = false;
  }

  // Apply new configuration.
  entry->config = new_config;

  // Create a fresh client with the new config.
  entry->client = std::make_unique<RtspClient>();
  entry->client->SetPacketCallback(MakePacketCallback());
  entry->client->SetStateCallback(MakeStateCallback());

  // Restart if requested and was previously running.
  if (restart && was_started) {
    if (!entry->client->Open(entry->config, channel_id)) {
      spdlog::error("StreamManager: failed to restart stream {} after update",
                    channel_id);
      return false;
    }
    entry->started = true;
  }

  spdlog::info("StreamManager: stream {} updated", channel_id);
  return true;
}

void StreamManager::StartAll() {
  std::vector<int> ids;
  {
    std::shared_lock lock(streams_mutex_);
    for (const auto& [id, entry] : streams_) {
      if (!entry->started) {
        ids.push_back(id);
      }
    }
  }
  for (int id : ids) {
    StartStream(id);
  }
}

void StreamManager::StopAll() {
  std::vector<int> ids;
  {
    std::shared_lock lock(streams_mutex_);
    for (const auto& [id, entry] : streams_) {
      if (entry->started) {
        ids.push_back(id);
      }
    }
  }
  for (int id : ids) {
    StopStream(id);
  }
}

StreamStats StreamManager::GetStats(int channel_id) const {
  std::shared_lock lock(streams_mutex_);
  auto it = streams_.find(channel_id);
  if (it == streams_.end()) {
    StreamStats empty;
    empty.channel_id = -1;
    return empty;
  }

  const auto& entry = it->second;
  StreamStats stats;
  stats.channel_id = channel_id;
  stats.url = entry->config.url;
  stats.state = entry->client->GetState();
  stats.stream_info = entry->client->GetStreamInfo();
  stats.packets_received = entry->client->PacketsReceived();
  stats.reconnect_attempts = entry->client->ReconnectAttempts();
  return stats;
}

std::vector<StreamStats> StreamManager::GetAllStats() const {
  std::shared_lock lock(streams_mutex_);
  std::vector<StreamStats> result;
  result.reserve(streams_.size());

  for (const auto& [id, entry] : streams_) {
    StreamStats stats;
    stats.channel_id = id;
    stats.url = entry->config.url;
    stats.state = entry->client->GetState();
    stats.stream_info = entry->client->GetStreamInfo();
    stats.packets_received = entry->client->PacketsReceived();
    stats.reconnect_attempts = entry->client->ReconnectAttempts();
    result.push_back(stats);
  }
  return result;
}

bool StreamManager::HasStream(int channel_id) const {
  std::shared_lock lock(streams_mutex_);
  return streams_.count(channel_id) > 0;
}

size_t StreamManager::StreamCount() const {
  std::shared_lock lock(streams_mutex_);
  return streams_.size();
}

void StreamManager::SetPacketCallback(PacketCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  packet_callback_ = std::move(callback);
}

void StreamManager::SetStateCallback(StreamStateCallback callback) {
  std::lock_guard<std::mutex> lock(callback_mutex_);
  state_callback_ = std::move(callback);
}

// ---------- Private helpers ----------

PacketCallback StreamManager::MakePacketCallback() {
  return [this](int channel_id, const uint8_t* data, size_t size, int64_t pts,
                int64_t dts, bool is_keyframe, CodecType codec) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (packet_callback_) {
      packet_callback_(channel_id, data, size, pts, dts, is_keyframe, codec);
    }
  };
}

StateCallback StreamManager::MakeStateCallback() {
  return [this](int channel_id, RtspConnectionState new_state,
                const std::string& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (state_callback_) {
      state_callback_(channel_id, new_state, message);
    }
  };
}

}  // namespace video_input
}  // namespace loong
