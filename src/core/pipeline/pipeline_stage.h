// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_PIPELINE_PIPELINE_STAGE_H_
#define LOONG_CORE_PIPELINE_PIPELINE_STAGE_H_

#include "core/common/types.h"
#include "core/pipeline/stage_queue.h"

#include <memory>
#include <string>

namespace loong::core {

/// Abstract base class for all pipeline stages.
/// Each stage processes frames and optionally passes them to the next stage.
/// Stages that support batch processing should override SupportsBatch() and
/// ProcessBatch(). The pipeline worker will automatically use batch mode
/// when the stage supports it.
class PipelineStage {
 public:
  virtual ~PipelineStage() = default;

  /// Initialize the stage with given configuration.
  virtual bool Initialize(const StageConfig& config) = 0;

  /// Process a single frame. Returns true on success.
  /// Implementations should call PassToNext() to forward the frame.
  virtual bool ProcessFrame(std::shared_ptr<Frame> frame) = 0;

  /// Process a batch of frames. Default implementation calls ProcessFrame
  /// for each frame sequentially. Batch-capable stages (like AiStage)
  /// override this for native batch inference.
  virtual bool ProcessBatch(std::vector<std::shared_ptr<Frame>>& frames) {
    for (auto& f : frames) {
      if (!ProcessFrame(std::move(f))) return false;
    }
    return true;
  }

  /// Whether this stage supports batch processing.
  /// Override to return true if the stage benefits from batching.
  virtual bool SupportsBatch() const { return false; }

  /// Maximum batch size this stage wants to process at once.
  virtual int GetBatchSize() const { return 1; }

  /// Called when the pipeline starts (after Initialize, before worker loops).
  /// Source stages (e.g., InputStage) override this to begin producing data.
  virtual void OnPipelineStart() {}

  /// Called when the pipeline stops (before worker threads are joined).
  /// Source stages override this to stop producing data.
  virtual void OnPipelineStop() {}

  /// Shutdown the stage, releasing all resources.
  virtual void Shutdown() = 0;

  /// Get the stage name (for logging and diagnostics).
  virtual std::string Name() const = 0;

  /// Set the output queue for passing frames to the next stage.
  void SetOutputQueue(std::shared_ptr<StageQueue> queue) {
    output_queue_ = std::move(queue);
  }

  /// Get the output queue.
  std::shared_ptr<StageQueue> GetOutputQueue() const { return output_queue_; }

 protected:
  /// Pass a processed frame to the next stage via the output queue.
  bool PassToNext(std::shared_ptr<Frame> frame) {
    if (output_queue_) {
      return output_queue_->Push(std::move(frame));
    }
    return true;  // No next stage (terminal stage)
  }

 private:
  std::shared_ptr<StageQueue> output_queue_;
};

}  // namespace loong::core

#endif  // LOONG_CORE_PIPELINE_PIPELINE_STAGE_H_
