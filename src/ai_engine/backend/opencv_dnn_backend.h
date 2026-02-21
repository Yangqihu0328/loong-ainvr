// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_BACKEND_OPENCV_DNN_BACKEND_H_
#define LOONG_AI_ENGINE_BACKEND_OPENCV_DNN_BACKEND_H_

#include "ai_engine/inference/inference_backend.h"

#include <opencv2/dnn.hpp>

namespace loong::ai_engine {

/// OpenCV DNN inference backend.
/// Supports ONNX model format via OpenCV's built-in DNN module.
/// Works on CPU without additional runtime dependencies.
class OpenCVDnnBackend : public InferenceBackend {
 public:
  OpenCVDnnBackend() = default;
  ~OpenCVDnnBackend() override = default;

  bool LoadModel(const std::string& model_path,
                 const BackendConfig& config) override;
  bool RunInference(const std::vector<float>& input_data,
                    const TensorShape& input_shape,
                    std::vector<float>& output_data,
                    TensorShape& output_shape) override;
  bool IsLoaded() const override { return loaded_; }
  bool SupportsBatch() const override { return false; }
  int MaxBatchSize() const override { return 1; }
  void Unload() override;
  std::string Name() const override { return "OpenCVDNN"; }

 private:
  cv::dnn::Net net_;
  bool loaded_ = false;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_BACKEND_OPENCV_DNN_BACKEND_H_
