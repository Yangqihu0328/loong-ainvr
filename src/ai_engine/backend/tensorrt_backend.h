// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_BACKEND_TENSORRT_BACKEND_H_
#define LOONG_AI_ENGINE_BACKEND_TENSORRT_BACKEND_H_

#ifdef LOONG_HAS_TENSORRT

#include <memory>
#include <string>
#include <vector>

#include <NvInfer.h>
#include <cuda_runtime_api.h>

#include "ai_engine/inference/inference_backend.h"

namespace loong::ai_engine {

/// Custom TensorRT logger adapter.
class TrtLogger : public nvinfer1::ILogger {
 public:
  void log(Severity severity, const char* msg) noexcept override;
};

/// TensorRT inference backend with engine caching and FP16/INT8 support.
///
/// Workflow:
///   1. Check for cached `.engine` file (same model path + ".engine").
///   2. If cached engine exists and is valid → deserialize directly.
///   3. Otherwise → parse ONNX → build engine → serialize to cache file.
///   4. Create execution context → ready for inference.
class TensorRTBackend : public InferenceBackend {
 public:
  TensorRTBackend();
  ~TensorRTBackend() override;

  bool LoadModel(const std::string& model_path,
                 const BackendConfig& config) override;

  bool RunInference(const std::vector<float>& input_data,
                    const TensorShape& input_shape,
                    std::vector<float>& output_data,
                    TensorShape& output_shape) override;

  bool SupportsBatch() const override { return true; }
  int MaxBatchSize() const override { return max_batch_size_; }
  void Unload() override;
  std::string Name() const override { return "tensorrt"; }

 private:
  bool BuildEngineFromOnnx(const std::string& onnx_path,
                           const BackendConfig& config);
  bool LoadCachedEngine(const std::string& engine_path);
  bool SaveEngine(const std::string& engine_path);
  bool AllocateBuffers();
  void FreeBuffers();
  std::string EngineCachePath(const std::string& model_path) const;

  TrtLogger logger_;

  // TensorRT objects (using raw pointers — TRT uses its own ref counting)
  nvinfer1::IRuntime* runtime_ = nullptr;
  nvinfer1::ICudaEngine* engine_ = nullptr;
  nvinfer1::IExecutionContext* context_ = nullptr;

  // CUDA resources
  cudaStream_t stream_ = nullptr;
  void* device_input_ = nullptr;
  void* device_output_ = nullptr;

  // Tensor metadata
  std::string input_name_;
  std::string output_name_;
  TensorShape input_shape_;
  TensorShape output_shape_;
  size_t input_size_bytes_ = 0;
  size_t output_size_bytes_ = 0;
  int max_batch_size_ = 32;
};

}  // namespace loong::ai_engine

#endif  // LOONG_HAS_TENSORRT

#endif  // LOONG_AI_ENGINE_BACKEND_TENSORRT_BACKEND_H_
