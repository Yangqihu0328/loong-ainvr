// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_MODEL_MANAGER_MODEL_MANAGER_H_
#define LOONG_AI_ENGINE_MODEL_MANAGER_MODEL_MANAGER_H_

#include "ai_engine/inference/inference_backend.h"
#include "ai_engine/inference/inference_engine.h"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loong::ai_engine {

/// Metadata describing a registered model.
struct ModelInfo {
  std::string name;         // Unique model name, e.g. "yolov8n-640"
  std::string family;       // YOLO family, e.g. "yolov8"
  std::string model_path;   // Filesystem path to model file (.onnx / .engine)
  std::string backend;      // Preferred backend, e.g. "onnxruntime"
  TensorShape input_shape;  // Expected input shape, e.g. {1, 3, 640, 640}
  std::vector<std::string> class_names;  // COCO / custom class labels
  bool loaded = false;  // Whether a backend has loaded this model
};

/// Manages model registration, lifecycle, and InferenceEngine creation.
///
/// The ModelManager is the single source of truth for which AI models are
/// available in the system. It supports:
/// - Registering / unregistering model metadata.
/// - Scanning a directory for model files.
/// - Creating InferenceEngine instances from registered models.
/// - Model hot-swap: atomically replace the model an engine is using.
/// - Listing and querying available models.
class ModelManager {
 public:
  ModelManager();
  ~ModelManager() = default;

  /// Register a model with explicit metadata.
  /// Returns false if a model with the same name already exists.
  bool RegisterModel(const ModelInfo& info);

  /// Unregister a model by name.
  bool UnregisterModel(const std::string& model_name);

  /// Scan a directory for model files (.onnx, .engine, .trt) and
  /// register them with auto-detected family based on filename.
  /// Returns the number of newly registered models.
  int ScanDirectory(const std::string& directory_path);

  /// Create a new InferenceEngine for the given model name.
  /// Internally selects the adapter from the model family and
  /// creates the backend from model info.
  /// Returns nullptr if the model is not registered or creation fails.
  std::shared_ptr<InferenceEngine> CreateEngine(const std::string& model_name);

  /// Create a new InferenceEngine with an explicit backend override.
  std::shared_ptr<InferenceEngine> CreateEngine(
      const std::string& model_name, const std::string& backend_name);

  /// Get info for a specific model.
  /// Returns nullptr if not found.
  const ModelInfo* GetModelInfo(const std::string& model_name) const;

  /// List all registered models.
  std::vector<ModelInfo> ListModels() const;

  /// Check if a model is registered.
  bool HasModel(const std::string& model_name) const;

  /// Get the number of registered models.
  size_t ModelCount() const;

  /// Load COCO class names from a text file (one class per line).
  /// Returns the loaded class name list.
  static std::vector<std::string> LoadClassNames(const std::string& file_path);

  // Non-copyable
  ModelManager(const ModelManager&) = delete;
  ModelManager& operator=(const ModelManager&) = delete;

 private:
  /// Auto-detect YOLO family from a model filename.
  /// e.g. "yolov8n.onnx" → "yolov8"
  static std::string DetectFamily(const std::string& filename);

  /// Auto-detect preferred backend from model file extension.
  /// .onnx → "onnxruntime", .engine/.trt → "tensorrt"
  static std::string DetectBackend(const std::string& filename);

  mutable std::mutex mutex_;
  std::unordered_map<std::string, ModelInfo> models_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_MODEL_MANAGER_MODEL_MANAGER_H_
