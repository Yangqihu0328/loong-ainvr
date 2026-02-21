// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_CASCADE_MODEL_CASCADE_H_
#define LOONG_AI_ENGINE_CASCADE_MODEL_CASCADE_H_

#include "ai_engine/inference/inference_engine.h"
#include "core/common/types.h"

#include <memory>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// Execution mode for a cascade step.
enum class CascadeMode {
  kPrimary,  // Full-frame detection (first model in the cascade)
  kCrop,  // ROI crop — triggered by primary detections, crops and re-infers
  kParallel,  // Independent full-frame model running alongside the primary
};

inline const char* CascadeModeToString(CascadeMode m) {
  switch (m) {
    case CascadeMode::kPrimary:
      return "primary";
    case CascadeMode::kCrop:
      return "crop";
    case CascadeMode::kParallel:
      return "parallel";
  }
  return "unknown";
}

/// Configuration for one step in a model cascade.
struct CascadeStep {
  std::string name;        // Step display name
  std::string model_name;  // Registered model name
  CascadeMode mode = CascadeMode::kPrimary;
  std::vector<std::string>
      trigger_classes;  // Trigger only on these classes (empty = all)
  float confidence_threshold = 0.5F;
  float roi_expand_ratio =
      0.1F;  // Expand ROI box by this ratio before cropping
};

/// Orchestrates multi-model inference as a cascade pipeline.
///
/// Execution order:
///  1. All kPrimary steps run on the full frame (typically exactly one).
///  2. All kParallel steps run on the full frame independently.
///  3. All kCrop steps run on ROIs extracted from the primary detections
///     that match the step's trigger_classes filter.
///
/// Results from primary and parallel steps populate
/// `AnalysisResult::detections`. Results from crop steps populate
/// `AnalysisResult::secondary_results`.
class ModelCascade {
 public:
  ModelCascade() = default;

  /// Add a cascade step with its associated inference engine.
  void AddStep(const CascadeStep& step,
               std::shared_ptr<InferenceEngine> engine);

  /// Run the full cascade on a raw frame.
  bool Run(const uint8_t* image_data, int width, int height,
           AnalysisResult& result);

  /// Get the number of configured steps.
  size_t StepCount() const { return steps_.size(); }

  /// Check if the cascade has any steps.
  bool Empty() const { return steps_.empty(); }

  /// Get step configs (read-only).
  const std::vector<CascadeStep>& Steps() const;

  /// Crop a ROI from the source image. Returns the cropped BGR pixel buffer.
  static std::vector<uint8_t> CropRoi(const uint8_t* image_data, int img_width,
                                      int img_height, int channels, float x1,
                                      float y1, float x2, float y2,
                                      float expand_ratio, int& crop_width,
                                      int& crop_height);

 private:
  struct StepEntry {
    CascadeStep config;
    std::shared_ptr<InferenceEngine> engine;
  };

  bool RunPrimaryOrParallel(const StepEntry& entry, const uint8_t* image_data,
                            int width, int height, AnalysisResult& result);

  bool RunCrop(const StepEntry& entry, const uint8_t* image_data, int width,
               int height, const std::vector<Detection>& primary_detections,
               AnalysisResult& result);

  std::vector<StepEntry> steps_;
  std::vector<CascadeStep> step_configs_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_CASCADE_MODEL_CASCADE_H_
