// Copyright 2026 Loong AI NVR Project

#include "ai_engine/lpr/lpr_detector_adapter.h"

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

LprDetectorAdapter::LprDetectorAdapter(int input_size)
    : YoloV8Adapter(input_size),
      plate_classes_(
          {"blue_plate", "green_plate", "yellow_plate", "white_plate"}) {}

bool LprDetectorAdapter::PostProcess(const std::vector<float>& output_data,
                                     const TensorShape& output_shape,
                                     int original_width, int original_height,
                                     float confidence_threshold,
                                     float nms_threshold,
                                     std::vector<Detection>& detections) {
  bool ok = YoloV8Adapter::PostProcess(
      output_data, output_shape, original_width, original_height,
      confidence_threshold, nms_threshold, detections);
  if (!ok) return false;

  for (auto& det : detections) {
    auto idx = static_cast<size_t>(det.class_id);
    if (idx < plate_classes_.size()) {
      det.class_name = plate_classes_[idx];
    } else {
      det.class_name = "plate";
    }
  }

  return true;
}

void LprDetectorAdapter::SetPlateClasses(std::vector<std::string> classes) {
  plate_classes_ = std::move(classes);
}

}  // namespace loong::ai_engine
