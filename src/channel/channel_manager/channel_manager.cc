// Copyright 2026 Loong AI NVR Project

#include "channel/channel_manager/channel_manager.h"

#include "channel/channel_monitor/channel_monitor.h"
#include "channel/channel_pipeline/channel_orchestrator.h"
#include "channel/channel_store/channel_store.h"
#include "network/flv_stream/http_flv_service.h"
#include "network/hls_stream/hls_service.h"
#include "network/media_stream/ws_media_service.h"
#include "network/rtsp_server/rtsp_server.h"
#include "network/webrtc/webrtc_service.h"
#include "pipeline_stages/input_stage/input_stage.h"
#include "rules/rule_engine/rule_engine.h"
#include "spdlog/spdlog.h"

namespace loong::channel {

ChannelManager::ChannelManager()
    : orchestrator_(std::make_unique<ChannelOrchestrator>()),
      monitor_(std::make_unique<ChannelMonitor>(*this)) {}

ChannelManager::~ChannelManager() {
  StopAll();
  recovery_running_ = false;
  if (recovery_thread_.joinable()) {
    recovery_thread_.join();
  }
}

int ChannelManager::CreateChannel(const ChannelConfig& config) {
  std::unique_lock lock(channels_mutex_);

  if (channels_.size() >= static_cast<size_t>(kMaxChannels)) {
    spdlog::error("ChannelManager: max channels ({}) reached", kMaxChannels);
    return -1;
  }

  int id = next_channel_id_++;
  auto entry = std::make_unique<ChannelEntry>();
  entry->config = config;
  entry->config.id = id;
  entry->state = ChannelState::kCreated;
  entry->auto_recovery = recovery_enabled_;

  // Build the pipeline
  entry->pipeline = BuildPipeline(entry->config);
  if (!entry->pipeline) {
    spdlog::error("ChannelManager: failed to build pipeline for ch {}", id);
    return -1;
  }

  channels_[id] = std::move(entry);
  orchestrator_->OnChannelAdded(id);

  if (channel_store_) {
    channel_store_->Save(channels_[id]->config);
  }

  spdlog::info("ChannelManager: channel {} created ('{}')", id, config.name);
  return id;
}

bool ChannelManager::DeleteChannel(int channel_id) {
  std::shared_ptr<core::ChannelPipeline> pipeline_copy;
  bool was_running = false;
  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
      spdlog::warn("ChannelManager: channel {} not found", channel_id);
      return false;
    }
    was_running = (it->second->state == ChannelState::kRunning);
    pipeline_copy = it->second->pipeline;
    it->second->state = ChannelState::kDestroyed;
    channels_.erase(it);
  }

  if (was_running && pipeline_copy) {
    pipeline_copy->DrainAndStop();
  }

  UnregisterFromStreamingServices(channel_id);
  orchestrator_->OnChannelRemoved(channel_id);

  if (channel_store_) {
    channel_store_->Delete(channel_id);
  }

  spdlog::info("ChannelManager: channel {} deleted", channel_id);
  return true;
}

bool ChannelManager::StartChannel(int channel_id) {
  std::shared_ptr<core::ChannelPipeline> pipeline_copy;
  ChannelConfig config;
  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;

    auto& entry = it->second;
    if (entry->state == ChannelState::kRunning) {
      return true;
    }
    pipeline_copy = entry->pipeline;
    config = entry->config;
  }

  if (!pipeline_copy) return false;

  if (!pipeline_copy->Initialize()) {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      it->second->state = ChannelState::kError;
      it->second->error_message = "pipeline initialization failed";
    }
    return false;
  }

  if (!pipeline_copy->Start()) {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      it->second->state = ChannelState::kError;
      it->second->error_message = "pipeline start failed";
    }
    return false;
  }

  // Auto-detect stream parameters (resolution, codec, framerate) from the
  // actual source and update the stored config so the user doesn't have to
  // specify these values when creating the channel.
  auto* input_stage =
      dynamic_cast<pipeline_stages::InputStage*>(pipeline_copy->GetStage(0));
  if (input_stage) {
    auto info = input_stage->GetStreamInfo();
    if (info.width > 0 && info.height > 0) {
      std::unique_lock lock(channels_mutex_);
      auto it = channels_.find(channel_id);
      if (it != channels_.end()) {
        auto& cfg = it->second->config;
        cfg.codec = info.codec;
        cfg.width = info.width;
        cfg.height = info.height;
        if (info.framerate > 0) cfg.framerate = info.framerate;
        config = cfg;
        if (channel_store_) {
          channel_store_->Save(cfg);
        }
        spdlog::info("ChannelManager: ch {} stream detected {}x{} @{}fps",
                     channel_id, info.width, info.height, info.framerate);
      }
    }
  }

  RegisterWithStreamingServices(config);

  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      it->second->state = ChannelState::kRunning;
      it->second->error_message.clear();
    }
  }

  spdlog::info("ChannelManager: channel {} started", channel_id);
  return true;
}

bool ChannelManager::StopChannel(int channel_id) {
  std::shared_ptr<core::ChannelPipeline> pipeline_copy;
  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;

    auto& entry = it->second;
    if (entry->state != ChannelState::kRunning) {
      return true;
    }
    pipeline_copy = entry->pipeline;
    entry->state = ChannelState::kStopping;
  }

  if (pipeline_copy) {
    pipeline_copy->DrainAndStop();
  }

  UnregisterFromStreamingServices(channel_id);

  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      it->second->state = ChannelState::kStopped;
    }
  }

  spdlog::info("ChannelManager: channel {} stopped", channel_id);
  return true;
}

bool ChannelManager::EditChannel(int channel_id,
                                 const ChannelConfig& new_config) {
  ChannelState prev_state;
  std::shared_ptr<core::ChannelPipeline> old_pipeline;

  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;
    prev_state = it->second->state;
    old_pipeline = it->second->pipeline;
  }

  if (prev_state == ChannelState::kRunning) {
    if (old_pipeline) old_pipeline->DrainAndStop();
    UnregisterFromStreamingServices(channel_id);
    {
      std::unique_lock lock(channels_mutex_);
      auto it = channels_.find(channel_id);
      if (it != channels_.end()) it->second->state = ChannelState::kStopped;
    }
  }
  old_pipeline.reset();

  ChannelConfig config = new_config;
  config.id = channel_id;
  auto new_pipeline = BuildPipeline(config);

  if (!new_pipeline) {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
      it->second->state = ChannelState::kError;
      it->second->error_message = "pipeline rebuild failed";
    }
    spdlog::error("ChannelManager: failed to rebuild ch {}", channel_id);
    return false;
  }

  {
    std::unique_lock lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) return false;
    it->second->config = config;
    it->second->pipeline = std::move(new_pipeline);
    it->second->state = ChannelState::kConfigured;
  }

  if (channel_store_) {
    channel_store_->Save(config);
  }

  if (prev_state == ChannelState::kRunning) {
    if (!StartChannel(channel_id)) {
      return false;
    }
  }

  spdlog::info("ChannelManager: channel {} edited", channel_id);
  return true;
}

void ChannelManager::StartAll() {
  std::vector<int> ids;
  {
    std::shared_lock lock(channels_mutex_);
    for (const auto& [id, entry] : channels_) {
      if (entry->state != ChannelState::kRunning) {
        ids.push_back(id);
      }
    }
  }
  for (int id : ids) {
    StartChannel(id);
  }
}

void ChannelManager::StopAll() {
  std::vector<int> ids;
  {
    std::shared_lock lock(channels_mutex_);
    for (const auto& [id, entry] : channels_) {
      if (entry->state == ChannelState::kRunning) {
        ids.push_back(id);
      }
    }
  }
  for (int id : ids) {
    StopChannel(id);
  }
}

ChannelStatus ChannelManager::GetStatus(int channel_id) const {
  std::shared_lock lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    ChannelStatus not_found;
    not_found.id = -1;
    return not_found;
  }

  const auto& entry = it->second;
  ChannelStatus status;
  status.id = channel_id;
  status.name = entry->config.name;
  status.state = entry->state;
  status.error_message = entry->error_message;
  status.latitude = entry->config.latitude;
  status.longitude = entry->config.longitude;
  if (entry->pipeline) {
    status.frames_processed = entry->pipeline->FramesProcessed();
    status.frames_dropped = entry->pipeline->FramesDropped();
  }
  return status;
}

std::vector<ChannelStatus> ChannelManager::GetAllStatus() const {
  std::shared_lock lock(channels_mutex_);
  std::vector<ChannelStatus> result;
  result.reserve(channels_.size());
  for (const auto& [id, entry] : channels_) {
    ChannelStatus status;
    status.id = id;
    status.name = entry->config.name;
    status.state = entry->state;
    status.error_message = entry->error_message;
    status.latitude = entry->config.latitude;
    status.longitude = entry->config.longitude;
    if (entry->pipeline) {
      status.frames_processed = entry->pipeline->FramesProcessed();
      status.frames_dropped = entry->pipeline->FramesDropped();
    }
    result.push_back(status);
  }
  return result;
}

size_t ChannelManager::ChannelCount() const {
  std::shared_lock lock(channels_mutex_);
  return channels_.size();
}

bool ChannelManager::GetChannelConfig(int channel_id,
                                      ChannelConfig& out_config) const {
  std::shared_lock lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) {
    return false;
  }
  out_config = it->second->config;
  return true;
}

bool ChannelManager::UpdateOverlayConfig(int channel_id,
                                         const OverlayConfig& overlay) {
  std::unique_lock lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return false;
  it->second->config.overlay = overlay;
  spdlog::info("ChannelManager: overlay config updated for ch {}", channel_id);
  return true;
}

void ChannelManager::EnableAutoRecovery(bool enable) {
  std::unique_lock lock(channels_mutex_);
  recovery_enabled_ = enable;

  // Set auto_recovery on all existing channels.
  for (auto& [id, entry] : channels_) {
    entry->auto_recovery = enable;
  }
  lock.unlock();

  // Delegate to ChannelMonitor (primary recovery mechanism).
  monitor_->SetAutoRecovery(enable);

  if (enable && !recovery_running_.exchange(true)) {
    recovery_thread_ = std::thread(&ChannelManager::RecoveryLoop, this);
    monitor_->Start();
  } else if (!enable) {
    recovery_running_ = false;
    if (recovery_thread_.joinable()) {
      recovery_thread_.join();
    }
    monitor_->Stop();
  }
}

ChannelOrchestrator& ChannelManager::GetOrchestrator() {
  return *orchestrator_;
}

ChannelMonitor& ChannelManager::GetMonitor() { return *monitor_; }

std::shared_ptr<core::ChannelPipeline> ChannelManager::BuildPipeline(
    const ChannelConfig& config) {
  return orchestrator_->BuildPipeline(config);
}

void ChannelManager::RecoveryLoop() {
  while (recovery_running_) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (!recovery_running_) break;

    std::vector<int> error_ids;
    {
      std::shared_lock lock(channels_mutex_);
      for (const auto& [id, entry] : channels_) {
        if (entry->state == ChannelState::kError && entry->auto_recovery) {
          error_ids.push_back(id);
        }
      }
    }
    for (int id : error_ids) {
      spdlog::warn("ChannelManager: attempting recovery for ch {}", id);
      StopChannel(id);
      StartChannel(id);
    }
  }
}

void ChannelManager::SetStreamingServices(
    std::shared_ptr<network::HttpFlvService> flv_service,
    std::shared_ptr<network::HlsService> hls_service,
    std::shared_ptr<network::RtspServer> rtsp_server,
    std::shared_ptr<network::WsMediaService> ws_media_service,
    std::shared_ptr<network::WebRtcService> webrtc_service) {
  flv_service_ = std::move(flv_service);
  hls_service_ = std::move(hls_service);
  rtsp_server_ = std::move(rtsp_server);
  ws_media_service_ = std::move(ws_media_service);
  webrtc_service_ = std::move(webrtc_service);

  StreamingServices svc;
  svc.flv_service = flv_service_;
  svc.hls_service = hls_service_;
  svc.rtsp_server = rtsp_server_;
  svc.ws_media_service = ws_media_service_;
  svc.webrtc_service = webrtc_service_;
  orchestrator_->SetStreamingServices(svc);

  spdlog::info(
      "ChannelManager: streaming services configured "
      "(FLV={}, HLS={}, RTSP={}, WS={}, WebRTC={})",
      flv_service_ != nullptr, hls_service_ != nullptr, rtsp_server_ != nullptr,
      ws_media_service_ != nullptr, webrtc_service_ != nullptr);
}

void ChannelManager::SetRuleEngine(std::shared_ptr<rules::RuleEngine> engine) {
  orchestrator_->SetRuleEngine(std::move(engine));
  spdlog::info("ChannelManager: rule engine configured for pipeline wiring");
}

void ChannelManager::RegisterWithStreamingServices(
    const ChannelConfig& config) {
  if (flv_service_) {
    flv_service_->RegisterChannel(config.id);
  }
  if (hls_service_) {
    hls_service_->RegisterChannel(config.id);
  }
  if (rtsp_server_) {
    rtsp_server_->RegisterChannel(config.id, config.codec, config.width,
                                  config.height, config.framerate);
  }
  if (ws_media_service_) {
    ws_media_service_->RegisterChannel(
        config.id, config.codec, static_cast<uint16_t>(config.width),
        static_cast<uint16_t>(config.height), config.framerate);
  }
  spdlog::debug("ChannelManager: ch {} registered with streaming services",
                config.id);
}

void ChannelManager::UnregisterFromStreamingServices(int channel_id) {
  if (flv_service_) {
    flv_service_->UnregisterChannel(channel_id);
  }
  if (hls_service_) {
    hls_service_->UnregisterChannel(channel_id);
  }
  if (rtsp_server_) {
    rtsp_server_->UnregisterChannel(channel_id);
  }
  if (ws_media_service_) {
    ws_media_service_->UnregisterChannel(channel_id);
  }
  if (webrtc_service_) {
    webrtc_service_->CloseSessionsForChannel(channel_id);
  }
  spdlog::debug("ChannelManager: ch {} unregistered from streaming services",
                channel_id);
}

void ChannelManager::SetChannelStore(std::shared_ptr<ChannelStore> store) {
  channel_store_ = std::move(store);
  spdlog::info("ChannelManager: channel store configured");
}

int ChannelManager::LoadChannels() {
  if (!channel_store_) {
    spdlog::warn("ChannelManager: no channel store, skipping load");
    return 0;
  }

  auto configs = channel_store_->LoadAll();
  if (configs.empty()) {
    spdlog::info("ChannelManager: no persisted channels to load");
    return 0;
  }

  int loaded = 0;
  for (auto& cfg : configs) {
    std::unique_lock lock(channels_mutex_);

    if (channels_.count(cfg.id)) {
      continue;
    }

    auto entry = std::make_unique<ChannelEntry>();
    entry->config = cfg;
    entry->state = ChannelState::kCreated;
    entry->auto_recovery = recovery_enabled_;
    entry->pipeline = BuildPipeline(cfg);

    if (!entry->pipeline) {
      spdlog::warn(
          "ChannelManager: failed to build pipeline for persisted ch {}",
          cfg.id);
      continue;
    }

    int id = cfg.id;
    channels_[id] = std::move(entry);

    if (id >= next_channel_id_) {
      next_channel_id_ = id + 1;
    }

    orchestrator_->OnChannelAdded(id);
    lock.unlock();

    if (StartChannel(id)) {
      ++loaded;
      spdlog::info("ChannelManager: restored ch {} '{}' from store", id,
                   cfg.name);
    }
  }

  spdlog::info("ChannelManager: loaded {} channels from store", loaded);
  return loaded;
}

}  // namespace loong::channel
