// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_attribute_adapter.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

FaceAttributeAdapter::FaceAttributeAdapter(int input_size)
    : input_size_(input_size) {}

bool FaceAttributeAdapter::PreProcess(const uint8_t* image_data,
                                      int width, int height,
                                      std::vector<float>& input_data,
                                      TensorShape& input_shape) {
  if (!image_data || width <= 0 || height <= 0) return false;

  auto is = static_cast<size_t>(input_size_);
  auto uw = static_cast<size_t>(width);
  auto uh = static_cast<size_t>(height);

  input_data.resize(3 * is * is);
  input_shape = {1, 3, static_cast<int64_t>(input_size_),
                 static_cast<int64_t>(input_size_)};

  // Bilinear-ish resize (nearest-neighbor) + BGR→RGB + normalize to [-1, 1]
  for (size_t y = 0; y < is; ++y) {
    for (size_t x = 0; x < is; ++x) {
      size_t src_x = x * uw / is;
      size_t src_y = y * uh / is;
      if (src_x >= uw) src_x = uw - 1;
      if (src_y >= uh) src_y = uh - 1;

      size_t src_idx = (src_y * uw + src_x) * 3;
      float b = static_cast<float>(image_data[src_idx]);
      float g = static_cast<float>(image_data[src_idx + 1]);
      float r = static_cast<float>(image_data[src_idx + 2]);

      // Normalize to [-1, 1] (standard for ArcFace/InsightFace)
      input_data[0 * is * is + y * is + x] = (r - 127.5F) / 127.5F;
      input_data[1 * is * is + y * is + x] = (g - 127.5F) / 127.5F;
      input_data[2 * is * is + y * is + x] = (b - 127.5F) / 127.5F;
    }
  }

  return true;
}

bool FaceAttributeAdapter::PostProcess(
    const std::vector<float>& output_data,
    const TensorShape& output_shape,
    int original_width, int original_height,
    float confidence_threshold, float /*nms_threshold*/,
    std::vector<Detection>& detections) {
  last_attrs_ = {};
  detections.clear();

  if (output_data.empty()) return false;

  auto total_floats = output_data.size();

  if (total_floats >= static_cast<size_t>(kFullOutputDim)) {
    // Combined model: [512-d embedding | gender_logit | age_value]
    last_attrs_.embedding.assign(output_data.begin(),
                                 output_data.begin() + kEmbeddingDim);
    L2Normalize(last_attrs_.embedding);

    float gender_logit = output_data[kEmbeddingDim];
    float gender_prob = 1.0F / (1.0F + std::exp(-gender_logit));
    last_attrs_.gender = (gender_prob > 0.5F) ? "male" : "female";
    last_attrs_.gender_confidence = (gender_prob > 0.5F) ? gender_prob : (1.0F - gender_prob);

    last_attrs_.age = static_cast<int>(std::round(output_data[kEmbeddingDim + 1]));
    if (last_attrs_.age < 0) last_attrs_.age = 0;
    if (last_attrs_.age > 120) last_attrs_.age = 120;

  } else if (total_floats >= static_cast<size_t>(kEmbeddingDim)) {
    // Embedding-only model
    last_attrs_.embedding.assign(output_data.begin(),
                                 output_data.begin() + kEmbeddingDim);
    L2Normalize(last_attrs_.embedding);
    last_attrs_.gender = "unknown";
    last_attrs_.age = -1;

  } else {
    spdlog::warn("FaceAttributeAdapter: unexpected output size {}",
                 total_floats);
    return false;
  }

  // Encode attributes into a Detection for cascade compatibility
  Detection det;
  det.x1 = 0;
  det.y1 = 0;
  det.x2 = static_cast<float>(original_width);
  det.y2 = static_cast<float>(original_height);
  det.confidence = last_attrs_.gender_confidence;
  det.class_id = (last_attrs_.gender == "male") ? 0 : 1;
  det.class_name = "face_attr";
  detections.push_back(det);

  return true;
}

void FaceAttributeAdapter::L2Normalize(std::vector<float>& vec) {
  float norm = 0.0F;
  for (float v : vec) norm += v * v;
  norm = std::sqrt(norm);
  if (norm > 1e-6F) {
    for (float& v : vec) v /= norm;
  }
}

float FaceAttributeAdapter::CosineSimilarity(const std::vector<float>& a,
                                             const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return 0.0F;
  float dot = 0.0F, norm_a = 0.0F, norm_b = 0.0F;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  return (denom > 1e-6F) ? (dot / denom) : 0.0F;
}

}  // namespace loong::ai_engine
