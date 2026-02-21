// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_INFERENCE_INFERENCE_ENGINE_H_
#define LOONG_AI_ENGINE_INFERENCE_INFERENCE_ENGINE_H_

#include "ai_engine/inference/inference_backend.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "core/common/types.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// High-level inference engine combining a backend with a YOLO adapter.
/// Provides unified Infer/InferBatch API and model hot-swap capability.
class InferenceEngine {
 public:
  InferenceEngine(std::unique_ptr<InferenceBackend> backend,
                  std::unique_ptr<YoloModelAdapter> adapter);
  ~InferenceEngine() = default;

  /// Run inference on a single raw image frame.
  bool Infer(const uint8_t* image_data, int width, int height,
             float confidence_threshold, std::vector<Detection>& detections);

  /// Run batch inference on multiple frames.
  bool InferBatch(const std::vector<const uint8_t*>& images,
                  const std::vector<int>& widths,
                  const std::vector<int>& heights, float confidence_threshold,
                  std::vector<std::vector<Detection>>& batch_detections);

  /// Hot-swap the model (thread-safe).
  bool SwitchModel(const std::string& model_path,
                   std::unique_ptr<YoloModelAdapter> new_adapter,
                   const BackendConfig& config);

  /// Get the current model family name.
  std::string ModelFamily() const;

  /// Get inference backend name.
  std::string BackendName() const;

 private:
  std::unique_ptr<InferenceBackend> backend_;
  std::unique_ptr<YoloModelAdapter> adapter_;
  mutable std::mutex switch_mutex_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_INFERENCE_INFERENCE_ENGINE_H_
