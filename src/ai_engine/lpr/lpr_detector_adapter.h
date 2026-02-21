// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_LPR_LPR_DETECTOR_ADAPTER_H_
#define LOONG_AI_ENGINE_LPR_LPR_DETECTOR_ADAPTER_H_

#include "ai_engine/yolo_adapter/yolov8_adapter.h"

namespace loong::ai_engine {

/// License plate detection adapter.
///
/// Wraps a YOLOv8-format model trained on plate detection.
/// Output detections have class_name set to plate color (blue/green/yellow/white)
/// based on class_id mapping. Compatible with ModelCascade kCrop mode.
class LprDetectorAdapter : public YoloV8Adapter {
 public:
  explicit LprDetectorAdapter(int input_size = 640);

  std::string ModelFamily() const override { return "lpr_detector"; }

  /// Remap class IDs to plate color names after standard YOLO post-processing.
  bool PostProcess(const std::vector<float>& output_data,
                   const TensorShape& output_shape,
                   int original_width, int original_height,
                   float confidence_threshold, float nms_threshold,
                   std::vector<Detection>& detections) override;

  /// Set plate color class names (default: blue, green, yellow, white).
  void SetPlateClasses(std::vector<std::string> classes);

 private:
  std::vector<std::string> plate_classes_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_LPR_LPR_DETECTOR_ADAPTER_H_
