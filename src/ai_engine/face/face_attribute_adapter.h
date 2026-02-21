// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_FACE_FACE_ATTRIBUTE_ADAPTER_H_
#define LOONG_AI_ENGINE_FACE_FACE_ATTRIBUTE_ADAPTER_H_

#include <array>
#include <string>
#include <vector>

#include "ai_engine/yolo_adapter/yolo_model_adapter.h"

namespace loong::ai_engine {

/// Result of face attribute analysis (age, gender, embedding).
struct FaceAttributes {
  int age = -1;                         // Estimated age
  std::string gender;                   // "male" / "female" / "unknown"
  float gender_confidence = 0.0F;
  std::vector<float> embedding;         // 512-d feature vector (L2-normalized)
};

/// ArcFace / InsightFace style attribute adapter.
///
/// Input: 112×112 aligned face crop (RGB, NCHW, normalized)
/// Output layout (combined model): [512-d embedding, 1 gender logit, 1 age value]
///   Total: 514 floats
///
/// For embedding-only models: output is 512 floats.
///
/// Works in kCrop mode within ModelCascade, receiving face ROI from FaceDetectorAdapter.
class FaceAttributeAdapter : public YoloModelAdapter {
 public:
  explicit FaceAttributeAdapter(int input_size = 112);

  bool PreProcess(const uint8_t* image_data, int width, int height,
                  std::vector<float>& input_data,
                  TensorShape& input_shape) override;

  bool PostProcess(const std::vector<float>& output_data,
                   const TensorShape& output_shape,
                   int original_width, int original_height,
                   float confidence_threshold, float nms_threshold,
                   std::vector<Detection>& detections) override;

  std::string ModelFamily() const override { return "face_attribute"; }
  TensorShape InputShape() const override {
    return {1, 3, static_cast<int64_t>(input_size_),
            static_cast<int64_t>(input_size_)};
  }

  /// Get attributes from the last PostProcess call.
  const FaceAttributes& GetLastAttributes() const { return last_attrs_; }

  /// Normalize embedding to unit L2 norm.
  static void L2Normalize(std::vector<float>& vec);

  /// Compute cosine similarity between two embeddings.
  static float CosineSimilarity(const std::vector<float>& a,
                                const std::vector<float>& b);

  static constexpr int kEmbeddingDim = 512;
  static constexpr int kFullOutputDim = 514;  // 512 + gender + age

 private:
  int input_size_;
  FaceAttributes last_attrs_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_FACE_FACE_ATTRIBUTE_ADAPTER_H_
