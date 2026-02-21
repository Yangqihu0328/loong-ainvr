// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CHANNEL_CHANNEL_MANAGER_CHANNEL_MANAGER_H_
#define LOONG_CHANNEL_CHANNEL_MANAGER_CHANNEL_MANAGER_H_

#include "channel/channel_store/channel_store.h"
#include "core/common/types.h"
#include "core/pipeline/channel_pipeline.h"

#include <memory>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace loong::network {
class HttpFlvService;
class HlsService;
class RtspServer;
class WebRtcService;
class WsMediaService;
}  // namespace loong::network

namespace loong::rules {
class RuleEngine;
}  // namespace loong::rules

namespace loong::channel {

class ChannelMonitor;
class ChannelOrchestrator;

/// Manages all video channels (up to 64).
/// Supports dynamic CRUD operations and lifecycle control.
/// Delegates pipeline construction to ChannelOrchestrator and
/// health monitoring to ChannelMonitor.
class ChannelManager {
 public:
  static constexpr int kMaxChannels = 64;

  ChannelManager();
  ~ChannelManager();

  /// Create a new channel with the given configuration.
  /// Returns the channel ID, or -1 on failure.
  int CreateChannel(const ChannelConfig& config);

  /// Delete a channel. Stops it first if running.
  bool DeleteChannel(int channel_id);

  /// Start a channel's pipeline.
  bool StartChannel(int channel_id);

  /// Stop a channel's pipeline.
  bool StopChannel(int channel_id);

  /// Edit a channel's configuration (Stop → Reconfigure → Start).
  bool EditChannel(int channel_id, const ChannelConfig& new_config);

  /// Start all channels.
  void StartAll();

  /// Stop all channels.
  void StopAll();

  /// Get the status of a single channel.
  ChannelStatus GetStatus(int channel_id) const;

  /// Get status of all channels.
  std::vector<ChannelStatus> GetAllStatus() const;

  /// Get current number of channels.
  size_t ChannelCount() const;

  /// Get the configuration of a channel. Returns false if not found.
  bool GetChannelConfig(int channel_id, ChannelConfig& out_config) const;

  /// Update only the overlay configuration for a channel.
  /// Takes effect on next channel restart (EditChannel or Stop/Start).
  bool UpdateOverlayConfig(int channel_id, const OverlayConfig& overlay);

  /// Set streaming services for output distribution.
  /// Must be called before creating/starting channels.
  void SetStreamingServices(
      std::shared_ptr<network::HttpFlvService> flv_service,
      std::shared_ptr<network::HlsService> hls_service,
      std::shared_ptr<network::RtspServer> rtsp_server,
      std::shared_ptr<network::WsMediaService> ws_media_service,
      std::shared_ptr<network::WebRtcService> webrtc_service = nullptr);

  /// Set the shared rule engine for pipeline RuleStage wiring.
  void SetRuleEngine(std::shared_ptr<rules::RuleEngine> engine);

  /// Set the persistent channel store. Must be called before LoadChannels.
  void SetChannelStore(std::shared_ptr<ChannelStore> store);

  /// Load all persisted channels from the store, create & start them.
  int LoadChannels();

  /// Enable auto-recovery for a channel.
  void EnableAutoRecovery(bool enable);

  /// Get the orchestrator (for external resource queries).
  ChannelOrchestrator& GetOrchestrator();

  /// Get the monitor (for external metric queries).
  ChannelMonitor& GetMonitor();

  // Non-copyable
  ChannelManager(const ChannelManager&) = delete;
  ChannelManager& operator=(const ChannelManager&) = delete;

 private:
  struct ChannelEntry {
    ChannelConfig config;
    std::shared_ptr<core::ChannelPipeline> pipeline;
    ChannelState state = ChannelState::kCreated;
    bool auto_recovery = false;
    std::string error_message;
  };

  /// Build a pipeline for the given channel configuration.
  std::shared_ptr<core::ChannelPipeline> BuildPipeline(
      const ChannelConfig& config);

  /// Register a channel with all streaming services.
  void RegisterWithStreamingServices(const ChannelConfig& config);

  /// Unregister a channel from all streaming services.
  void UnregisterFromStreamingServices(int channel_id);

  /// Recovery loop (runs in background thread).
  void RecoveryLoop();

  mutable std::shared_mutex channels_mutex_;
  std::unordered_map<int, std::unique_ptr<ChannelEntry>> channels_;
  int next_channel_id_ = 1;

  // Auto-recovery
  bool recovery_enabled_ = false;
  std::atomic<bool> recovery_running_{false};
  std::thread recovery_thread_;

  // Sub-components
  std::unique_ptr<ChannelOrchestrator> orchestrator_;
  std::unique_ptr<ChannelMonitor> monitor_;

  // Persistent channel store
  std::shared_ptr<ChannelStore> channel_store_;

  // Streaming services (set via SetStreamingServices)
  std::shared_ptr<network::HttpFlvService> flv_service_;
  std::shared_ptr<network::HlsService> hls_service_;
  std::shared_ptr<network::RtspServer> rtsp_server_;
  std::shared_ptr<network::WsMediaService> ws_media_service_;
  std::shared_ptr<network::WebRtcService> webrtc_service_;
};

}  // namespace loong::channel

#endif  // LOONG_CHANNEL_CHANNEL_MANAGER_CHANNEL_MANAGER_H_
