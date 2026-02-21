// Copyright 2026 Loong AI NVR Project

#include "core/thread_pool/thread_pool.h"

#include <atomic>
#include <chrono>
#include <vector>

#include "gtest/gtest.h"

namespace loong {
namespace core {
namespace {

TEST(ThreadPool, WorkerCountIsNonZero) {
  auto& pool = ThreadPool::Instance();
  EXPECT_GT(pool.WorkerCount(), 0u);
}

TEST(ThreadPool, SubmitAndGetResult) {
  auto& pool = ThreadPool::Instance();
  auto future = pool.Submit([] { return 42; });
  EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPool, ConcurrentTasks) {
  auto& pool = ThreadPool::Instance();
  std::atomic<int> counter{0};
  constexpr int kTaskCount = 100;

  std::vector<std::future<void>> futures;
  futures.reserve(kTaskCount);

  for (int i = 0; i < kTaskCount; ++i) {
    futures.push_back(pool.Submit([&counter] {
      ++counter;
    }));
  }

  for (auto& f : futures) {
    f.get();
  }

  EXPECT_EQ(counter.load(), kTaskCount);
}

}  // namespace
}  // namespace core
}  // namespace loong
