// Copyright 2026 Loong AI NVR Project

#include "ai_engine/model_manager/model_manager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <thread>

#include "ai_engine/backend/backend_factory.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "spdlog/spdlog.h"

namespace fs = std::filesystem;

namespace loong::ai_engine {

ModelManager::ModelManager() = default;

// ========== Registration ==========

bool ModelManager::RegisterModel(const ModelInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (models_.count(info.name)) {
    spdlog::warn("ModelManager: model '{}' already registered", info.name);
    return false;
  }
  models_[info.name] = info;
  spdlog::info("ModelManager: registered model '{}' (family={}, backend={})",
               info.name, info.family, info.backend);
  return true;
}

bool ModelManager::UnregisterModel(const std::string& model_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = models_.find(model_name);
  if (it == models_.end()) {
    spdlog::warn("ModelManager: model '{}' not found", model_name);
    return false;
  }
  models_.erase(it);
  spdlog::info("ModelManager: unregistered model '{}'", model_name);
  return true;
}

// ========== Directory Scanning ==========

int ModelManager::ScanDirectory(const std::string& directory_path) {
  int count = 0;

  std::error_code ec;
  if (!fs::is_directory(directory_path, ec)) {
    spdlog::warn("ModelManager: '{}' is not a directory", directory_path);
    return 0;
  }

  for (const auto& entry : fs::directory_iterator(directory_path, ec)) {
    if (!entry.is_regular_file()) continue;

    std::string ext = entry.path().extension().string();
    // Only consider known model file extensions.
    if (ext != ".onnx" && ext != ".engine" && ext != ".trt" &&
        ext != ".xml") {
      continue;
    }

    std::string filename = entry.path().filename().string();
    std::string stem = entry.path().stem().string();
    std::string full_path = entry.path().string();

    // Skip if already registered.
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (models_.count(stem)) continue;
    }

    ModelInfo info;
    info.name = stem;
    info.model_path = full_path;
    info.family = DetectFamily(filename);
    info.backend = DetectBackend(filename);
    info.input_shape = {1, 3, 640, 640};  // Default YOLO input

    if (info.family.empty()) {
      spdlog::debug("ModelManager: skipping '{}' — unknown family",
                    filename);
      continue;
    }

    if (RegisterModel(info)) {
      ++count;
    }
  }

  spdlog::info("ModelManager: scanned '{}', found {} new models",
               directory_path, count);
  return count;
}

// ========== Engine Creation ==========

std::shared_ptr<InferenceEngine> ModelManager::CreateEngine(
    const std::string& model_name) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = models_.find(model_name);
  if (it == models_.end()) {
    spdlog::error("ModelManager: model '{}' not registered", model_name);
    return nullptr;
  }
  return CreateEngine(model_name, it->second.backend);
}

std::shared_ptr<InferenceEngine> ModelManager::CreateEngine(
    const std::string& model_name, const std::string& backend_name) {
  ModelInfo info;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = models_.find(model_name);
    if (it == models_.end()) {
      spdlog::error("ModelManager: model '{}' not registered", model_name);
      return nullptr;
    }
    info = it->second;
  }

  // Create the inference backend.
  std::string effective_backend =
      backend_name.empty() ? info.backend : backend_name;
  auto backend = BackendFactory::Create(effective_backend);
  if (!backend) {
    spdlog::error("ModelManager: failed to create backend '{}'",
                  effective_backend);
    return nullptr;
  }

  // Load the model into the backend.
  BackendConfig bc;
  bc.num_threads = static_cast<int>(std::thread::hardware_concurrency());
  if (bc.num_threads <= 0) bc.num_threads = 4;
  if (!backend->LoadModel(info.model_path, bc)) {
    spdlog::error("ModelManager: failed to load model '{}' into backend '{}'",
                  info.name, effective_backend);
    return nullptr;
  }

  // Detect model input spatial size from backend metadata.
  int input_size = 640;
  auto model_shape = backend->GetInputShape();
  if (model_shape.size() >= 4 && model_shape[2] > 0 && model_shape[3] > 0) {
    input_size = static_cast<int>(model_shape[2]);
    spdlog::info("ModelManager: detected model input size {}x{} from metadata",
                 model_shape[2], model_shape[3]);
  }

  // Create the YOLO adapter with detected input size.
  auto adapter = YoloAdapterFactory::Create(info.family, input_size);
  if (!adapter) {
    spdlog::error("ModelManager: failed to create adapter for family '{}'",
                  info.family);
    return nullptr;
  }

  auto engine = std::make_shared<InferenceEngine>(
      std::move(backend), std::move(adapter));

  // Mark as loaded.
  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = models_.find(model_name);
    if (it != models_.end()) {
      it->second.loaded = true;
    }
  }

  spdlog::info("ModelManager: created engine for model '{}' "
               "(family={}, backend={})",
               info.name, info.family, effective_backend);
  return engine;
}

// ========== Queries ==========

const ModelInfo* ModelManager::GetModelInfo(
    const std::string& model_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = models_.find(model_name);
  if (it == models_.end()) return nullptr;
  return &it->second;
}

std::vector<ModelInfo> ModelManager::ListModels() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ModelInfo> result;
  result.reserve(models_.size());
  for (const auto& [name, info] : models_) {
    result.push_back(info);
  }
  return result;
}

bool ModelManager::HasModel(const std::string& model_name) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return models_.count(model_name) > 0;
}

size_t ModelManager::ModelCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return models_.size();
}

// ========== Class Names ==========

std::vector<std::string> ModelManager::LoadClassNames(
    const std::string& file_path) {
  std::vector<std::string> names;
  std::ifstream f(file_path);
  if (!f.is_open()) {
    spdlog::warn("ModelManager: cannot open class names file '{}'",
                 file_path);
    return names;
  }

  std::string line;
  while (std::getline(f, line)) {
    // Trim trailing whitespace.
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
      line.pop_back();
    }
    if (!line.empty()) {
      names.push_back(line);
    }
  }

  spdlog::info("ModelManager: loaded {} class names from '{}'",
               names.size(), file_path);
  return names;
}

// ========== Auto-detection Helpers ==========

std::string ModelManager::DetectFamily(const std::string& filename) {
  // Convert to lowercase for matching.
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // Match YOLO version patterns.
  if (lower.find("yolov11") != std::string::npos) return "yolov11";
  if (lower.find("yolov10") != std::string::npos) return "yolov10";
  if (lower.find("yolov9") != std::string::npos) return "yolov9";
  if (lower.find("yolov8") != std::string::npos) return "yolov8";
  if (lower.find("yolov7") != std::string::npos) return "yolov7";
  if (lower.find("yolov5") != std::string::npos) return "yolov5";
  if (lower.find("yolo") != std::string::npos) return "yolov8";  // Default

  return "";  // Unknown
}

std::string ModelManager::DetectBackend(const std::string& filename) {
  std::string lower = filename;
  std::transform(lower.begin(), lower.end(), lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (lower.find(".onnx") != std::string::npos) return "onnxruntime";
  if (lower.find(".engine") != std::string::npos) return "tensorrt";
  if (lower.find(".trt") != std::string::npos) return "tensorrt";
  if (lower.find(".xml") != std::string::npos) return "opencv_dnn";

  return "opencv_dnn";  // Fallback
}

}  // namespace loong::ai_engine
