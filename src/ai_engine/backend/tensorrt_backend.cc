// Copyright 2026 Loong AI NVR Project

#ifdef LOONG_HAS_TENSORRT

#include "ai_engine/backend/tensorrt_backend.h"

#include <NvOnnxParser.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <numeric>

#include "spdlog/spdlog.h"

namespace loong {
namespace ai_engine {

// ============================================================
// TrtLogger
// ============================================================

void TrtLogger::log(Severity severity, const char* msg) noexcept {
  switch (severity) {
    case Severity::kINTERNAL_ERROR:
    case Severity::kERROR:
      spdlog::error("[TensorRT] {}", msg);
      break;
    case Severity::kWARNING:
      spdlog::warn("[TensorRT] {}", msg);
      break;
    case Severity::kINFO:
      spdlog::debug("[TensorRT] {}", msg);
      break;
    default:
      break;
  }
}

// ============================================================
// TensorRTBackend
// ============================================================

TensorRTBackend::TensorRTBackend() = default;

TensorRTBackend::~TensorRTBackend() {
  Unload();
}

bool TensorRTBackend::LoadModel(const std::string& model_path,
                                const BackendConfig& config) {
  std::string cache_path = EngineCachePath(model_path);

  // Try loading cached engine first.
  if (LoadCachedEngine(cache_path)) {
    spdlog::info("TensorRT: loaded cached engine from '{}'", cache_path);
  } else {
    spdlog::info("TensorRT: building engine from ONNX '{}'...", model_path);
    if (!BuildEngineFromOnnx(model_path, config)) {
      return false;
    }
    SaveEngine(cache_path);
    spdlog::info("TensorRT: engine cached to '{}'", cache_path);
  }

  // Create execution context.
  context_ = engine_->createExecutionContext();
  if (!context_) {
    spdlog::error("TensorRT: failed to create execution context");
    return false;
  }

  // Create CUDA stream.
  if (cudaStreamCreate(&stream_) != cudaSuccess) {
    spdlog::error("TensorRT: failed to create CUDA stream");
    return false;
  }

  // Determine I/O tensor shapes.
  int nb_io = engine_->getNbIOTensors();
  for (int i = 0; i < nb_io; ++i) {
    const char* name = engine_->getIOTensorName(i);
    auto mode = engine_->getTensorIOMode(name);
    auto dims = engine_->getTensorShape(name);

    TensorShape shape;
    for (int d = 0; d < dims.nbDims; ++d) {
      shape.push_back(static_cast<int64_t>(dims.d[d]));
    }

    if (mode == nvinfer1::TensorIOMode::kINPUT) {
      input_name_ = name;
      input_shape_ = shape;
    } else {
      output_name_ = name;
      output_shape_ = shape;
    }
  }

  if (!AllocateBuffers()) return false;

  spdlog::info(
      "TensorRT: ready (input='{}' output='{}' max_batch={})",
      input_name_, output_name_, max_batch_size_);
  return true;
}

bool TensorRTBackend::RunInference(const std::vector<float>& input_data,
                                   const TensorShape& input_shape,
                                   std::vector<float>& output_data,
                                   TensorShape& output_shape) {
  if (!context_ || !stream_) return false;

  size_t in_bytes = input_data.size() * sizeof(float);
  if (in_bytes > input_size_bytes_) {
    spdlog::error("TensorRT: input too large ({} > {})", in_bytes,
                  input_size_bytes_);
    return false;
  }

  // Set dynamic input shape if needed.
  nvinfer1::Dims dims;
  dims.nbDims = static_cast<int>(input_shape.size());
  for (size_t i = 0; i < input_shape.size(); ++i) {
    dims.d[i] = static_cast<int>(input_shape[i]);
  }
  context_->setInputShape(input_name_.c_str(), dims);

  // Host → Device
  cudaMemcpyAsync(device_input_, input_data.data(), in_bytes,
                  cudaMemcpyHostToDevice, stream_);

  // Bind I/O
  context_->setTensorAddress(input_name_.c_str(), device_input_);
  context_->setTensorAddress(output_name_.c_str(), device_output_);

  // Execute
  if (!context_->enqueueV3(stream_)) {
    spdlog::error("TensorRT: enqueueV3 failed");
    return false;
  }

  // Compute output size.
  auto out_dims = context_->getTensorShape(output_name_.c_str());
  output_shape.clear();
  int64_t out_elements = 1;
  for (int i = 0; i < out_dims.nbDims; ++i) {
    output_shape.push_back(static_cast<int64_t>(out_dims.d[i]));
    out_elements *= static_cast<int64_t>(out_dims.d[i]);
  }

  output_data.resize(static_cast<size_t>(out_elements));
  size_t out_bytes = static_cast<size_t>(out_elements) * sizeof(float);

  // Device → Host
  cudaMemcpyAsync(output_data.data(), device_output_, out_bytes,
                  cudaMemcpyDeviceToHost, stream_);
  cudaStreamSynchronize(stream_);

  return true;
}

void TensorRTBackend::Unload() {
  FreeBuffers();

  if (stream_) {
    cudaStreamDestroy(stream_);
    stream_ = nullptr;
  }
  if (context_) {
    delete context_;
    context_ = nullptr;
  }
  if (engine_) {
    delete engine_;
    engine_ = nullptr;
  }
  if (runtime_) {
    delete runtime_;
    runtime_ = nullptr;
  }
}

// ============================================================
// Private helpers
// ============================================================

bool TensorRTBackend::BuildEngineFromOnnx(const std::string& onnx_path,
                                          const BackendConfig& config) {
  auto builder =
      std::unique_ptr<nvinfer1::IBuilder>(nvinfer1::createInferBuilder(logger_));
  if (!builder) return false;

  auto network = std::unique_ptr<nvinfer1::INetworkDefinition>(
      builder->createNetworkV2(
          1U << static_cast<uint32_t>(
              nvinfer1::NetworkDefinitionCreationFlag::kEXPLICIT_BATCH)));
  if (!network) return false;

  auto parser = std::unique_ptr<nvonnxparser::IParser>(
      nvonnxparser::createParser(*network, logger_));
  if (!parser) return false;

  if (!parser->parseFromFile(onnx_path.c_str(),
                             static_cast<int>(nvinfer1::ILogger::Severity::kWARNING))) {
    spdlog::error("TensorRT: failed to parse ONNX file");
    return false;
  }

  auto build_config = std::unique_ptr<nvinfer1::IBuilderConfig>(
      builder->createBuilderConfig());
  if (!build_config) return false;

  build_config->setMemoryPoolLimit(nvinfer1::MemoryPoolType::kWORKSPACE,
                                   config.workspace_mb * (1ULL << 20));

  if (config.enable_fp16 && builder->platformHasFastFp16()) {
    build_config->setFlag(nvinfer1::BuilderFlag::kFP16);
    spdlog::info("TensorRT: FP16 enabled");
  }

  if (config.enable_int8 && builder->platformHasFastInt8()) {
    build_config->setFlag(nvinfer1::BuilderFlag::kINT8);
    spdlog::info("TensorRT: INT8 enabled (calibration data='{}')",
                 config.calibration_data_path);
  }

  // Set dynamic batch size: min=1, opt=batch/2, max=batch
  auto input = network->getInput(0);
  auto profile = builder->createOptimizationProfile();
  auto in_dims = input->getDimensions();

  nvinfer1::Dims min_dims = in_dims;
  nvinfer1::Dims opt_dims = in_dims;
  nvinfer1::Dims max_dims = in_dims;
  min_dims.d[0] = 1;
  opt_dims.d[0] = std::max(1, max_batch_size_ / 2);
  max_dims.d[0] = max_batch_size_;

  profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMIN,
                         min_dims);
  profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kOPT,
                         opt_dims);
  profile->setDimensions(input->getName(), nvinfer1::OptProfileSelector::kMAX,
                         max_dims);
  build_config->addOptimizationProfile(profile);

  // Build serialized engine.
  auto plan = std::unique_ptr<nvinfer1::IHostMemory>(
      builder->buildSerializedNetwork(*network, *build_config));
  if (!plan) {
    spdlog::error("TensorRT: failed to build serialized network");
    return false;
  }

  runtime_ = nvinfer1::createInferRuntime(logger_);
  if (!runtime_) return false;

  engine_ = runtime_->deserializeCudaEngine(plan->data(), plan->size());
  return engine_ != nullptr;
}

bool TensorRTBackend::LoadCachedEngine(const std::string& engine_path) {
  std::ifstream file(engine_path, std::ios::binary);
  if (!file.is_open()) return false;

  file.seekg(0, std::ios::end);
  auto size = file.tellg();
  file.seekg(0, std::ios::beg);

  if (size <= 0) return false;

  std::vector<char> data(static_cast<size_t>(size));
  file.read(data.data(), size);

  runtime_ = nvinfer1::createInferRuntime(logger_);
  if (!runtime_) return false;

  engine_ = runtime_->deserializeCudaEngine(data.data(), data.size());
  return engine_ != nullptr;
}

bool TensorRTBackend::SaveEngine(const std::string& engine_path) {
  if (!engine_) return false;

  auto plan = std::unique_ptr<nvinfer1::IHostMemory>(
      engine_->serialize());
  if (!plan) return false;

  std::ofstream file(engine_path, std::ios::binary);
  if (!file.is_open()) return false;

  file.write(static_cast<const char*>(plan->data()),
             static_cast<std::streamsize>(plan->size()));
  return true;
}

bool TensorRTBackend::AllocateBuffers() {
  // Compute max buffer sizes using max batch.
  int64_t in_elements = 1;
  for (size_t i = 0; i < input_shape_.size(); ++i) {
    int64_t d = input_shape_[i];
    if (i == 0) d = max_batch_size_;  // dynamic batch dim
    in_elements *= d;
  }
  input_size_bytes_ = static_cast<size_t>(in_elements) * sizeof(float);

  int64_t out_elements = 1;
  for (size_t i = 0; i < output_shape_.size(); ++i) {
    int64_t d = output_shape_[i];
    if (i == 0) d = max_batch_size_;
    out_elements *= d;
  }
  output_size_bytes_ = static_cast<size_t>(out_elements) * sizeof(float);

  if (cudaMalloc(&device_input_, input_size_bytes_) != cudaSuccess) {
    spdlog::error("TensorRT: failed to allocate input GPU memory");
    return false;
  }
  if (cudaMalloc(&device_output_, output_size_bytes_) != cudaSuccess) {
    spdlog::error("TensorRT: failed to allocate output GPU memory");
    cudaFree(device_input_);
    device_input_ = nullptr;
    return false;
  }
  return true;
}

void TensorRTBackend::FreeBuffers() {
  if (device_input_) {
    cudaFree(device_input_);
    device_input_ = nullptr;
  }
  if (device_output_) {
    cudaFree(device_output_);
    device_output_ = nullptr;
  }
}

std::string TensorRTBackend::EngineCachePath(
    const std::string& model_path) const {
  return model_path + ".engine";
}

}  // namespace ai_engine
}  // namespace loong

#endif  // LOONG_HAS_TENSORRT
