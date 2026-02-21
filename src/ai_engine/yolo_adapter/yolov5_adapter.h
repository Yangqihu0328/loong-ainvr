// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_YOLO_ADAPTER_YOLOV5_ADAPTER_H_
#define LOONG_AI_ENGINE_YOLO_ADAPTER_YOLOV5_ADAPTER_H_

#include "ai_engine/yolo_adapter/yolo_model_adapter.h"

namespace loong::ai_engine {

/// YOLOv5 model adapter.
/// Pre-processing: Letterbox resize + BGR→RGB + normalize [0,1] + NCHW
/// Post-processing: (batch, 25200, 85) → NMS → detections
class YoloV5Adapter : public YoloModelAdapter {
 public:
  explicit YoloV5Adapter(int input_size = 640);
  ~YoloV5Adapter() override = default;

  bool PreProcess(const uint8_t* image_data, int width, int height,
                  std::vector<float>& input_data,
                  TensorShape& input_shape) override;

  bool PostProcess(const std::vector<float>& output_data,
                   const TensorShape& output_shape, int original_width,
                   int original_height, float confidence_threshold,
                   float nms_threshold,
                   std::vector<Detection>& detections) override;

  std::string ModelFamily() const override { return "yolov5"; }
  TensorShape InputShape() const override {
    return {1, 3, input_size_, input_size_};
  }

 private:
  void LetterboxResize(const uint8_t* src, int src_w, int src_h,
                       std::vector<float>& dst);
  void ApplyNMS(std::vector<Detection>& detections, float nms_threshold);

  int input_size_;
  float scale_x_ = 1.0F;
  float scale_y_ = 1.0F;
  int pad_x_ = 0;
  int pad_y_ = 0;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_YOLO_ADAPTER_YOLOV5_ADAPTER_H_
