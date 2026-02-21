// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_detector_adapter.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace loong::ai_engine {

FaceDetectorAdapter::FaceDetectorAdapter(int input_size)
    : input_size_(input_size) {}

bool FaceDetectorAdapter::PreProcess(const uint8_t* image_data, int width,
                                     int height, std::vector<float>& input_data,
                                     TensorShape& input_shape) {
  if (!image_data || width <= 0 || height <= 0) return false;

  LetterboxResize(image_data, width, height, input_data);
  input_shape = {1, 3, static_cast<int64_t>(input_size_),
                 static_cast<int64_t>(input_size_)};
  return true;
}

bool FaceDetectorAdapter::PostProcess(const std::vector<float>& output_data,
                                      const TensorShape& output_shape,
                                      int original_width, int original_height,
                                      float confidence_threshold,
                                      float nms_threshold,
                                      std::vector<Detection>& detections) {
  last_faces_.clear();

  // SCRFD output: [N, 15] where 15 = 4(bbox) + 1(score) + 10(5 landmarks×2)
  // Or [N, 5] for bbox-only models
  if (output_shape.size() < 2) return false;

  auto num_dets = static_cast<size_t>(output_shape[0]);
  auto cols = static_cast<size_t>(output_shape.back());

  bool has_landmarks = (cols >= 15);

  for (size_t i = 0; i < num_dets; ++i) {
    size_t offset = i * cols;
    if (offset + 4 >= output_data.size()) break;

    float x1 = output_data[offset];
    float y1 = output_data[offset + 1];
    float x2 = output_data[offset + 2];
    float y2 = output_data[offset + 3];
    float score = output_data[offset + 4];

    if (score < confidence_threshold) continue;

    // Undo letterbox transform
    float fx1 = (x1 - static_cast<float>(pad_x_)) / scale_x_;
    float fy1 = (y1 - static_cast<float>(pad_y_)) / scale_y_;
    float fx2 = (x2 - static_cast<float>(pad_x_)) / scale_x_;
    float fy2 = (y2 - static_cast<float>(pad_y_)) / scale_y_;

    fx1 = std::max(0.0F, std::min(fx1, static_cast<float>(original_width)));
    fy1 = std::max(0.0F, std::min(fy1, static_cast<float>(original_height)));
    fx2 = std::max(0.0F, std::min(fx2, static_cast<float>(original_width)));
    fy2 = std::max(0.0F, std::min(fy2, static_cast<float>(original_height)));

    FaceDetection face;
    face.box = {fx1, fy1, fx2, fy2, score, 0, "face"};

    if (has_landmarks && offset + 14 < output_data.size()) {
      for (int k = 0; k < 5; ++k) {
        float lx = output_data[offset + 5 + static_cast<size_t>(k) * 2];
        float ly = output_data[offset + 6 + static_cast<size_t>(k) * 2];
        face.landmarks[static_cast<size_t>(k)] = {
            (lx - static_cast<float>(pad_x_)) / scale_x_,
            (ly - static_cast<float>(pad_y_)) / scale_y_};
      }
    }

    last_faces_.push_back(face);
  }

  ApplyNMS(last_faces_, nms_threshold);

  detections.clear();
  detections.reserve(last_faces_.size());
  for (const auto& f : last_faces_) {
    detections.push_back(f.box);
  }

  return true;
}

void FaceDetectorAdapter::LetterboxResize(const uint8_t* src, int src_w,
                                          int src_h, std::vector<float>& dst) {
  auto is = static_cast<size_t>(input_size_);
  dst.assign(3 * is * is, 0.5F);

  float scale =
      std::min(static_cast<float>(input_size_) / static_cast<float>(src_w),
               static_cast<float>(input_size_) / static_cast<float>(src_h));
  int new_w = static_cast<int>(static_cast<float>(src_w) * scale);
  int new_h = static_cast<int>(static_cast<float>(src_h) * scale);
  pad_x_ = (input_size_ - new_w) / 2;
  pad_y_ = (input_size_ - new_h) / 2;
  scale_x_ = scale;
  scale_y_ = scale;

  auto unw = static_cast<size_t>(new_w);
  auto unh = static_cast<size_t>(new_h);
  auto upx = static_cast<size_t>(pad_x_);
  auto upy = static_cast<size_t>(pad_y_);
  auto usw = static_cast<size_t>(src_w);
  auto ush = static_cast<size_t>(src_h);

  for (size_t y = 0; y < unh; ++y) {
    for (size_t x = 0; x < unw; ++x) {
      size_t orig_x = x * usw / unw;
      size_t orig_y = y * ush / unh;
      if (orig_x >= usw) orig_x = usw - 1;
      if (orig_y >= ush) orig_y = ush - 1;

      size_t src_idx = (orig_y * usw + orig_x) * 3;
      float b = static_cast<float>(src[src_idx]) / 255.0F;
      float g = static_cast<float>(src[src_idx + 1]) / 255.0F;
      float r = static_cast<float>(src[src_idx + 2]) / 255.0F;

      size_t dy = upy + y;
      size_t dx = upx + x;
      // NCHW: R, G, B channels
      dst[0 * is * is + dy * is + dx] = r;
      dst[1 * is * is + dy * is + dx] = g;
      dst[2 * is * is + dy * is + dx] = b;
    }
  }
}

float FaceDetectorAdapter::ComputeIou(const Detection& a, const Detection& b) {
  float ix1 = std::max(a.x1, b.x1);
  float iy1 = std::max(a.y1, b.y1);
  float ix2 = std::min(a.x2, b.x2);
  float iy2 = std::min(a.y2, b.y2);

  float inter = std::max(0.0F, ix2 - ix1) * std::max(0.0F, iy2 - iy1);
  float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
  float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
  float union_area = area_a + area_b - inter;
  return (union_area > 0.0F) ? (inter / union_area) : 0.0F;
}

void FaceDetectorAdapter::ApplyNMS(std::vector<FaceDetection>& faces,
                                   float nms_threshold) {
  std::sort(faces.begin(), faces.end(),
            [](const FaceDetection& a, const FaceDetection& b) {
              return a.box.confidence > b.box.confidence;
            });

  std::vector<bool> suppressed(faces.size(), false);
  std::vector<FaceDetection> result;

  for (size_t i = 0; i < faces.size(); ++i) {
    if (suppressed[i]) continue;
    result.push_back(faces[i]);
    for (size_t j = i + 1; j < faces.size(); ++j) {
      if (suppressed[j]) continue;
      if (ComputeIou(faces[i].box, faces[j].box) > nms_threshold) {
        suppressed[j] = true;
      }
    }
  }

  faces = std::move(result);
}

}  // namespace loong::ai_engine
