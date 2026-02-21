// Copyright 2026 Loong AI NVR Project

#include "core/pipeline/channel_pipeline.h"

#include "spdlog/spdlog.h"

namespace loong::core {

ChannelPipeline::ChannelPipeline(int channel_id)
    : channel_id_(channel_id) {}

ChannelPipeline::~ChannelPipeline() {
  if (running_) {
    Stop();
  }
}

void ChannelPipeline::AddStage(std::unique_ptr<PipelineStage> stage,
                                const StageConfig& config) {
  StageEntry entry;
  entry.stage = std::move(stage);
  entry.config = config;
  entry.input_queue =
      std::make_shared<StageQueue>(config.queue_capacity);
  stages_.push_back(std::move(entry));
}

bool ChannelPipeline::Initialize() {
  if (stages_.empty()) {
    spdlog::error("Channel {}: no stages added", channel_id_);
    return false;
  }

  // Wire up stage output queues: each stage's output = next stage's input
  for (size_t i = 0; i < stages_.size(); ++i) {
    if (!stages_[i].stage->Initialize(stages_[i].config)) {
      spdlog::error("Channel {}: failed to initialize stage '{}'",
                     channel_id_, stages_[i].stage->Name());
      state_ = ChannelState::kError;
      return false;
    }

    if (i + 1 < stages_.size()) {
      stages_[i].stage->SetOutputQueue(stages_[i + 1].input_queue);
    }
    // Last stage has no output queue (terminal)
  }

  state_ = ChannelState::kConfigured;
  spdlog::info("Channel {}: pipeline initialized with {} stages",
               channel_id_, stages_.size());
  return true;
}

bool ChannelPipeline::Start() {
  if (state_ != ChannelState::kConfigured &&
      state_ != ChannelState::kStopped) {
    spdlog::error("Channel {}: cannot start from state {}",
                   channel_id_, ChannelStateToString(state_));
    return false;
  }

  running_ = true;

  // Launch a worker thread for each stage
  for (size_t i = 0; i < stages_.size(); ++i) {
    stages_[i].worker = std::thread(&ChannelPipeline::StageWorkerLoop,
                                     this, i);
  }

  // Notify all stages that the pipeline has started.
  // Source stages (e.g., InputStage) use this to begin producing data.
  for (auto& entry : stages_) {
    entry.stage->OnPipelineStart();
  }

  state_ = ChannelState::kRunning;
  spdlog::info("Channel {}: pipeline started", channel_id_);
  return true;
}

bool ChannelPipeline::Stop() {
  if (!running_) return true;

  // Notify all stages to stop producing data (e.g., InputStage stops RTSP).
  // This must happen BEFORE setting running_=false so that source stages
  // stop pushing frames while workers are still draining.
  for (auto& entry : stages_) {
    entry.stage->OnPipelineStop();
  }

  running_ = false;

  // Signal all input queues to shutdown
  for (auto& entry : stages_) {
    entry.input_queue->Shutdown();
  }

  // Join all worker threads
  for (auto& entry : stages_) {
    if (entry.worker.joinable()) {
      entry.worker.join();
    }
  }

  // Shutdown all stages
  for (auto& entry : stages_) {
    entry.stage->Shutdown();
  }

  state_ = ChannelState::kStopped;
  spdlog::info("Channel {}: pipeline stopped", channel_id_);
  return true;
}

bool ChannelPipeline::DrainAndStop() {
  if (!running_) return true;

  spdlog::info("Channel {}: draining pipeline...", channel_id_);

  // Wait briefly for queues to drain while workers are still running.
  for (int attempt = 0; attempt < 50; ++attempt) {
    bool all_empty = true;
    for (const auto& entry : stages_) {
      if (!entry.input_queue->Empty()) {
        all_empty = false;
        break;
      }
    }
    if (all_empty) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  // Now stop normally (sets running_=false, signals queues, joins threads).
  return Stop();
}

ChannelState ChannelPipeline::GetState() const {
  return state_;
}

std::shared_ptr<StageQueue> ChannelPipeline::GetInputQueue() const {
  if (stages_.empty()) return nullptr;
  return stages_[0].input_queue;
}

int64_t ChannelPipeline::FramesProcessed() const {
  return frames_processed_;
}

int64_t ChannelPipeline::FramesDropped() const {
  int64_t total = 0;
  for (const auto& entry : stages_) {
    total += entry.input_queue->DroppedCount();
  }
  return total;
}

void ChannelPipeline::StageWorkerLoop(size_t stage_index) {
  auto& entry = stages_[stage_index];
  const auto& name = entry.stage->Name();
  bool use_batch = entry.stage->SupportsBatch();
  auto batch_size = static_cast<size_t>(entry.stage->GetBatchSize());

  spdlog::debug("Channel {}: stage '{}' worker started (batch={})",
                channel_id_, name, use_batch ? batch_size : 1);

  while (running_ || !entry.input_queue->Empty()) {
    if (use_batch && batch_size > 1) {
      // Batch mode: collect up to batch_size frames and process together.
      auto frames = entry.input_queue->PopBatch(
          batch_size, std::chrono::milliseconds(10));

      if (frames.empty()) {
        // No frames within the timeout — try a single pop with longer wait
        // to avoid busy-spinning.
        auto frame_opt = entry.input_queue->Pop(
            std::chrono::milliseconds(100));
        if (frame_opt.has_value()) {
          frames.push_back(std::move(frame_opt.value()));
        }
      }

      if (!frames.empty()) {
        if (!entry.stage->ProcessBatch(frames)) {
          spdlog::warn("Channel {}: stage '{}' batch processing failed",
                        channel_id_, name);
        } else {
          frames_processed_ += static_cast<int64_t>(frames.size());
        }
      }
    } else {
      // Single-frame mode.
      auto frame_opt = entry.input_queue->Pop(
          std::chrono::milliseconds(100));

      if (!frame_opt.has_value()) {
        continue;
      }

      auto& frame = frame_opt.value();
      if (!entry.stage->ProcessFrame(std::move(frame))) {
        spdlog::warn("Channel {}: stage '{}' failed to process frame",
                      channel_id_, name);
      } else {
        ++frames_processed_;
      }
    }
  }

  spdlog::debug("Channel {}: stage '{}' worker exited",
                channel_id_, name);
}

PipelineStage* ChannelPipeline::GetStage(size_t index) const {
  if (index < stages_.size()) {
    return stages_[index].stage.get();
  }
  return nullptr;
}

}  // namespace loong::core
