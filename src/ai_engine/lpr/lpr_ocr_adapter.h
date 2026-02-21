// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_LPR_LPR_OCR_ADAPTER_H_
#define LOONG_AI_ENGINE_LPR_LPR_OCR_ADAPTER_H_

#include <string>
#include <vector>

#include "ai_engine/yolo_adapter/yolo_model_adapter.h"

namespace loong::ai_engine {

/// CRNN-based license plate OCR adapter.
///
/// Pre-processing: resize plate crop to (100, 32) grayscale, normalize.
/// Post-processing: CTC greedy decode of CRNN output → plate number string.
///
/// The recognized plate text is stored as a Detection with:
///   - class_name = plate number string (e.g. "京A12345")
///   - class_id = 0
///   - confidence = average character confidence
///   - bbox = full image bounds (0,0,w,h)
///
/// This allows reuse with the standard InferenceEngine pipeline.
class LprOcrAdapter : public YoloModelAdapter {
 public:
  /// Create an OCR adapter with the given input dimensions and character set.
  explicit LprOcrAdapter(int input_width = 100, int input_height = 32);

  bool PreProcess(const uint8_t* image_data, int width, int height,
                  std::vector<float>& input_data,
                  TensorShape& input_shape) override;

  bool PostProcess(const std::vector<float>& output_data,
                   const TensorShape& output_shape,
                   int original_width, int original_height,
                   float confidence_threshold, float nms_threshold,
                   std::vector<Detection>& detections) override;

  std::string ModelFamily() const override { return "lpr_ocr"; }
  TensorShape InputShape() const override {
    return {1, 1, input_height_, input_width_};
  }

  /// Set the character vocabulary (index 0 = CTC blank token).
  void SetCharset(std::vector<std::string> charset);

  /// Get the default Chinese plate character set.
  static std::vector<std::string> DefaultChineseCharset();

  /// CTC greedy decode: argmax per time step, collapse repeats, remove blanks.
  static std::string CtcGreedyDecode(const std::vector<float>& logits,
                                     int time_steps, int num_classes,
                                     const std::vector<std::string>& charset,
                                     float* avg_confidence = nullptr);

 private:
  int input_width_;
  int input_height_;
  std::vector<std::string> charset_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_LPR_LPR_OCR_ADAPTER_H_
