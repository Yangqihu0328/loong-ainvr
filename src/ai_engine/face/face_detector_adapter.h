// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_FACE_FACE_DETECTOR_ADAPTER_H_
#define LOONG_AI_ENGINE_FACE_FACE_DETECTOR_ADAPTER_H_

#include "ai_engine/yolo_adapter/yolo_model_adapter.h"

#include <array>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// A 2D facial landmark (x, y in absolute pixel coordinates).
struct FaceLandmark {
  float x = 0.0F;
  float y = 0.0F;
};

/// Extended face detection with 5 landmarks.
struct FaceDetection {
  Detection box;  // Bounding box + confidence
  std::array<FaceLandmark, 5>
      landmarks;  // left_eye, right_eye, nose, mouth_l, mouth_r
};

/// SCRFD/RetinaFace-style face detection adapter.
///
/// Pre-processing: letterbox resize + BGR→RGB + normalize [0,1] + NCHW
/// Post-processing: anchor-free detection head → face bbox + 5 landmarks
///
/// The adapter outputs standard Detection objects where class_name = "face".
/// Landmarks are encoded into a companion vector accessible via
/// GetLastLandmarks().
///
/// Compatible with ModelCascade kPrimary or kCrop modes.
class FaceDetectorAdapter : public YoloModelAdapter {
 public:
  explicit FaceDetectorAdapter(int input_size = 640);

  bool PreProcess(const uint8_t* image_data, int width, int height,
                  std::vector<float>& input_data,
                  TensorShape& input_shape) override;

  bool PostProcess(const std::vector<float>& output_data,
                   const TensorShape& output_shape, int original_width,
                   int original_height, float confidence_threshold,
                   float nms_threshold,
                   std::vector<Detection>& detections) override;

  std::string ModelFamily() const override { return "face_detector"; }
  TensorShape InputShape() const override {
    return {1, 3, input_size_, input_size_};
  }

  /// Get face detections with landmarks from the last PostProcess call.
  const std::vector<FaceDetection>& GetLastFaceDetections() const {
    return last_faces_;
  }

 private:
  void LetterboxResize(const uint8_t* src, int src_w, int src_h,
                       std::vector<float>& dst);
  void ApplyNMS(std::vector<FaceDetection>& faces, float nms_threshold);

  static float ComputeIou(const Detection& a, const Detection& b);

  int input_size_;
  float scale_x_ = 1.0F;
  float scale_y_ = 1.0F;
  int pad_x_ = 0;
  int pad_y_ = 0;

  std::vector<FaceDetection> last_faces_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_FACE_FACE_DETECTOR_ADAPTER_H_
