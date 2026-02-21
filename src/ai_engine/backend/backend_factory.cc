// Copyright 2026 Loong AI NVR Project

#include "ai_engine/backend/backend_factory.h"

#include "ai_engine/backend/opencv_dnn_backend.h"
#ifdef LOONG_HAS_ONNXRUNTIME
#include "ai_engine/backend/onnxruntime_backend.h"
#endif
#ifdef LOONG_HAS_TENSORRT
#include "ai_engine/backend/tensorrt_backend.h"
#endif
#include "spdlog/spdlog.h"

namespace loong::ai_engine {

std::unique_ptr<InferenceBackend> BackendFactory::Create(
    const std::string& name) {
  if (name == "opencv_dnn" || name == "opencv" || name == "dnn") {
    return std::make_unique<OpenCVDnnBackend>();
  }

#ifdef LOONG_HAS_ONNXRUNTIME
  if (name == "onnxruntime" || name == "onnx" || name == "ort") {
    return std::make_unique<OnnxRuntimeBackend>();
  }
#else
  if (name == "onnxruntime" || name == "onnx" || name == "ort") {
    spdlog::warn(
        "BackendFactory: ONNX Runtime requested but not compiled in, "
        "falling back to opencv_dnn");
    return std::make_unique<OpenCVDnnBackend>();
  }
#endif

#ifdef LOONG_HAS_TENSORRT
  if (name == "tensorrt" || name == "trt") {
    return std::make_unique<TensorRTBackend>();
  }
#else
  if (name == "tensorrt" || name == "trt") {
    spdlog::warn(
        "BackendFactory: TensorRT requested but not compiled in, "
        "falling back to opencv_dnn");
    return std::make_unique<OpenCVDnnBackend>();
  }
#endif

  spdlog::warn("BackendFactory: unknown backend '{}', using opencv_dnn", name);
  return std::make_unique<OpenCVDnnBackend>();
}

}  // namespace loong::ai_engine
