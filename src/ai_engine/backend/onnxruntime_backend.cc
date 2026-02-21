// Copyright 2026 Loong AI NVR Project

#include "ai_engine/backend/onnxruntime_backend.h"

#include "spdlog/spdlog.h"

#include <numeric>
#include <stdexcept>

namespace loong {
namespace ai_engine {

OnnxRuntimeBackend::OnnxRuntimeBackend() {
  env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LoongAINVR");
}

OnnxRuntimeBackend::~OnnxRuntimeBackend() { Unload(); }

bool OnnxRuntimeBackend::TryAddCudaProvider(Ort::SessionOptions& options,
                                            int device_id) {
#ifdef LOONG_ORT_CUDA
  try {
    OrtCUDAProviderOptions cuda_opts;
    cuda_opts.device_id = device_id;
    cuda_opts.arena_extend_strategy = 0;  // kNextPowerOfTwo
    cuda_opts.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
    cuda_opts.do_copy_in_default_stream = 1;
    options.AppendExecutionProvider_CUDA(cuda_opts);
    spdlog::info("OnnxRuntimeBackend: CUDA EP enabled (device={})", device_id);
    return true;
  } catch (const Ort::Exception& e) {
    spdlog::warn("OnnxRuntimeBackend: CUDA EP unavailable: {}", e.what());
    return false;
  }
#else
  (void)options;
  (void)device_id;
  spdlog::info("OnnxRuntimeBackend: built without CUDA support, using CPU");
  return false;
#endif
}

bool OnnxRuntimeBackend::LoadModel(const std::string& model_path,
                                   const BackendConfig& config) {
  try {
    Ort::SessionOptions session_opts;
    session_opts.SetIntraOpNumThreads(
        config.num_threads > 0 ? config.num_threads : 4);
    session_opts.SetGraphOptimizationLevel(
        GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (config.enable_fp16) {
      spdlog::info("OnnxRuntimeBackend: FP16 requested (depends on EP)");
    }

    using_gpu_ = TryAddCudaProvider(session_opts, config.device_id);

    session_ =
        std::make_unique<Ort::Session>(*env_, model_path.c_str(), session_opts);

    // Retrieve input tensor names.
    input_names_.clear();
    size_t num_inputs = session_->GetInputCount();
    for (size_t i = 0; i < num_inputs; ++i) {
      auto name = session_->GetInputNameAllocated(i, allocator_);
      input_names_.emplace_back(name.get());
    }

    // Retrieve output tensor names.
    output_names_.clear();
    size_t num_outputs = session_->GetOutputCount();
    for (size_t i = 0; i < num_outputs; ++i) {
      auto name = session_->GetOutputNameAllocated(i, allocator_);
      output_names_.emplace_back(name.get());
    }

    // Read the first input tensor shape and check batching support.
    supports_batch_ = false;
    input_shape_.clear();
    if (num_inputs > 0) {
      auto type_info = session_->GetInputTypeInfo(0);
      auto tensor_info = type_info.GetTensorTypeAndShapeInfo();
      auto shape = tensor_info.GetShape();
      input_shape_.assign(shape.begin(), shape.end());
      if (!shape.empty() && (shape[0] == -1 || shape[0] > 1)) {
        supports_batch_ = true;
      }
    }

    loaded_ = true;
    spdlog::info(
        "OnnxRuntimeBackend: loaded '{}' ({} inputs, {} outputs, {}, batch={})",
        model_path, num_inputs, num_outputs, using_gpu_ ? "GPU" : "CPU",
        supports_batch_ ? "dynamic" : "1");
    return true;

  } catch (const Ort::Exception& e) {
    spdlog::error("OnnxRuntimeBackend: failed to load '{}': {}", model_path,
                  e.what());
    return false;
  }
}

bool OnnxRuntimeBackend::RunInference(const std::vector<float>& input_data,
                                      const TensorShape& input_shape,
                                      std::vector<float>& output_data,
                                      TensorShape& output_shape) {
  if (!loaded_ || !session_) {
    spdlog::warn("OnnxRuntimeBackend: model not loaded");
    return false;
  }

  try {
    auto memory_info =
        Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    // Create input tensor from caller-provided flat data.
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        memory_info, const_cast<float*>(input_data.data()), input_data.size(),
        input_shape.data(), input_shape.size());

    // Build C-string name arrays for Run().
    std::vector<const char*> input_name_ptrs;
    input_name_ptrs.reserve(input_names_.size());
    for (const auto& n : input_names_) {
      input_name_ptrs.push_back(n.c_str());
    }

    std::vector<const char*> output_name_ptrs;
    output_name_ptrs.reserve(output_names_.size());
    for (const auto& n : output_names_) {
      output_name_ptrs.push_back(n.c_str());
    }

    auto results = session_->Run(
        Ort::RunOptions{nullptr}, input_name_ptrs.data(), &input_tensor, 1,
        output_name_ptrs.data(), output_name_ptrs.size());

    if (results.empty()) {
      spdlog::warn("OnnxRuntimeBackend: empty output");
      return false;
    }

    // Extract the first output tensor.
    auto& out_tensor = results.front();
    auto type_info = out_tensor.GetTensorTypeAndShapeInfo();

    output_shape.clear();
    auto shape = type_info.GetShape();
    output_shape.assign(shape.begin(), shape.end());

    size_t total_elements = static_cast<size_t>(
        std::accumulate(shape.begin(), shape.end(), static_cast<int64_t>(1),
                        std::multiplies<>()));

    const float* out_ptr = out_tensor.GetTensorData<float>();
    output_data.assign(out_ptr, out_ptr + total_elements);

    return true;

  } catch (const Ort::Exception& e) {
    spdlog::error("OnnxRuntimeBackend: inference failed: {}", e.what());
    return false;
  }
}

void OnnxRuntimeBackend::Unload() {
  session_.reset();
  input_names_.clear();
  output_names_.clear();
  loaded_ = false;
  using_gpu_ = false;
}

}  // namespace ai_engine
}  // namespace loong
