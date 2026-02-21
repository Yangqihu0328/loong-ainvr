// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CHANNEL_CHANNEL_PIPELINE_CHANNEL_ORCHESTRATOR_H_
#define LOONG_CHANNEL_CHANNEL_PIPELINE_CHANNEL_ORCHESTRATOR_H_

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/common/types.h"
#include "core/pipeline/channel_pipeline.h"

namespace loong::network {
class HttpFlvService;
class HlsService;
class RtspServer;
class WebRtcService;
class WsMediaService;
}  // namespace loong::network

namespace loong::ai_engine {
class ModelManager;
}  // namespace loong::ai_engine

namespace loong::rules {
class RuleEngine;
}  // namespace loong::rules

namespace loong::channel {

/// References to streaming services for wiring OutputStage callbacks.
struct StreamingServices {
  std::shared_ptr<network::HttpFlvService> flv_service;
  std::shared_ptr<network::HlsService> hls_service;
  std::shared_ptr<network::RtspServer> rtsp_server;
  std::shared_ptr<network::WsMediaService> ws_media_service;
  std::shared_ptr<network::WebRtcService> webrtc_service;
};

/// Resource allocation information for a channel pipeline.
struct ChannelResources {
  int channel_id = -1;
  int decode_threads = 1;
  int ai_threads = 1;
  int output_threads = 1;
  size_t input_queue_capacity = 128;
  size_t decode_queue_capacity = 64;
  size_t ai_queue_capacity = 32;
  size_t overlay_queue_capacity = 32;
  size_t output_queue_capacity = 64;
  int ai_batch_size = 1;
};

/// Orchestrates the construction and resource allocation of 64-channel
/// video processing pipelines.
///
/// Responsibilities:
/// - Build a complete ChannelPipeline (Input → Decode → AI → Rule →
///   Overlay → Output) from a ChannelConfig.
/// - Manage per-channel resource budgets (queue capacities, thread counts)
///   based on the total number of active channels.
/// - Provide load-balancing hints: distribute AI batch sizes and queue
///   depths to balance throughput across many concurrent channels.
///
/// Usage: ChannelManager delegates pipeline construction to this class
/// instead of hardcoding stage wiring.
class ChannelOrchestrator {
 public:
  static constexpr int kMaxChannels = 64;

  ChannelOrchestrator();
  ~ChannelOrchestrator() = default;

  /// Set streaming services used to wire OutputStage callbacks.
  void SetStreamingServices(const StreamingServices& services);

  /// Set the shared rule engine for wiring RuleStage into each pipeline.
  void SetRuleEngine(std::shared_ptr<rules::RuleEngine> engine);

  /// Set the model manager for creating AI inference engines.
  void SetModelManager(std::shared_ptr<ai_engine::ModelManager> mgr);

  /// Enable cascade inference on each pipeline's AiStage.
  /// Steps define the multi-model chain (e.g., YOLO → LPR detector → OCR).
  void SetCascadeEnabled(bool enabled);
  void SetCascadeSteps(const std::vector<std::pair<std::string, std::string>>& steps);

  /// Build a complete pipeline for the given channel configuration.
  /// Returns nullptr on failure.
  std::unique_ptr<core::ChannelPipeline> BuildPipeline(
      const ChannelConfig& config);

  /// Compute resource allocation for a new channel, given the current
  /// total number of active channels.
  ChannelResources AllocateResources(const ChannelConfig& config,
                                     int active_channel_count) const;

  /// Notify the orchestrator that a channel was added/removed so it
  /// can update internal resource tracking.
  void OnChannelAdded(int channel_id);
  void OnChannelRemoved(int channel_id);

  /// Get current resource allocation for a channel.
  ChannelResources GetResources(int channel_id) const;

  /// Get overall resource utilization summary.
  int ActiveChannelCount() const;

  // Non-copyable
  ChannelOrchestrator(const ChannelOrchestrator&) = delete;
  ChannelOrchestrator& operator=(const ChannelOrchestrator&) = delete;

 private:
  /// Determine the AI batch size based on model name and channel count.
  int ComputeBatchSize(const std::string& ai_model_name,
                       int active_channels) const;

  /// Determine queue capacity scaling factor based on load.
  double QueueScaleFactor(int active_channels) const;

  /// Create and configure the Input stage.
  StageConfig MakeInputStageConfig(const ChannelConfig& config,
                                   const ChannelResources& res) const;

  /// Create and configure the Decode stage.
  StageConfig MakeDecodeStageConfig(const ChannelConfig& config,
                                    const ChannelResources& res) const;

  /// Create and configure the AI stage.
  StageConfig MakeAiStageConfig(const ChannelConfig& config,
                                const ChannelResources& res) const;

  /// Create and configure the Rule stage.
  StageConfig MakeRuleStageConfig(const ChannelConfig& config,
                                  const ChannelResources& res) const;

  /// Create and configure the Overlay stage.
  StageConfig MakeOverlayStageConfig(const ChannelConfig& config,
                                     const ChannelResources& res) const;

  /// Create and configure the Output stage.
  StageConfig MakeOutputStageConfig(const ChannelConfig& config,
                                    const ChannelResources& res) const;

  mutable std::mutex mutex_;
  std::unordered_map<int, ChannelResources> channel_resources_;

  // Streaming services for wiring OutputStage
  StreamingServices streaming_services_;

  // Rule engine for wiring RuleStage
  std::shared_ptr<rules::RuleEngine> rule_engine_;

  // Model manager for AI engine creation
  std::shared_ptr<ai_engine::ModelManager> model_manager_;

  // Cascade config: list of (model_name, mode_string) pairs
  bool cascade_enabled_ = false;
  std::vector<std::pair<std::string, std::string>> cascade_steps_;
};

}  // namespace loong::channel

#endif  // LOONG_CHANNEL_CHANNEL_PIPELINE_CHANNEL_ORCHESTRATOR_H_
