// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_PIPELINE_CHANNEL_PIPELINE_H_
#define LOONG_CORE_PIPELINE_CHANNEL_PIPELINE_H_

#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"
#include "core/pipeline/stage_queue.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace loong::core {

/// A channel pipeline orchestrates a sequence of processing stages
/// for a single video channel: Input → Decode → AI → Overlay → Output.
///
/// Each stage is connected via StageQueues. A worker thread per stage
/// pulls frames from its input queue, processes them, and pushes to
/// the output queue.
class ChannelPipeline {
 public:
  explicit ChannelPipeline(int channel_id);
  ~ChannelPipeline();

  /// Add a stage to the pipeline (in order).
  void AddStage(std::unique_ptr<PipelineStage> stage,
                const StageConfig& config);

  /// Initialize all stages.
  bool Initialize();

  /// Start processing — launches worker threads.
  bool Start();

  /// Stop processing — signals shutdown and joins worker threads.
  bool Stop();

  /// Drain remaining frames and then stop.
  bool DrainAndStop();

  /// Get current pipeline state.
  ChannelState GetState() const;

  /// Get channel ID.
  int GetChannelId() const { return channel_id_; }

  /// Get the input queue (for pushing frames from external source).
  std::shared_ptr<StageQueue> GetInputQueue() const;

  /// Get pipeline statistics.
  int64_t FramesProcessed() const;
  int64_t FramesDropped() const;

  /// Get a stage by index (for querying runtime info like stream params).
  PipelineStage* GetStage(size_t index) const;

  // Non-copyable
  ChannelPipeline(const ChannelPipeline&) = delete;
  ChannelPipeline& operator=(const ChannelPipeline&) = delete;

 private:
  struct StageEntry {
    std::unique_ptr<PipelineStage> stage;
    StageConfig config;
    std::shared_ptr<StageQueue> input_queue;
    std::thread worker;
  };

  void StageWorkerLoop(size_t stage_index);

  int channel_id_;
  std::vector<StageEntry> stages_;
  std::atomic<ChannelState> state_{ChannelState::kCreated};
  std::atomic<bool> running_{false};
  std::atomic<int64_t> frames_processed_{0};
};

}  // namespace loong::core

#endif  // LOONG_CORE_PIPELINE_CHANNEL_PIPELINE_H_
