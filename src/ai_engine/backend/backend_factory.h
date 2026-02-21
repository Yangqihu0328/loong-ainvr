// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_BACKEND_BACKEND_FACTORY_H_
#define LOONG_AI_ENGINE_BACKEND_BACKEND_FACTORY_H_

#include "ai_engine/inference/inference_backend.h"

#include <memory>
#include <string>

namespace loong::ai_engine {

/// Factory for creating inference backends by name.
class BackendFactory {
 public:
  /// Create a backend by name: "opencv_dnn", "onnxruntime", "tensorrt"
  static std::unique_ptr<InferenceBackend> Create(const std::string& name);
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_BACKEND_BACKEND_FACTORY_H_
