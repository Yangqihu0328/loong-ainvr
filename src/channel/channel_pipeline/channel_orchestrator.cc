// Copyright 2026 Loong AI NVR Project

#include "channel/channel_pipeline/channel_orchestrator.h"

#include <algorithm>
#include <sstream>

#include "ai_engine/backend/backend_factory.h"
#include "ai_engine/cascade/model_cascade.h"
#include "ai_engine/inference/inference_engine.h"
#include "ai_engine/model_manager/model_manager.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "network/flv_stream/http_flv_service.h"
#include "network/hls_stream/hls_service.h"
#include "network/media_stream/ws_media_service.h"
#include "network/rtsp_server/rtsp_server.h"
#include "network/webrtc/webrtc_service.h"
#include "pipeline_stages/ai_stage/ai_stage.h"
#include "pipeline_stages/decode_stage/decode_stage.h"
#include "pipeline_stages/input_stage/input_stage.h"
#include "pipeline_stages/output_stage/output_stage.h"
#include "pipeline_stages/overlay_stage/overlay_stage.h"
#include "rules/rule_stage/rule_stage.h"
#include "spdlog/spdlog.h"

namespace loong::channel {

ChannelOrchestrator::ChannelOrchestrator() = default;

void ChannelOrchestrator::SetStreamingServices(
    const StreamingServices& services) {
  streaming_services_ = services;
}

void ChannelOrchestrator::SetRuleEngine(
    std::shared_ptr<rules::RuleEngine> engine) {
  rule_engine_ = std::move(engine);
}

void ChannelOrchestrator::SetModelManager(
    std::shared_ptr<ai_engine::ModelManager> mgr) {
  model_manager_ = std::move(mgr);
}

void ChannelOrchestrator::SetCascadeEnabled(bool enabled) {
  cascade_enabled_ = enabled;
}

void ChannelOrchestrator::SetCascadeSteps(
    const std::vector<std::pair<std::string, std::string>>& steps) {
  cascade_steps_ = steps;
}

// ========== Resource Allocation ==========

ChannelResources ChannelOrchestrator::AllocateResources(
    const ChannelConfig& config, int active_channel_count) const {
  ChannelResources res;
  res.channel_id = config.id;

  double scale = QueueScaleFactor(active_channel_count);

  // Queue capacities shrink as more channels compete for memory.
  res.input_queue_capacity =
      std::max(static_cast<size_t>(16),
               static_cast<size_t>(128 * scale));
  res.decode_queue_capacity =
      std::max(static_cast<size_t>(8),
               static_cast<size_t>(64 * scale));
  res.ai_queue_capacity =
      std::max(static_cast<size_t>(4),
               static_cast<size_t>(32 * scale));
  res.overlay_queue_capacity =
      std::max(static_cast<size_t>(4),
               static_cast<size_t>(32 * scale));
  res.output_queue_capacity =
      std::max(static_cast<size_t>(8),
               static_cast<size_t>(64 * scale));

  // Thread counts: 1 per stage is typical; high-res channels may get more.
  res.decode_threads = 1;
  res.ai_threads = 1;
  res.output_threads = 1;

  // AI batch size: larger when fewer channels compete for GPU.
  res.ai_batch_size = ComputeBatchSize(config.ai_model_name,
                                       active_channel_count);

  return res;
}

double ChannelOrchestrator::QueueScaleFactor(int active_channels) const {
  // Full capacity for ≤8 channels; linear reduction to 0.25 at 64 channels.
  if (active_channels <= 8) return 1.0;
  if (active_channels >= kMaxChannels) return 0.25;
  return 1.0 - 0.75 * (static_cast<double>(active_channels - 8) /
                        static_cast<double>(kMaxChannels - 8));
}

int ChannelOrchestrator::ComputeBatchSize(
    const std::string& /*ai_model_name*/, int active_channels) const {
  // Fewer channels → larger batches for better GPU utilization.
  // More channels → smaller batches to share GPU time fairly.
  if (active_channels <= 4) return 8;
  if (active_channels <= 16) return 4;
  if (active_channels <= 32) return 2;
  return 1;
}

// ========== Stage Config Builders ==========

StageConfig ChannelOrchestrator::MakeInputStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "InputStage";
  sc.thread_count = 1;
  sc.queue_capacity = res.input_queue_capacity;
  // Pass RTSP URL via params JSON.
  std::ostringstream oss;
  oss << R"({"rtsp_url":")" << config.rtsp_url
      << R"(","channel_id":)" << config.id << "}";
  sc.params = oss.str();
  return sc;
}

StageConfig ChannelOrchestrator::MakeDecodeStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "DecodeStage";
  sc.thread_count = res.decode_threads;
  sc.queue_capacity = res.decode_queue_capacity;
  std::ostringstream oss;
  std::string codec_str;
  if (config.codec == CodecType::kH265) codec_str = "h265";
  else if (config.codec == CodecType::kH264) codec_str = "h264";
  oss << R"({"codec":")" << codec_str
      << R"(","width":)" << config.width
      << ",\"height\":" << config.height << "}";
  sc.params = oss.str();
  return sc;
}

StageConfig ChannelOrchestrator::MakeAiStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "AiStage";
  sc.thread_count = res.ai_threads;
  sc.queue_capacity = res.ai_queue_capacity;

  std::ostringstream oss;
  oss << R"({"model_name":")" << config.ai_model_name
      << R"(","backend":")" << config.ai_backend
      << R"(","confidence_threshold":)" << config.confidence_threshold
      << ",\"batch_size\":" << res.ai_batch_size
      << ",\"skip_frames\":0}";
  sc.params = oss.str();
  return sc;
}

StageConfig ChannelOrchestrator::MakeRuleStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "RuleStage";
  sc.thread_count = 1;
  sc.queue_capacity = res.ai_queue_capacity;
  std::ostringstream oss;
  oss << R"({"channel_id":)" << config.id << "}";
  sc.params = oss.str();
  return sc;
}

StageConfig ChannelOrchestrator::MakeOverlayStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "OverlayStage";
  sc.thread_count = 1;
  sc.queue_capacity = res.overlay_queue_capacity;

  // Pass per-channel overlay configuration via JSON params.
  const auto& ov = config.overlay;
  std::ostringstream oss;
  oss << "{\"enabled\":" << (ov.enabled ? "true" : "false")
      << ",\"line_thickness\":" << ov.line_thickness
      << ",\"font_scale\":" << ov.font_scale
      << ",\"show_labels\":" << (ov.show_labels ? "true" : "false")
      << ",\"show_confidence\":" << (ov.show_confidence ? "true" : "false")
      << ",\"show_timestamp\":" << (ov.show_timestamp ? "true" : "false")
      << ",\"show_channel_name\":" << (ov.show_channel_name ? "true" : "false")
      << ",\"show_trajectory\":" << (ov.show_trajectory ? "true" : "false")
      << ",\"trajectory_max_points\":" << ov.trajectory_max_points
      << ",\"timestamp_position\":" << ov.timestamp_position
      << ",\"fill_opacity\":" << ov.fill_opacity
      << ",\"channel_id\":" << config.id
      << R"(,"channel_name":")" << config.name << "\"";
  if (!ov.timestamp_format.empty()) {
    oss << R"(,"timestamp_format":")" << ov.timestamp_format << "\"";
  }
  oss << "}";
  sc.params = oss.str();
  return sc;
}

StageConfig ChannelOrchestrator::MakeOutputStageConfig(
    const ChannelConfig& config,
    const ChannelResources& res) const {
  StageConfig sc;
  sc.name = "OutputStage";
  sc.thread_count = res.output_threads;
  sc.queue_capacity = res.output_queue_capacity;
  std::ostringstream oss;
  std::string out_codec;
  if (config.codec == CodecType::kH265) out_codec = "h265";
  else if (config.codec == CodecType::kH264) out_codec = "h264";
  oss << R"({"codec":")" << out_codec
      << R"(","width":)" << config.width
      << ",\"height\":" << config.height
      << ",\"bitrate_kbps\":" << config.bitrate_kbps
      << ",\"framerate\":" << (config.framerate > 0 ? config.framerate : 30)
      << R"(,"preset":"ultrafast")"
      << ",\"channel_id\":" << config.id << "}";
  sc.params = oss.str();
  return sc;
}

// ========== Pipeline Building ==========

std::unique_ptr<core::ChannelPipeline> ChannelOrchestrator::BuildPipeline(
    const ChannelConfig& config) {
  int active_count;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    active_count = static_cast<int>(channel_resources_.size());
  }

  if (active_count >= kMaxChannels) {
    spdlog::error("ChannelOrchestrator: max channels ({}) reached",
                  kMaxChannels);
    return nullptr;
  }

  ChannelResources res = AllocateResources(config, active_count);

  // Store actual allocated resources (not just placeholder).
  {
    std::lock_guard<std::mutex> lock2(mutex_);
    channel_resources_[config.id] = res;
  }

  auto pipeline = std::make_unique<core::ChannelPipeline>(config.id);

  // Stage 1: Input (RTSP pull)
  auto input_stage = std::make_unique<pipeline_stages::InputStage>();
  pipeline->AddStage(std::move(input_stage),
                     MakeInputStageConfig(config, res));

  // Stage 2: Decode (H.264/H.265 → raw frames)
  auto decode_stage = std::make_unique<pipeline_stages::DecodeStage>();
  pipeline->AddStage(std::move(decode_stage),
                     MakeDecodeStageConfig(config, res));

  // Stage 3: AI Analysis (YOLO detection)
  auto ai_stage = std::make_unique<pipeline_stages::AiStage>();

  // Create inference engine if AI model is configured and ModelManager is set.
  if (!config.ai_model_name.empty() && model_manager_) {
    std::string backend_name = config.ai_backend.empty()
                                   ? ""  // Let ModelManager decide
                                   : config.ai_backend;
    auto engine = model_manager_->CreateEngine(config.ai_model_name,
                                               backend_name);
    if (engine) {
      ai_stage->SetInferenceEngine(engine);

      // Build cascade if configured (e.g., YOLO → LPR-det → LPR-OCR)
      if (cascade_enabled_ && !cascade_steps_.empty()) {
        auto cascade = std::make_shared<ai_engine::ModelCascade>();
        ai_engine::CascadeStep primary_step;
        primary_step.name = "primary";
        primary_step.model_name = config.ai_model_name;
        primary_step.mode = ai_engine::CascadeMode::kPrimary;
        primary_step.confidence_threshold = config.confidence_threshold;
        cascade->AddStep(primary_step, engine);

        for (const auto& [step_model, step_mode] : cascade_steps_) {
          auto step_engine = model_manager_->CreateEngine(step_model,
                                                          backend_name);
          if (step_engine) {
            ai_engine::CascadeStep cs;
            cs.name = step_model;
            cs.model_name = step_model;
            cs.mode = (step_mode == "parallel") ? ai_engine::CascadeMode::kParallel
                                                : ai_engine::CascadeMode::kCrop;
            cs.confidence_threshold = config.confidence_threshold;
            cascade->AddStep(cs, step_engine);
          }
        }

        if (cascade->StepCount() > 1) {
          ai_stage->SetModelCascade(cascade);
          spdlog::info("ChannelOrchestrator: ch {} cascade wired ({} steps)",
                       config.id, cascade->StepCount());
        }
      }
    } else {
      spdlog::warn("ChannelOrchestrator: ch {} — model '{}' not available, "
                   "AI stage disabled", config.id, config.ai_model_name);
    }
  } else if (!config.ai_model_name.empty() && !model_manager_) {
    spdlog::warn("ChannelOrchestrator: ch {} — no ModelManager set, "
                 "AI stage disabled", config.id);
  }

  pipeline->AddStage(std::move(ai_stage),
                     MakeAiStageConfig(config, res));

  // Stage 4: Rule Evaluation (evaluate analysis rules against detections)
  if (rule_engine_) {
    auto rule_stage = std::make_unique<rules::RuleStage>();
    rule_stage->SetRuleEngine(rule_engine_);
    pipeline->AddStage(std::move(rule_stage),
                       MakeRuleStageConfig(config, res));
  }

  // Stage 5: Overlay (draw detections + rule visualization)
  auto overlay_stage =
      std::make_unique<pipeline_stages::OverlayStage>();
  pipeline->AddStage(std::move(overlay_stage),
                     MakeOverlayStageConfig(config, res));

  // Stage 6: Output (encode + distribute)
  auto output_stage = std::make_unique<pipeline_stages::OutputStage>();

  // Wire the output callback to distribute encoded packets to all
  // streaming services (FLV, RTSP, WebSocket fMP4).
  {
    auto flv = streaming_services_.flv_service;
    auto hls = streaming_services_.hls_service;
    auto rtsp = streaming_services_.rtsp_server;
    auto ws = streaming_services_.ws_media_service;
    auto webrtc = streaming_services_.webrtc_service;

    if (flv || hls || rtsp || ws || webrtc) {
      output_stage->SetOutputCallback(
          [flv, hls, rtsp, ws, webrtc](int channel_id, const uint8_t* data,
                               size_t size, int64_t pts, bool is_keyframe) {
            if (flv) flv->PushFrame(channel_id, data, size, pts, is_keyframe);
            if (hls) hls->PushFrame(channel_id, data, size, pts, is_keyframe);
            if (rtsp)
              rtsp->PushFrame(channel_id, data, size, pts, is_keyframe);
            if (ws) ws->PushFrame(channel_id, data, size, pts, is_keyframe);
            if (webrtc)
              webrtc->WriteVideoFrame(channel_id, data, size, pts, is_keyframe);
          });
      spdlog::debug("ChannelOrchestrator: ch {} output wired to streaming "
                    "services (webrtc={})", config.id, webrtc != nullptr);
    } else {
      spdlog::warn("ChannelOrchestrator: ch {} — no streaming services "
                   "configured, output will be discarded", config.id);
    }
  }

  pipeline->AddStage(std::move(output_stage),
                     MakeOutputStageConfig(config, res));

  spdlog::info("ChannelOrchestrator: built pipeline for ch {} "
               "(queues: {}/{}/{}/{}/{}, batch_size={})",
               config.id,
               res.input_queue_capacity,
               res.decode_queue_capacity,
               res.ai_queue_capacity,
               res.overlay_queue_capacity,
               res.output_queue_capacity,
               res.ai_batch_size);

  return pipeline;
}

// ========== Channel Tracking ==========

void ChannelOrchestrator::OnChannelAdded(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  // Store a placeholder; real resources were already computed in
  // BuildPipeline, but we track the channel existence for counting.
  if (channel_resources_.find(channel_id) == channel_resources_.end()) {
    ChannelResources res;
    res.channel_id = channel_id;
    channel_resources_[channel_id] = res;
  }
}

void ChannelOrchestrator::OnChannelRemoved(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  channel_resources_.erase(channel_id);
}

ChannelResources ChannelOrchestrator::GetResources(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = channel_resources_.find(channel_id);
  if (it != channel_resources_.end()) {
    return it->second;
  }
  ChannelResources empty;
  empty.channel_id = -1;
  return empty;
}

int ChannelOrchestrator::ActiveChannelCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return static_cast<int>(channel_resources_.size());
}

}  // namespace loong::channel
