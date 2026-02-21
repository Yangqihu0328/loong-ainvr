// Copyright 2026 Loong AI NVR Project

#include "ai_engine/cascade/model_cascade.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

void ModelCascade::AddStep(const CascadeStep& step,
                           std::shared_ptr<InferenceEngine> engine) {
  steps_.push_back({step, std::move(engine)});
  step_configs_.push_back(step);
  spdlog::debug("ModelCascade: added step '{}' mode={} model={}",
                step.name, CascadeModeToString(step.mode), step.model_name);
}

bool ModelCascade::Run(const uint8_t* image_data, int width, int height,
                       AnalysisResult& result) {
  if (steps_.empty()) return false;

  auto total_start = std::chrono::steady_clock::now();

  // Phase 1+2: Run primary and parallel steps (full frame)
  for (auto& entry : steps_) {
    if (entry.config.mode == CascadeMode::kPrimary ||
        entry.config.mode == CascadeMode::kParallel) {
      RunPrimaryOrParallel(entry, image_data, width, height, result);
    }
  }

  // Phase 3: Run crop steps on primary detections
  for (auto& entry : steps_) {
    if (entry.config.mode == CascadeMode::kCrop) {
      RunCrop(entry, image_data, width, height,
              result.detections, result);
    }
  }

  auto total_end = std::chrono::steady_clock::now();
  result.inference_time_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          total_end - total_start)
          .count();

  result.has_result = true;
  return true;
}

const std::vector<CascadeStep>& ModelCascade::Steps() const {
  return step_configs_;
}

bool ModelCascade::RunPrimaryOrParallel(const StepEntry& entry,
                                        const uint8_t* image_data,
                                        int width, int height,
                                        AnalysisResult& result) {
  std::vector<Detection> detections;
  bool ok = entry.engine->Infer(image_data, width, height,
                                entry.config.confidence_threshold,
                                detections);
  if (!ok) {
    spdlog::warn("ModelCascade: step '{}' inference failed", entry.config.name);
    return false;
  }

  for (auto& det : detections) {
    result.detections.push_back(std::move(det));
  }

  spdlog::debug("ModelCascade: step '{}' produced {} detections",
                entry.config.name, detections.size());
  return true;
}

bool ModelCascade::RunCrop(const StepEntry& entry,
                           const uint8_t* image_data,
                           int width, int height,
                           const std::vector<Detection>& primary_detections,
                           AnalysisResult& result) {
  const auto& triggers = entry.config.trigger_classes;

  for (size_t i = 0; i < primary_detections.size(); ++i) {
    const auto& det = primary_detections[i];

    // Filter by trigger classes
    if (!triggers.empty()) {
      if (std::find(triggers.begin(), triggers.end(), det.class_name) ==
          triggers.end()) {
        continue;
      }
    }

    // Crop ROI from the original image
    int crop_w = 0, crop_h = 0;
    constexpr int kChannels = 3;  // BGR
    auto cropped = CropRoi(image_data, width, height, kChannels,
                           det.x1, det.y1, det.x2, det.y2,
                           entry.config.roi_expand_ratio,
                           crop_w, crop_h);
    if (cropped.empty() || crop_w <= 0 || crop_h <= 0) continue;

    // Run secondary model on cropped ROI
    std::vector<Detection> sub_detections;
    bool ok = entry.engine->Infer(cropped.data(), crop_w, crop_h,
                                  entry.config.confidence_threshold,
                                  sub_detections);
    if (!ok) continue;

    // Map sub-detection coordinates back to the original frame
    float roi_x1 = std::max(0.0F, det.x1 - (det.x2 - det.x1) *
                                       entry.config.roi_expand_ratio);
    float roi_y1 = std::max(0.0F, det.y1 - (det.y2 - det.y1) *
                                       entry.config.roi_expand_ratio);

    for (auto& sub : sub_detections) {
      sub.x1 = roi_x1 + sub.x1;
      sub.y1 = roi_y1 + sub.y1;
      sub.x2 = roi_x1 + sub.x2;
      sub.y2 = roi_y1 + sub.y2;
    }

    SecondaryResult sec;
    sec.parent_index = static_cast<int>(i);
    sec.model_name = entry.config.model_name;
    sec.detections = std::move(sub_detections);
    result.secondary_results.push_back(std::move(sec));

    spdlog::debug("ModelCascade: crop step '{}' on det[{}] '{}' → {} sub-dets",
                  entry.config.name, i, det.class_name,
                  result.secondary_results.back().detections.size());
  }
  return true;
}

std::vector<uint8_t> ModelCascade::CropRoi(
    const uint8_t* image_data,
    int img_width, int img_height, int channels,
    float x1, float y1, float x2, float y2,
    float expand_ratio,
    int& crop_width, int& crop_height) {
  float box_w = x2 - x1;
  float box_h = y2 - y1;

  // Expand the ROI
  float ex1 = x1 - box_w * expand_ratio;
  float ey1 = y1 - box_h * expand_ratio;
  float ex2 = x2 + box_w * expand_ratio;
  float ey2 = y2 + box_h * expand_ratio;

  // Clamp to image boundaries
  int cx1 = std::max(0, static_cast<int>(ex1));
  int cy1 = std::max(0, static_cast<int>(ey1));
  int cx2 = std::min(img_width, static_cast<int>(ex2 + 0.5F));
  int cy2 = std::min(img_height, static_cast<int>(ey2 + 0.5F));

  crop_width = cx2 - cx1;
  crop_height = cy2 - cy1;

  if (crop_width <= 0 || crop_height <= 0) return {};

  auto ucrop_w = static_cast<size_t>(crop_width);
  auto ucrop_h = static_cast<size_t>(crop_height);
  auto uchannels = static_cast<size_t>(channels);

  size_t row_bytes = ucrop_w * uchannels;
  std::vector<uint8_t> buf(row_bytes * ucrop_h);

  size_t src_stride = static_cast<size_t>(img_width) * uchannels;
  for (size_t row = 0; row < ucrop_h; ++row) {
    size_t src_offset =
        (static_cast<size_t>(cy1) + row) * src_stride +
        static_cast<size_t>(cx1) * uchannels;
    std::memcpy(buf.data() + row * row_bytes,
                image_data + src_offset, row_bytes);
  }

  return buf;
}

}  // namespace loong::ai_engine
