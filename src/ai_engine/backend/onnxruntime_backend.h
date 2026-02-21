// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_BACKEND_ONNXRUNTIME_BACKEND_H_
#define LOONG_AI_ENGINE_BACKEND_ONNXRUNTIME_BACKEND_H_

#include "ai_engine/inference/inference_backend.h"

#include <memory>
#include <onnxruntime_cxx_api.h>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// ONNX Runtime inference backend.
/// Supports GPU (CUDA EP) and CPU execution providers with native batch
/// inference. Delivers significantly higher throughput than OpenCV DNN,
/// especially when batching multiple frames on GPU.
class OnnxRuntimeBackend : public InferenceBackend {
 public:
  OnnxRuntimeBackend();
  ~OnnxRuntimeBackend() override;

  // Non-copyable, non-movable (due to ONNX Runtime session ownership).
  OnnxRuntimeBackend(const OnnxRuntimeBackend&) = delete;
  OnnxRuntimeBackend& operator=(const OnnxRuntimeBackend&) = delete;

  bool LoadModel(const std::string& model_path,
                 const BackendConfig& config) override;

  bool RunInference(const std::vector<float>& input_data,
                    const TensorShape& input_shape,
                    std::vector<float>& output_data,
                    TensorShape& output_shape) override;

  bool IsLoaded() const override { return loaded_; }
  bool SupportsBatch() const override { return supports_batch_; }
  int MaxBatchSize() const override { return max_batch_size_; }
  void Unload() override;
  std::string Name() const override { return "OnnxRuntime"; }
  TensorShape GetInputShape() const override { return input_shape_; }

 private:
  /// Try to append CUDA execution provider.
  /// Returns true if CUDA EP was successfully added.
  bool TryAddCudaProvider(Ort::SessionOptions& options, int device_id);

  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> session_;
  Ort::AllocatorWithDefaultOptions allocator_;

  std::vector<std::string> input_names_;
  std::vector<std::string> output_names_;

  bool loaded_ = false;
  bool using_gpu_ = false;
  bool supports_batch_ = false;
  int max_batch_size_ = 32;
  TensorShape input_shape_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_BACKEND_ONNXRUNTIME_BACKEND_H_
