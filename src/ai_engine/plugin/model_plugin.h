// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_PLUGIN_MODEL_PLUGIN_H_
#define LOONG_AI_ENGINE_PLUGIN_MODEL_PLUGIN_H_

#include <cstdint>
#include <string>
#include <vector>

#include "ai_engine/inference/inference_backend.h"
#include "core/common/types.h"

namespace loong::ai_engine {

/// Version of the plugin API (for compatibility checks).
constexpr int kPluginApiVersion = 1;

/// Metadata about a model plugin.
struct PluginInfo {
  std::string name;
  std::string version;
  std::string author;
  std::string description;
  std::string model_family;    // e.g. "yolov8_custom"
  int api_version = 0;
};

/// Abstract interface that every model plugin must implement.
/// Plugins are compiled as shared libraries (.so) and loaded at runtime.
class ModelPlugin {
 public:
  virtual ~ModelPlugin() = default;

  /// Return plugin metadata.
  virtual PluginInfo GetInfo() const = 0;

  /// Initialize the plugin (called once after loading).
  virtual bool Initialize(const std::string& config_json) = 0;

  /// Preprocess raw image data into model input tensor.
  virtual bool PreProcess(const uint8_t* image_data, int width, int height,
                          std::vector<float>& input_data,
                          TensorShape& input_shape) = 0;

  /// Postprocess model output into detections.
  virtual bool PostProcess(const std::vector<float>& output_data,
                           const TensorShape& output_shape,
                           int original_width, int original_height,
                           float confidence_threshold, float nms_threshold,
                           std::vector<Detection>& detections) = 0;

  /// Get expected input tensor shape.
  virtual TensorShape InputShape() const = 0;

  /// Shutdown the plugin (called before unloading).
  virtual void Shutdown() = 0;
};

}  // namespace loong::ai_engine

/// C entry points that plugin .so files must export.
extern "C" {
using CreatePluginFn = loong::ai_engine::ModelPlugin* (*)();
using DestroyPluginFn = void (*)(loong::ai_engine::ModelPlugin*);
}

/// Macro to define plugin entry points in a .so file.
#define LOONG_DEFINE_MODEL_PLUGIN(PluginClass)                    \
  extern "C" {                                                     \
  loong::ai_engine::ModelPlugin* loong_create_plugin() {          \
    return new PluginClass();                                      \
  }                                                                \
  void loong_destroy_plugin(loong::ai_engine::ModelPlugin* p) {   \
    delete p;                                                      \
  }                                                                \
  }

#endif  // LOONG_AI_ENGINE_PLUGIN_MODEL_PLUGIN_H_
