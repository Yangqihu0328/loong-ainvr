// Copyright 2026 Loong AI NVR Project

#include "ai_engine/inference/inference_engine.h"

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

InferenceEngine::InferenceEngine(std::unique_ptr<InferenceBackend> backend,
                                 std::unique_ptr<YoloModelAdapter> adapter)
    : backend_(std::move(backend)), adapter_(std::move(adapter)) {}

bool InferenceEngine::Infer(const uint8_t* image_data, int width, int height,
                            float confidence_threshold,
                            std::vector<Detection>& detections) {
  std::lock_guard<std::mutex> lock(switch_mutex_);

  if (!backend_ || !backend_->IsLoaded()) return false;

  // Step 1: Preprocess
  std::vector<float> input_data;
  TensorShape input_shape;
  if (!adapter_->PreProcess(image_data, width, height, input_data,
                            input_shape)) {
    spdlog::warn("InferenceEngine: preprocess failed");
    return false;
  }

  // Step 2: Run inference
  std::vector<float> output_data;
  TensorShape output_shape;
  if (!backend_->RunInference(input_data, input_shape, output_data,
                              output_shape)) {
    spdlog::warn("InferenceEngine: inference failed");
    return false;
  }

  // Step 3: Postprocess
  float nms_threshold = 0.45F;
  if (!adapter_->PostProcess(output_data, output_shape, width, height,
                             confidence_threshold, nms_threshold, detections)) {
    spdlog::warn("InferenceEngine: postprocess failed");
    return false;
  }

  return true;
}

bool InferenceEngine::InferBatch(
    const std::vector<const uint8_t*>& images, const std::vector<int>& widths,
    const std::vector<int>& heights, float confidence_threshold,
    std::vector<std::vector<Detection>>& batch_detections) {
  if (images.empty()) return true;

  std::lock_guard<std::mutex> lock(switch_mutex_);

  if (!backend_ || !backend_->IsLoaded()) return false;

  // Use native batch inference when the backend supports it.
  if (backend_->SupportsBatch() &&
      static_cast<int>(images.size()) <= backend_->MaxBatchSize()) {
    // Step 1: Batch preprocess — build a single batched input tensor.
    std::vector<float> batch_input;
    TensorShape batch_shape;
    if (!adapter_->PreProcessBatch(images, widths, heights, batch_input,
                                   batch_shape)) {
      spdlog::warn("InferenceEngine: batch preprocess failed");
      return false;
    }

    // Step 2: Run batched inference in one forward pass.
    std::vector<float> batch_output;
    TensorShape output_shape;
    if (!backend_->RunInference(batch_input, batch_shape, batch_output,
                                output_shape)) {
      spdlog::warn("InferenceEngine: batch inference failed");
      return false;
    }

    // Step 3: Batch postprocess — split output per image.
    float nms_threshold = 0.45F;
    if (!adapter_->PostProcessBatch(batch_output, output_shape, widths, heights,
                                    confidence_threshold, nms_threshold,
                                    batch_detections)) {
      spdlog::warn("InferenceEngine: batch postprocess failed");
      return false;
    }

    return true;
  }

  // Fallback: sequential inference for non-batch backends.
  batch_detections.resize(images.size());
  for (size_t i = 0; i < images.size(); ++i) {
    // Call single-frame pipeline (preprocess → infer → postprocess).
    std::vector<float> input_data;
    TensorShape input_shape;
    if (!adapter_->PreProcess(images[i], widths[i], heights[i], input_data,
                              input_shape)) {
      spdlog::warn("InferenceEngine: batch item {} preprocess failed", i);
      continue;
    }

    std::vector<float> output_data;
    TensorShape output_shape;
    if (!backend_->RunInference(input_data, input_shape, output_data,
                                output_shape)) {
      spdlog::warn("InferenceEngine: batch item {} inference failed", i);
      continue;
    }

    float nms_threshold = 0.45F;
    if (!adapter_->PostProcess(output_data, output_shape, widths[i], heights[i],
                               confidence_threshold, nms_threshold,
                               batch_detections[i])) {
      spdlog::warn("InferenceEngine: batch item {} postprocess failed", i);
    }
  }

  return true;
}

bool InferenceEngine::SwitchModel(const std::string& model_path,
                                  std::unique_ptr<YoloModelAdapter> new_adapter,
                                  const BackendConfig& config) {
  std::lock_guard<std::mutex> lock(switch_mutex_);

  spdlog::info("InferenceEngine: switching model to '{}'", model_path);

  // Load new model
  backend_->Unload();
  if (!backend_->LoadModel(model_path, config)) {
    spdlog::error("InferenceEngine: failed to load new model");
    return false;
  }

  adapter_ = std::move(new_adapter);
  spdlog::info("InferenceEngine: model switched to {}",
               adapter_->ModelFamily());
  return true;
}

std::string InferenceEngine::ModelFamily() const {
  std::lock_guard<std::mutex> lock(switch_mutex_);
  return adapter_ ? adapter_->ModelFamily() : "none";
}

std::string InferenceEngine::BackendName() const {
  return backend_ ? backend_->Name() : "none";
}

}  // namespace loong::ai_engine
