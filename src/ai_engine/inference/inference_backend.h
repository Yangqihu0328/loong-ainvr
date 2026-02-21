// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_INFERENCE_INFERENCE_BACKEND_H_
#define LOONG_AI_ENGINE_INFERENCE_INFERENCE_BACKEND_H_

#include <cstddef>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// Shape of a tensor (e.g., {1, 3, 640, 640}).
using TensorShape = std::vector<int64_t>;

/// Backend configuration.
struct BackendConfig {
  int device_id = 0;           // GPU device ID
  int num_threads = 1;         // CPU threads for inference
  bool enable_fp16 = false;    // Half-precision inference
  bool enable_int8 = false;    // INT8 quantized inference (TensorRT)
  std::string calibration_data_path;  // INT8 calibration data
  size_t workspace_mb = 256;   // TensorRT workspace size
};

/// Abstract inference backend interface.
/// Wraps ONNX Runtime, TensorRT, or OpenCV DNN.
class InferenceBackend {
 public:
  virtual ~InferenceBackend() = default;

  /// Load a model from file.
  virtual bool LoadModel(const std::string& model_path,
                         const BackendConfig& config) = 0;

  /// Run inference on a batch of input data.
  /// input_data: flattened float tensor
  /// input_shape: shape of the input tensor
  /// output_data: will be filled with flattened output tensor
  /// output_shape: will be filled with output tensor shape
  virtual bool RunInference(const std::vector<float>& input_data,
                            const TensorShape& input_shape,
                            std::vector<float>& output_data,
                            TensorShape& output_shape) = 0;

  /// Check if this backend supports batch inference.
  virtual bool SupportsBatch() const = 0;

  /// Get maximum supported batch size.
  virtual int MaxBatchSize() const = 0;

  /// Check if a model is currently loaded and ready for inference.
  virtual bool IsLoaded() const = 0;

  /// Unload the model and free resources.
  virtual void Unload() = 0;

  /// Get backend name.
  virtual std::string Name() const = 0;

  /// Return the first input tensor shape (e.g., {1,3,640,640}).
  /// Default returns empty; backends override after model loading.
  virtual TensorShape GetInputShape() const { return {}; }
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_INFERENCE_INFERENCE_BACKEND_H_
