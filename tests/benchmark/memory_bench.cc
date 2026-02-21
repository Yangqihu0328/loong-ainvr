// Copyright 2026 Loong AI NVR Project
// Benchmark: Memory pool allocation / Frame creation overhead

#include <benchmark/benchmark.h>

#include <cstring>
#include <memory>
#include <vector>

#include "core/common/types.h"
#include "core/memory_pool/frame_buffer_pool.h"

namespace loong::benchmark {
namespace {

void BM_FrameBufferPoolAcquireRelease(::benchmark::State& state) {
  size_t buffer_size = static_cast<size_t>(state.range(0));
  size_t pool_size = 32;

  core::FrameBufferPool pool(pool_size, buffer_size);

  for (auto _ : state) {
    auto buf = pool.Acquire();
    ::benchmark::DoNotOptimize(buf);
    buf.reset();
  }
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) *
      static_cast<int64_t>(buffer_size));
}
BENCHMARK(BM_FrameBufferPoolAcquireRelease)
    ->Arg(640 * 480 * 3)
    ->Arg(1280 * 720 * 3)
    ->Arg(1920 * 1080 * 3)
    ->Arg(3840 * 2160 * 3)
    ->Unit(::benchmark::kNanosecond);

void BM_FrameBufferPoolWithWrite(::benchmark::State& state) {
  size_t buffer_size = static_cast<size_t>(state.range(0));
  size_t pool_size = 16;

  core::FrameBufferPool pool(pool_size, buffer_size);

  for (auto _ : state) {
    auto buf = pool.Acquire();
    if (buf) {
      std::memset(buf->Data(), 0x42, buf->Size());
      buf->SetActualSize(buffer_size);
    }
    ::benchmark::DoNotOptimize(buf);
    buf.reset();
  }
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) *
      static_cast<int64_t>(buffer_size));
}
BENCHMARK(BM_FrameBufferPoolWithWrite)
    ->Arg(640 * 480 * 3)
    ->Arg(1920 * 1080 * 3)
    ->Unit(::benchmark::kMicrosecond);

void BM_RawAllocation(::benchmark::State& state) {
  size_t buffer_size = static_cast<size_t>(state.range(0));

  for (auto _ : state) {
    auto ptr = std::make_unique<uint8_t[]>(buffer_size);
    ::benchmark::DoNotOptimize(ptr.get());
  }
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) *
      static_cast<int64_t>(buffer_size));
}
BENCHMARK(BM_RawAllocation)
    ->Arg(640 * 480 * 3)
    ->Arg(1920 * 1080 * 3)
    ->Arg(3840 * 2160 * 3)
    ->Unit(::benchmark::kNanosecond);

void BM_FrameBufferPoolPressure(::benchmark::State& state) {
  int hold_count = static_cast<int>(state.range(0));
  size_t buffer_size = 1920 * 1080 * 3;
  size_t pool_size = static_cast<size_t>(hold_count + 4);

  core::FrameBufferPool pool(pool_size, buffer_size);

  std::vector<std::shared_ptr<core::FrameBuffer>> held;
  held.reserve(static_cast<size_t>(hold_count));
  for (int i = 0; i < hold_count; ++i) {
    held.push_back(pool.Acquire());
  }

  for (auto _ : state) {
    auto buf = pool.Acquire();
    ::benchmark::DoNotOptimize(buf);
    buf.reset();
  }

  held.clear();
  state.counters["pool_size"] = static_cast<double>(pool_size);
  state.counters["held_buffers"] = hold_count;
}
BENCHMARK(BM_FrameBufferPoolPressure)
    ->Arg(0)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(::benchmark::kNanosecond);

void BM_FrameCreation(::benchmark::State& state) {
  int width = static_cast<int>(state.range(0));
  int height = width * 9 / 16;

  for (auto _ : state) {
    auto frame = std::make_shared<Frame>();
    frame->channel_id = 1;
    frame->type = FrameType::kRaw;
    frame->info.width = width;
    frame->info.height = height;
    frame->info.stride = width * 3;

    size_t size = static_cast<size_t>(width) *
                  static_cast<size_t>(height) * 3;
    frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[size]);
    frame->data_size = size;
    ::benchmark::DoNotOptimize(frame);
  }
  state.SetBytesProcessed(
      static_cast<int64_t>(state.iterations()) *
      static_cast<int64_t>(width) * height * 3);
}
BENCHMARK(BM_FrameCreation)
    ->Arg(640)
    ->Arg(1280)
    ->Arg(1920)
    ->Arg(3840)
    ->Unit(::benchmark::kMicrosecond);

void BM_MemoryScaling(::benchmark::State& state) {
  int num_channels = static_cast<int>(state.range(0));
  size_t buffer_size = 1920 * 1080 * 3;
  size_t buffers_per_channel = 8;

  for (auto _ : state) {
    std::vector<std::unique_ptr<core::FrameBufferPool>> pools;
    pools.reserve(static_cast<size_t>(num_channels));
    for (int i = 0; i < num_channels; ++i) {
      pools.push_back(
          std::make_unique<core::FrameBufferPool>(
              buffers_per_channel, buffer_size));
    }

    std::vector<std::shared_ptr<core::FrameBuffer>> buffers;
    buffers.reserve(static_cast<size_t>(num_channels));
    for (auto& pool : pools) {
      buffers.push_back(pool->Acquire());
    }
    ::benchmark::DoNotOptimize(buffers);

    buffers.clear();
    pools.clear();
  }

  double total_mb = static_cast<double>(num_channels) *
                    static_cast<double>(buffers_per_channel) *
                    static_cast<double>(buffer_size) / (1024.0 * 1024.0);
  state.counters["channels"] = num_channels;
  state.counters["total_memory_mb"] = total_mb;
}
BENCHMARK(BM_MemoryScaling)
    ->Arg(1)
    ->Arg(4)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Unit(::benchmark::kMillisecond);

}  // namespace
}  // namespace loong::benchmark

BENCHMARK_MAIN();
