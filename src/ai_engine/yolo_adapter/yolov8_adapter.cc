// Copyright 2026 Loong AI NVR Project

#include "ai_engine/yolo_adapter/yolov8_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace loong::ai_engine {

YoloV8Adapter::YoloV8Adapter(int input_size) : input_size_(input_size) {}

bool YoloV8Adapter::PreProcess(const uint8_t* image_data,
                                int width, int height,
                                std::vector<float>& input_data,
                                TensorShape& input_shape) {
  // Same letterbox preprocessing as YOLOv5
  LetterboxResize(image_data, width, height, input_data);
  input_shape = {1, 3, input_size_, input_size_};
  return true;
}

bool YoloV8Adapter::PostProcess(const std::vector<float>& output_data,
                                 const TensorShape& output_shape,
                                 int original_width, int original_height,
                                 float confidence_threshold,
                                 float nms_threshold,
                                 std::vector<Detection>& detections) {
  // YOLOv8 output: [1, 84, 8400] — transposed vs YOLOv5
  // 84 = 4 (cx, cy, w, h) + 80 (class_scores) — no objectness score
  if (output_shape.size() < 3) return false;

  int num_features = static_cast<int>(output_shape[1]);  // 84
  int num_detections = static_cast<int>(output_shape[2]);  // 8400
  int num_classes = num_features - 4;

  detections.clear();

  size_t nd = static_cast<size_t>(num_detections);
  for (int i = 0; i < num_detections; ++i) {
    auto idx = static_cast<size_t>(i);
    // Transposed access: data[feature][detection]
    float cx = output_data[0 * nd + idx];
    float cy = output_data[1 * nd + idx];
    float w = output_data[2 * nd + idx];
    float h = output_data[3 * nd + idx];

    // Find best class score (no objectness multiplication in v8)
    int best_class = 0;
    float best_score = 0.0F;
    for (int c = 0; c < num_classes; ++c) {
      float score =
          output_data[(4 + static_cast<size_t>(c)) * nd + idx];
      if (score > best_score) {
        best_score = score;
        best_class = c;
      }
    }

    if (best_score < confidence_threshold) continue;

    // Map from letterbox to original image coordinates
    float x1 = (cx - w / 2.0F - static_cast<float>(pad_x_)) / scale_x_;
    float y1 = (cy - h / 2.0F - static_cast<float>(pad_y_)) / scale_y_;
    float x2 = (cx + w / 2.0F - static_cast<float>(pad_x_)) / scale_x_;
    float y2 = (cy + h / 2.0F - static_cast<float>(pad_y_)) / scale_y_;

    x1 = std::max(0.0F, std::min(x1, static_cast<float>(original_width)));
    y1 = std::max(0.0F, std::min(y1, static_cast<float>(original_height)));
    x2 = std::max(0.0F, std::min(x2, static_cast<float>(original_width)));
    y2 = std::max(0.0F, std::min(y2, static_cast<float>(original_height)));

    Detection det;
    det.x1 = x1;
    det.y1 = y1;
    det.x2 = x2;
    det.y2 = y2;
    det.confidence = best_score;
    det.class_id = best_class;
    detections.push_back(det);
  }

  ApplyNMS(detections, nms_threshold);
  return true;
}

void YoloV8Adapter::LetterboxResize(const uint8_t* src,
                                     int src_w, int src_h,
                                     std::vector<float>& dst) {
  float scale = std::min(static_cast<float>(input_size_) / static_cast<float>(src_w),
                         static_cast<float>(input_size_) / static_cast<float>(src_h));
  int new_w = static_cast<int>(static_cast<float>(src_w) * scale);
  int new_h = static_cast<int>(static_cast<float>(src_h) * scale);
  pad_x_ = (input_size_ - new_w) / 2;
  pad_y_ = (input_size_ - new_h) / 2;
  scale_x_ = scale;
  scale_y_ = scale;

  cv::Mat src_mat(src_h, src_w, CV_8UC3, const_cast<uint8_t*>(src));
  cv::Mat resized;
  cv::resize(src_mat, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

  cv::Mat padded(input_size_, input_size_, CV_8UC3, cv::Scalar(114, 114, 114));
  resized.copyTo(padded(cv::Rect(pad_x_, pad_y_, new_w, new_h)));

  cv::Mat rgb;
  cv::cvtColor(padded, rgb, cv::COLOR_BGR2RGB);

  cv::Mat float_img;
  rgb.convertTo(float_img, CV_32FC3, 1.0 / 255.0);

  std::vector<cv::Mat> channels;
  cv::split(float_img, channels);

  size_t plane = static_cast<size_t>(input_size_) * static_cast<size_t>(input_size_);
  dst.resize(3 * plane);
  std::memcpy(dst.data(), channels[0].data, plane * sizeof(float));
  std::memcpy(dst.data() + plane, channels[1].data, plane * sizeof(float));
  std::memcpy(dst.data() + 2 * plane, channels[2].data, plane * sizeof(float));
}

void YoloV8Adapter::ApplyNMS(std::vector<Detection>& detections,
                              float nms_threshold) {
  if (detections.empty()) return;

  std::sort(detections.begin(), detections.end(),
            [](const Detection& a, const Detection& b) {
              return a.confidence > b.confidence;
            });

  std::vector<bool> suppressed(detections.size(), false);

  for (size_t i = 0; i < detections.size(); ++i) {
    if (suppressed[i]) continue;
    for (size_t j = i + 1; j < detections.size(); ++j) {
      if (suppressed[j]) continue;
      if (detections[i].class_id != detections[j].class_id) continue;

      float ix1 = std::max(detections[i].x1, detections[j].x1);
      float iy1 = std::max(detections[i].y1, detections[j].y1);
      float ix2 = std::min(detections[i].x2, detections[j].x2);
      float iy2 = std::min(detections[i].y2, detections[j].y2);

      float inter_area = std::max(0.0F, ix2 - ix1) *
                         std::max(0.0F, iy2 - iy1);
      float area_i = (detections[i].x2 - detections[i].x1) *
                     (detections[i].y2 - detections[i].y1);
      float area_j = (detections[j].x2 - detections[j].x1) *
                     (detections[j].y2 - detections[j].y1);
      float union_area = area_i + area_j - inter_area;

      if (union_area > 0.0F && (inter_area / union_area) > nms_threshold) {
        suppressed[j] = true;
      }
    }
  }

  std::vector<Detection> result;
  for (size_t i = 0; i < detections.size(); ++i) {
    if (!suppressed[i]) result.push_back(detections[i]);
  }
  detections = std::move(result);
}

}  // namespace loong::ai_engine
