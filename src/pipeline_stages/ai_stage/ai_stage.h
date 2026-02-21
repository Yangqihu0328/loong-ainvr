// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_PIPELINE_STAGES_AI_STAGE_AI_STAGE_H_
#define LOONG_PIPELINE_STAGES_AI_STAGE_AI_STAGE_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <thread>

#include "ai_engine/cascade/model_cascade.h"
#include "ai_engine/inference/inference_engine.h"
#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"

namespace loong::pipeline_stages {

/// AI inference stage — runs YOLO detection on decoded frames.
/// Supports both single-frame and batch inference modes.
/// When batch_size > 1 and the backend supports batching, frames are
/// accumulated and inferred together for higher GPU throughput.
///
/// FEAT-2.1: When a ModelCascade is configured, uses multi-model cascade
/// inference instead of the single InferenceEngine path.
class AiStage : public core::PipelineStage {
 public:
  AiStage() = default;
  ~AiStage() override = default;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  bool ProcessBatch(std::vector<std::shared_ptr<Frame>>& frames) override;
  bool SupportsBatch() const override { return false; }
  int GetBatchSize() const override { return batch_size_; }
  void Shutdown() override;
  std::string Name() const override { return "AiStage"; }

  /// Set the inference engine (single-model mode, dependency injection).
  void SetInferenceEngine(
      std::shared_ptr<ai_engine::InferenceEngine> engine);

  /// Set the model cascade (multi-model mode). Takes priority over single engine.
  void SetModelCascade(
      std::shared_ptr<ai_engine::ModelCascade> cascade);

 private:
  /// Run single-frame inference on one frame (single engine path).
  bool InferSingle(std::shared_ptr<Frame>& frame);

  /// Run cascade inference on one frame (multi-model path).
  bool InferCascade(std::shared_ptr<Frame>& frame);

  void InferWorker();

  std::shared_ptr<ai_engine::InferenceEngine> engine_;
  std::shared_ptr<ai_engine::ModelCascade> cascade_;
  float confidence_threshold_ = 0.5F;
  int batch_size_ = 1;
  int skip_frames_ = 0;
  int frame_counter_ = 0;
  bool initialized_ = false;

  // Async inference: latest result shared between worker and pipeline thread.
  std::mutex result_mutex_;
  AnalysisResult last_result_;

  // Async inference worker thread and pending frame.
  std::thread infer_thread_;
  std::mutex infer_mutex_;
  std::condition_variable infer_cv_;
  std::shared_ptr<Frame> pending_frame_;
  std::atomic<bool> infer_running_{false};
  std::atomic<bool> shutdown_infer_{false};

  // Stats.
  std::atomic<int64_t> infer_count_{0};
  std::atomic<int64_t> total_infer_us_{0};
  int64_t pass_count_ = 0;
  std::chrono::steady_clock::time_point stats_start_ =
      std::chrono::steady_clock::now();
};

}  // namespace loong::pipeline_stages

#endif  // LOONG_PIPELINE_STAGES_AI_STAGE_AI_STAGE_H_
