// Copyright 2026 Loong AI NVR Project

#include "core/pipeline/stage_queue.h"

#include "gtest/gtest.h"

#include <thread>

namespace loong {
namespace core {
namespace {

std::shared_ptr<Frame> MakeFrame(int channel_id, int64_t pts,
                                 bool keyframe = false) {
  auto f = std::make_shared<Frame>();
  f->channel_id = channel_id;
  f->pts = pts;
  f->info.is_keyframe = keyframe;
  return f;
}

TEST(StageQueue, PushAndPop) {
  StageQueue queue(10);
  auto frame = MakeFrame(1, 100);
  EXPECT_TRUE(queue.Push(frame));
  EXPECT_EQ(queue.Size(), 1u);

  auto result = queue.Pop(std::chrono::milliseconds(100));
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result.value()->channel_id, 1);
  EXPECT_EQ(result.value()->pts, 100);
}

TEST(StageQueue, BackpressureDropsOldest) {
  StageQueue queue(2);
  queue.Push(MakeFrame(1, 100));
  queue.Push(MakeFrame(1, 200));

  // Queue is full, pushing a new frame should drop oldest of ch 1
  queue.Push(MakeFrame(1, 300));
  EXPECT_EQ(queue.Size(), 2u);
  EXPECT_EQ(queue.DroppedCount(), 1);

  // The oldest (pts=100) should have been dropped
  auto f1 = queue.Pop(std::chrono::milliseconds(10));
  ASSERT_TRUE(f1.has_value());
  EXPECT_EQ(f1.value()->pts, 200);
}

TEST(StageQueue, KeyframesNotDropped) {
  StageQueue queue(2);
  queue.Push(MakeFrame(1, 100, true));  // Keyframe
  queue.Push(MakeFrame(1, 200));

  // Push a 3rd frame — should drop pts=200 (non-keyframe), not pts=100
  queue.Push(MakeFrame(1, 300));
  EXPECT_EQ(queue.Size(), 2u);

  auto f1 = queue.Pop(std::chrono::milliseconds(10));
  ASSERT_TRUE(f1.has_value());
  EXPECT_EQ(f1.value()->pts, 100);  // Keyframe preserved
}

TEST(StageQueue, PopBatch) {
  StageQueue queue(16);
  for (int i = 0; i < 5; ++i) {
    queue.Push(MakeFrame(i, static_cast<int64_t>(i) * 100));
  }

  auto batch = queue.PopBatch(3, std::chrono::milliseconds(10));
  EXPECT_EQ(batch.size(), 3u);
  EXPECT_EQ(queue.Size(), 2u);
}

TEST(StageQueue, PopTimeoutReturnsNullopt) {
  StageQueue queue(10);
  auto result = queue.Pop(std::chrono::milliseconds(10));
  EXPECT_FALSE(result.has_value());
}

TEST(StageQueue, ShutdownUnblocksPop) {
  StageQueue queue(10);

  std::thread t([&queue] {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    queue.Shutdown();
  });

  auto result = queue.Pop(std::chrono::milliseconds(5000));
  EXPECT_FALSE(result.has_value());
  EXPECT_TRUE(queue.IsShutdown());

  t.join();
}

}  // namespace
}  // namespace core
}  // namespace loong
