// Copyright 2026 Loong AI NVR Project
// Benchmark: Pipeline throughput (single/multi-channel FPS)

#include "core/common/types.h"
#include "core/pipeline/channel_pipeline.h"
#include "core/pipeline/pipeline_stage.h"
#include "core/pipeline/stage_queue.h"

#include <atomic>
#include <benchmark/benchmark.h>
#include <memory>
#include <vector>

namespace loong::benchmark {
namespace {

class NoOpStage : public core::PipelineStage {
 public:
  bool Initialize(const StageConfig& /*config*/) override { return true; }
  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    return PassToNext(std::move(frame));
  }
  void Shutdown() override {}
  std::string Name() const override { return "NoOp"; }
};

class SimulatedAiStage : public core::PipelineStage {
 public:
  explicit SimulatedAiStage(int latency_us) : latency_us_(latency_us) {}

  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < latency_us_) {
    }
    return PassToNext(std::move(frame));
  }

  void Shutdown() override {}
  std::string Name() const override { return "SimulatedAI"; }

 private:
  int latency_us_;
};

class CounterStage : public core::PipelineStage {
 public:
  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> /*frame*/) override {
    count_.fetch_add(1, std::memory_order_relaxed);
    return true;
  }

  void Shutdown() override {}
  std::string Name() const override { return "Counter"; }

  int64_t Count() const { return count_.load(std::memory_order_relaxed); }
  void Reset() { count_.store(0, std::memory_order_relaxed); }

 private:
  std::atomic<int64_t> count_{0};
};

std::shared_ptr<Frame> MakeTestFrame(int channel_id, int width, int height) {
  auto frame = std::make_shared<Frame>();
  frame->channel_id = channel_id;
  frame->type = FrameType::kRaw;
  frame->info.width = width;
  frame->info.height = height;
  frame->info.stride = width * 3;
  frame->pts = std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();

  size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[size]());
  frame->data_size = size;
  return frame;
}

void BM_StageQueueThroughput(::benchmark::State& state) {
  core::StageQueue queue(static_cast<size_t>(state.range(0)));
  auto frame = MakeTestFrame(0, 640, 480);

  for (auto _ : state) {
    queue.Push(frame);
    auto result = queue.Pop(std::chrono::milliseconds(10));
    ::benchmark::DoNotOptimize(result);
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
}
BENCHMARK(BM_StageQueueThroughput)
    ->Arg(64)
    ->Arg(128)
    ->Arg(512)
    ->Unit(::benchmark::kMicrosecond);

void BM_PipelineNoOpStages(::benchmark::State& state) {
  int num_stages = static_cast<int>(state.range(0));
  int num_frames = 1000;

  for (auto _ : state) {
    state.PauseTiming();
    core::ChannelPipeline pipeline(1);
    auto counter = std::make_unique<CounterStage>();
    auto* counter_ptr = counter.get();

    for (int i = 0; i < num_stages; ++i) {
      StageConfig cfg;
      cfg.name = "noop_" + std::to_string(i);
      cfg.queue_capacity = 256;
      pipeline.AddStage(std::make_unique<NoOpStage>(), cfg);
    }

    StageConfig counter_cfg;
    counter_cfg.name = "counter";
    counter_cfg.queue_capacity = 256;
    pipeline.AddStage(std::move(counter), counter_cfg);
    pipeline.Initialize();
    pipeline.Start();
    state.ResumeTiming();

    auto input_queue = pipeline.GetInputQueue();
    for (int i = 0; i < num_frames; ++i) {
      input_queue->Push(MakeTestFrame(1, 640, 480));
    }

    while (counter_ptr->Count() < num_frames) {
      std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    state.PauseTiming();
    pipeline.Stop();
    state.ResumeTiming();
  }
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          num_frames);
  state.counters["frames"] = num_frames;
  state.counters["stages"] = static_cast<double>(state.range(0));
}
BENCHMARK(BM_PipelineNoOpStages)
    ->Arg(1)
    ->Arg(3)
    ->Arg(5)
    ->Unit(::benchmark::kMillisecond);

void BM_PipelineWithAiLatency(::benchmark::State& state) {
  int ai_latency_us = static_cast<int>(state.range(0));
  int num_frames = 200;

  for (auto _ : state) {
    state.PauseTiming();
    core::ChannelPipeline pipeline(1);
    auto counter = std::make_unique<CounterStage>();
    auto* counter_ptr = counter.get();

    StageConfig noop_cfg;
    noop_cfg.name = "decode_sim";
    noop_cfg.queue_capacity = 256;
    pipeline.AddStage(std::make_unique<NoOpStage>(), noop_cfg);

    StageConfig ai_cfg;
    ai_cfg.name = "ai_sim";
    ai_cfg.queue_capacity = 256;
    pipeline.AddStage(std::make_unique<SimulatedAiStage>(ai_latency_us),
                      ai_cfg);

    StageConfig counter_cfg;
    counter_cfg.name = "counter";
    counter_cfg.queue_capacity = 256;
    pipeline.AddStage(std::move(counter), counter_cfg);
    pipeline.Initialize();
    pipeline.Start();
    state.ResumeTiming();

    auto input_queue = pipeline.GetInputQueue();
    for (int i = 0; i < num_frames; ++i) {
      input_queue->Push(MakeTestFrame(1, 640, 480));
    }

    while (counter_ptr->Count() < num_frames) {
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }

    state.PauseTiming();
    pipeline.Stop();
    state.ResumeTiming();
  }

  double expected_fps = 1e6 / ai_latency_us;
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          num_frames);
  state.counters["ai_latency_us"] = ai_latency_us;
  state.counters["theoretical_fps"] = expected_fps;
}
BENCHMARK(BM_PipelineWithAiLatency)
    ->Arg(1000)
    ->Arg(5000)
    ->Arg(20000)
    ->Unit(::benchmark::kMillisecond);

void BM_MultiChannelPipeline(::benchmark::State& state) {
  int num_channels = static_cast<int>(state.range(0));
  int frames_per_channel = 100;

  for (auto _ : state) {
    state.PauseTiming();
    std::vector<std::unique_ptr<core::ChannelPipeline>> pipelines;
    std::vector<CounterStage*> counters;
    std::vector<std::shared_ptr<core::StageQueue>> input_queues;

    for (int ch = 0; ch < num_channels; ++ch) {
      auto pipeline = std::make_unique<core::ChannelPipeline>(ch);
      auto counter = std::make_unique<CounterStage>();
      counters.push_back(counter.get());

      StageConfig noop_cfg;
      noop_cfg.name = "noop";
      noop_cfg.queue_capacity = 256;
      pipeline->AddStage(std::make_unique<NoOpStage>(), noop_cfg);

      StageConfig counter_cfg;
      counter_cfg.name = "counter";
      counter_cfg.queue_capacity = 256;
      pipeline->AddStage(std::move(counter), counter_cfg);
      pipeline->Initialize();
      pipeline->Start();
      input_queues.push_back(pipeline->GetInputQueue());
      pipelines.push_back(std::move(pipeline));
    }
    state.ResumeTiming();

    for (int ch = 0; ch < num_channels; ++ch) {
      for (int f = 0; f < frames_per_channel; ++f) {
        input_queues[static_cast<size_t>(ch)]->Push(
            MakeTestFrame(ch, 640, 480));
      }
    }

    bool all_done = false;
    while (!all_done) {
      all_done = true;
      for (int ch = 0; ch < num_channels; ++ch) {
        if (counters[static_cast<size_t>(ch)]->Count() < frames_per_channel) {
          all_done = false;
          break;
        }
      }
      if (!all_done) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
      }
    }

    state.PauseTiming();
    for (auto& p : pipelines) p->Stop();
    state.ResumeTiming();
  }

  int total = num_channels * frames_per_channel;
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) * total);
  state.counters["channels"] = num_channels;
  state.counters["total_frames"] = total;
}
BENCHMARK(BM_MultiChannelPipeline)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(32)
    ->Unit(::benchmark::kMillisecond);

}  // namespace
}  // namespace loong::benchmark

BENCHMARK_MAIN();
