// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_YOLO_ADAPTER_YOLO_MODEL_ADAPTER_H_
#define LOONG_AI_ENGINE_YOLO_ADAPTER_YOLO_MODEL_ADAPTER_H_

#include <string>
#include <vector>

#include "ai_engine/inference/inference_backend.h"
#include "core/common/types.h"

namespace loong::ai_engine {

/// Abstract YOLO model adapter for version-specific pre/post processing.
/// Each YOLO version (v5, v7, v8, v9, v10, v11) implements this interface.
class YoloModelAdapter {
 public:
  virtual ~YoloModelAdapter() = default;

  /// Preprocess a raw image into model input tensor.
  /// image_data: raw BGR pixel data
  /// width, height: image dimensions
  /// input_data: output flattened float tensor
  /// input_shape: output tensor shape
  virtual bool PreProcess(const uint8_t* image_data,
                          int width, int height,
                          std::vector<float>& input_data,
                          TensorShape& input_shape) = 0;

  /// Batch preprocess: concatenate multiple images into a single batched
  /// input tensor for native batch inference on GPU.
  /// Default implementation calls PreProcess() for each image and merges.
  virtual bool PreProcessBatch(
      const std::vector<const uint8_t*>& images,
      const std::vector<int>& widths,
      const std::vector<int>& heights,
      std::vector<float>& batch_input_data,
      TensorShape& batch_input_shape);

  /// Postprocess model output into detection results.
  virtual bool PostProcess(const std::vector<float>& output_data,
                           const TensorShape& output_shape,
                           int original_width, int original_height,
                           float confidence_threshold,
                           float nms_threshold,
                           std::vector<Detection>& detections) = 0;

  /// Batch postprocess: split batched model output and decode each.
  /// Default implementation splits the output tensor along dim-0 and calls
  /// PostProcess() for each image.
  virtual bool PostProcessBatch(
      const std::vector<float>& output_data,
      const TensorShape& output_shape,
      const std::vector<int>& original_widths,
      const std::vector<int>& original_heights,
      float confidence_threshold,
      float nms_threshold,
      std::vector<std::vector<Detection>>& batch_detections);

  /// Get the YOLO family name (e.g., "yolov8").
  virtual std::string ModelFamily() const = 0;

  /// Get the expected input tensor shape (single image, batch=1).
  virtual TensorShape InputShape() const = 0;
};

/// Factory for creating YOLO adapters by model family name.
class YoloAdapterFactory {
 public:
  static std::unique_ptr<YoloModelAdapter> Create(
      const std::string& family, int input_size = 640);
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_YOLO_ADAPTER_YOLO_MODEL_ADAPTER_H_
