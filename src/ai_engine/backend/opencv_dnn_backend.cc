// Copyright 2026 Loong AI NVR Project

#include "ai_engine/backend/opencv_dnn_backend.h"

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

bool OpenCVDnnBackend::LoadModel(const std::string& model_path,
                                  const BackendConfig& config) {
  try {
    net_ = cv::dnn::readNetFromONNX(model_path);
  } catch (const cv::Exception& e) {
    spdlog::error("OpenCVDnnBackend: failed to load model '{}': {}",
                   model_path, e.what());
    return false;
  }

  // Set target CPU (default) or CUDA if available
  net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
  net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);

  if (config.num_threads > 0) {
    cv::setNumThreads(config.num_threads);
  }

  loaded_ = true;
  spdlog::info("OpenCVDnnBackend: loaded model '{}'", model_path);
  return true;
}

bool OpenCVDnnBackend::RunInference(
    const std::vector<float>& input_data,
    const TensorShape& input_shape,
    std::vector<float>& output_data,
    TensorShape& output_shape) {
  if (!loaded_) return false;

  // Convert shape to OpenCV dimensions
  std::vector<int> dims;
  dims.reserve(input_shape.size());
  for (auto d : input_shape) {
    dims.push_back(static_cast<int>(d));
  }

  // Create blob from input data (the data is already preprocessed)
  cv::Mat blob(dims, CV_32F, const_cast<float*>(input_data.data()));
  net_.setInput(blob);

  cv::Mat output;
  try {
    output = net_.forward();
  } catch (const cv::Exception& e) {
    spdlog::error("OpenCVDnnBackend: inference failed: {}", e.what());
    return false;
  }

  // Flatten output to vector
  output_data.assign(reinterpret_cast<float*>(output.data),
                     reinterpret_cast<float*>(output.data) +
                         static_cast<size_t>(output.total()));

  // Extract output shape
  output_shape.clear();
  for (int i = 0; i < output.dims; ++i) {
    output_shape.push_back(static_cast<int64_t>(output.size[i]));
  }

  return true;
}

void OpenCVDnnBackend::Unload() {
  net_ = cv::dnn::Net();
  loaded_ = false;
}

}  // namespace loong::ai_engine
