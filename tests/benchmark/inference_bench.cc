// Copyright 2026 Loong AI NVR Project
// Benchmark: AI inference latency (single / batch, mock backend)

#include <benchmark/benchmark.h>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include "ai_engine/inference/inference_backend.h"
#include "ai_engine/inference/inference_engine.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "core/common/types.h"

namespace loong::benchmark {
namespace {

/// Mock inference backend that simulates configurable compute latency.
class MockBackend : public ai_engine::InferenceBackend {
 public:
  explicit MockBackend(int latency_us) : latency_us_(latency_us) {}

  bool LoadModel(const std::string& /*model_path*/,
                 const ai_engine::BackendConfig& /*config*/) override {
    loaded_ = true;
    return true;
  }

  bool RunInference(const std::vector<float>& input_data,
                    const ai_engine::TensorShape& input_shape,
                    std::vector<float>& output_data,
                    ai_engine::TensorShape& output_shape) override {
    if (!loaded_) return false;

    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < latency_us_) {
    }

    int batch = input_shape.empty() ? 1 : static_cast<int>(input_shape[0]);
    int num_dets = 10;
    output_shape = {batch, num_dets, 6};
    output_data.resize(static_cast<size_t>(batch * num_dets * 6));

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0F, 1.0F);
    for (auto& v : output_data) {
      v = dist(rng);
    }
    return true;
  }

  bool IsLoaded() const override { return loaded_; }
  bool SupportsBatch() const override { return true; }
  int MaxBatchSize() const override { return 32; }
  void Unload() override { loaded_ = false; }
  std::string Name() const override { return "MockBackend"; }

 private:
  int latency_us_;
  bool loaded_ = false;
};

/// Mock YOLO adapter for preprocessing/postprocessing benchmarks.
class MockYoloAdapter : public ai_engine::YoloModelAdapter {
 public:
  bool PreProcess(const uint8_t* /*image_data*/, int width, int height,
                  std::vector<float>& output,
                  ai_engine::TensorShape& shape) override {
    shape = {1, 3, 640, 640};
    output.resize(1 * 3 * 640 * 640, 0.5F);
    return true;
  }

  bool PreProcessBatch(const std::vector<const uint8_t*>& /*images*/,
                       const std::vector<int>& /*widths*/,
                       const std::vector<int>& /*heights*/,
                       std::vector<float>& output,
                       ai_engine::TensorShape& shape) override {
    int batch = static_cast<int>(shape[0]);
    shape = {batch, 3, 640, 640};
    output.resize(static_cast<size_t>(batch) * 3 * 640 * 640, 0.5F);
    return true;
  }

  bool PostProcess(const std::vector<float>& raw_output,
                   const ai_engine::TensorShape& /*shape*/,
                   int /*orig_width*/, int /*orig_height*/,
                   float conf_threshold, float /*nms_threshold*/,
                   std::vector<Detection>& detections) override {
    detections.clear();
    for (size_t i = 0; i + 5 < raw_output.size(); i += 6) {
      if (raw_output[i + 4] >= conf_threshold) {
        Detection d;
        d.x1 = raw_output[i];
        d.y1 = raw_output[i + 1];
        d.x2 = raw_output[i + 2];
        d.y2 = raw_output[i + 3];
        d.confidence = raw_output[i + 4];
        d.class_id = static_cast<int>(raw_output[i + 5]);
        detections.push_back(d);
      }
    }
    return true;
  }

  bool PostProcessBatch(
      const std::vector<float>& raw_output,
      const ai_engine::TensorShape& shape,
      const std::vector<int>& /*orig_widths*/,
      const std::vector<int>& /*orig_heights*/,
      float conf_threshold, float /*nms_threshold*/,
      std::vector<std::vector<Detection>>& batch_detections) override {
    int batch = static_cast<int>(shape[0]);
    int num_dets = static_cast<int>(shape[1]);
    int det_size = static_cast<int>(shape[2]);
    batch_detections.resize(static_cast<size_t>(batch));

    for (int b = 0; b < batch; ++b) {
      batch_detections[static_cast<size_t>(b)].clear();
      size_t offset = static_cast<size_t>(b) *
                      static_cast<size_t>(num_dets) *
                      static_cast<size_t>(det_size);
      for (int d = 0; d < num_dets; ++d) {
        size_t idx = offset + static_cast<size_t>(d) *
                              static_cast<size_t>(det_size);
        if (idx + 5 < raw_output.size() &&
            raw_output[idx + 4] >= conf_threshold) {
          Detection det;
          det.x1 = raw_output[idx];
          det.y1 = raw_output[idx + 1];
          det.x2 = raw_output[idx + 2];
          det.y2 = raw_output[idx + 3];
          det.confidence = raw_output[idx + 4];
          det.class_id = static_cast<int>(raw_output[idx + 5]);
          batch_detections[static_cast<size_t>(b)].push_back(det);
        }
      }
    }
    return true;
  }

  std::string ModelFamily() const override { return "YOLOv8"; }
  ai_engine::TensorShape InputShape() const override {
    return {1, 3, 640, 640};
  }
};

std::unique_ptr<ai_engine::InferenceEngine> MakeEngine(int latency_us) {
  auto backend = std::make_unique<MockBackend>(latency_us);
  auto adapter = std::make_unique<MockYoloAdapter>();

  ai_engine::BackendConfig cfg;
  backend->LoadModel("mock_model.onnx", cfg);

  return std::make_unique<ai_engine::InferenceEngine>(
      std::move(backend), std::move(adapter));
}

std::vector<uint8_t> MakeTestImage(int width, int height) {
  std::vector<uint8_t> img(static_cast<size_t>(width) *
                           static_cast<size_t>(height) * 3);
  std::mt19937 rng(123);
  std::uniform_int_distribution<int> dist(0, 255);
  for (auto& pixel : img) {
    pixel = static_cast<uint8_t>(dist(rng));
  }
  return img;
}

void BM_InferenceSingle(::benchmark::State& state) {
  int latency_us = static_cast<int>(state.range(0));
  auto engine = MakeEngine(latency_us);

  auto img = MakeTestImage(1920, 1080);
  std::vector<Detection> detections;

  for (auto _ : state) {
    engine->Infer(img.data(), 1920, 1080, 0.5F, detections);
    ::benchmark::DoNotOptimize(detections);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
  state.counters["latency_us"] = latency_us;
  state.counters["detections"] = static_cast<double>(detections.size());
}
BENCHMARK(BM_InferenceSingle)
    ->Arg(500)
    ->Arg(2000)
    ->Arg(10000)
    ->Arg(50000)
    ->Unit(::benchmark::kMicrosecond);

void BM_InferenceBatch(::benchmark::State& state) {
  int batch_size = static_cast<int>(state.range(0));
  int latency_us = 5000;
  auto engine = MakeEngine(latency_us);

  auto img = MakeTestImage(1920, 1080);
  std::vector<const uint8_t*> images(static_cast<size_t>(batch_size),
                                     img.data());
  std::vector<int> widths(static_cast<size_t>(batch_size), 1920);
  std::vector<int> heights(static_cast<size_t>(batch_size), 1080);
  std::vector<std::vector<Detection>> batch_dets;

  for (auto _ : state) {
    engine->InferBatch(images, widths, heights, 0.5F, batch_dets);
    ::benchmark::DoNotOptimize(batch_dets);
  }

  state.SetItemsProcessed(
      static_cast<int64_t>(state.iterations()) * batch_size);
  state.counters["batch_size"] = batch_size;
}
BENCHMARK(BM_InferenceBatch)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(::benchmark::kMicrosecond);

void BM_PreProcess(::benchmark::State& state) {
  int width = static_cast<int>(state.range(0));
  int height = width * 9 / 16;

  MockYoloAdapter adapter;
  auto img = MakeTestImage(width, height);
  std::vector<float> output;
  ai_engine::TensorShape shape;

  for (auto _ : state) {
    adapter.PreProcess(img.data(), width, height, output, shape);
    ::benchmark::DoNotOptimize(output);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
  state.counters["resolution"] =
      static_cast<double>(width) * height / 1e6;
}
BENCHMARK(BM_PreProcess)
    ->Arg(640)
    ->Arg(1280)
    ->Arg(1920)
    ->Arg(3840)
    ->Unit(::benchmark::kMicrosecond);

void BM_PostProcess(::benchmark::State& state) {
  int num_raw_dets = static_cast<int>(state.range(0));

  MockYoloAdapter adapter;
  ai_engine::TensorShape shape = {1, num_raw_dets, 6};
  std::vector<float> raw_output(static_cast<size_t>(num_raw_dets) * 6);

  std::mt19937 rng(42);
  std::uniform_real_distribution<float> dist(0.0F, 1.0F);
  for (auto& v : raw_output) {
    v = dist(rng);
  }

  std::vector<Detection> detections;

  for (auto _ : state) {
    adapter.PostProcess(raw_output, shape, 1920, 1080, 0.5F, 0.45F,
                        detections);
    ::benchmark::DoNotOptimize(detections);
  }

  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
  state.counters["raw_detections"] = num_raw_dets;
  state.counters["filtered_detections"] =
      static_cast<double>(detections.size());
}
BENCHMARK(BM_PostProcess)
    ->Arg(100)
    ->Arg(1000)
    ->Arg(8400)
    ->Arg(25200)
    ->Unit(::benchmark::kMicrosecond);

void BM_ModelHotSwap(::benchmark::State& state) {
  auto engine = MakeEngine(1000);

  for (auto _ : state) {
    auto new_adapter = std::make_unique<MockYoloAdapter>();
    ai_engine::BackendConfig cfg;
    engine->SwitchModel("new_model.onnx", std::move(new_adapter), cfg);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_ModelHotSwap)->Unit(::benchmark::kMicrosecond);

}  // namespace
}  // namespace loong::benchmark

BENCHMARK_MAIN();
