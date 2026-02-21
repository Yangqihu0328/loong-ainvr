// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_attribute_adapter.h"
#include "ai_engine/face/face_detector_adapter.h"
#include "ai_engine/lpr/lpr_detector_adapter.h"
#include "ai_engine/lpr/lpr_ocr_adapter.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "ai_engine/yolo_adapter/yolov5_adapter.h"
#include "ai_engine/yolo_adapter/yolov8_adapter.h"
#include "spdlog/spdlog.h"

#include <numeric>

namespace loong::ai_engine {

// ---------------------------------------------------------------------------
// YoloModelAdapter default batch implementations
// ---------------------------------------------------------------------------

bool YoloModelAdapter::PreProcessBatch(
    const std::vector<const uint8_t*>& images, const std::vector<int>& widths,
    const std::vector<int>& heights, std::vector<float>& batch_input_data,
    TensorShape& batch_input_shape) {
  if (images.empty()) return false;

  size_t batch_size = images.size();

  // Preprocess first image to determine per-image tensor size.
  std::vector<float> first_data;
  TensorShape single_shape;
  if (!PreProcess(images[0], widths[0], heights[0], first_data, single_shape)) {
    return false;
  }

  size_t per_image_elements = first_data.size();

  // Allocate the full batch tensor.
  batch_input_data.resize(batch_size * per_image_elements);
  std::copy(first_data.begin(), first_data.end(), batch_input_data.begin());

  // Preprocess remaining images.
  for (size_t i = 1; i < batch_size; ++i) {
    std::vector<float> img_data;
    TensorShape img_shape;
    if (!PreProcess(images[i], widths[i], heights[i], img_data, img_shape)) {
      spdlog::warn("PreProcessBatch: image {} failed", i);
      return false;
    }
    std::copy(img_data.begin(), img_data.end(),
              batch_input_data.begin() +
                  static_cast<ptrdiff_t>(i * per_image_elements));
  }

  // Build batch shape: replace dim-0 with batch_size.
  batch_input_shape = single_shape;
  batch_input_shape[0] = static_cast<int64_t>(batch_size);

  return true;
}

bool YoloModelAdapter::PostProcessBatch(
    const std::vector<float>& output_data, const TensorShape& output_shape,
    const std::vector<int>& original_widths,
    const std::vector<int>& original_heights, float confidence_threshold,
    float nms_threshold,
    std::vector<std::vector<Detection>>& batch_detections) {
  if (output_shape.empty() || original_widths.empty()) return false;

  auto batch_size = static_cast<size_t>(output_shape[0]);
  batch_detections.resize(batch_size);

  // Calculate per-image element count from remaining dimensions.
  size_t per_image_elements = 1;
  TensorShape single_shape = output_shape;
  single_shape[0] = 1;
  for (size_t d = 1; d < output_shape.size(); ++d) {
    per_image_elements *= static_cast<size_t>(output_shape[d]);
  }

  for (size_t i = 0; i < batch_size; ++i) {
    size_t offset = i * per_image_elements;
    std::vector<float> single_output(
        output_data.begin() + static_cast<ptrdiff_t>(offset),
        output_data.begin() +
            static_cast<ptrdiff_t>(offset + per_image_elements));

    if (!PostProcess(single_output, single_shape, original_widths[i],
                     original_heights[i], confidence_threshold, nms_threshold,
                     batch_detections[i])) {
      spdlog::warn("PostProcessBatch: image {} failed", i);
    }
  }

  return true;
}

// ---------------------------------------------------------------------------
// YoloAdapterFactory
// ---------------------------------------------------------------------------

std::unique_ptr<YoloModelAdapter> YoloAdapterFactory::Create(
    const std::string& family, int input_size) {
  if (family == "yolov5" || family == "yolov5s" || family == "yolov5n") {
    return std::make_unique<YoloV5Adapter>(input_size);
  }
  if (family == "yolov7") {
    return std::make_unique<YoloV5Adapter>(input_size);
  }
  if (family == "yolov8" || family == "yolov8n" || family == "yolov8s" ||
      family == "yolov9" || family == "yolov10" || family == "yolov11") {
    return std::make_unique<YoloV8Adapter>(input_size);
  }

  if (family == "lpr_detector" || family == "lpr_det") {
    return std::make_unique<LprDetectorAdapter>();
  }
  if (family == "lpr_ocr" || family == "plate_ocr" || family == "crnn") {
    return std::make_unique<LprOcrAdapter>();
  }

  if (family == "face_detector" || family == "scrfd" ||
      family == "retinaface") {
    return std::make_unique<FaceDetectorAdapter>();
  }
  if (family == "face_attribute" || family == "arcface" ||
      family == "insightface") {
    return std::make_unique<FaceAttributeAdapter>();
  }

  spdlog::warn("YoloAdapterFactory: unknown family '{}', defaulting to v8",
               family);
  return std::make_unique<YoloV8Adapter>(input_size);
}

}  // namespace loong::ai_engine
