// Copyright 2026 Loong AI NVR Project

#include "core/pipeline/channel_pipeline.h"

#include <atomic>

#include "gtest/gtest.h"

namespace loong {
namespace core {
namespace {

/// A mock pipeline stage that counts processed frames.
class MockStage : public PipelineStage {
 public:
  explicit MockStage(const std::string& name) : name_(name) {}

  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    ++process_count_;
    return PassToNext(std::move(frame));
  }

  void Shutdown() override { shutdown_called_ = true; }
  std::string Name() const override { return name_; }

  std::atomic<int> process_count_{0};
  bool shutdown_called_ = false;

 private:
  std::string name_;
};

TEST(ChannelPipeline, InitializeAndStartStop) {
  ChannelPipeline pipeline(1);
  auto stage = std::make_unique<MockStage>("test_stage");
  auto* stage_ptr = stage.get();

  StageConfig config;
  config.name = "test_stage";
  config.queue_capacity = 16;
  pipeline.AddStage(std::move(stage), config);

  EXPECT_TRUE(pipeline.Initialize());
  EXPECT_EQ(pipeline.GetState(), ChannelState::kConfigured);

  EXPECT_TRUE(pipeline.Start());
  EXPECT_EQ(pipeline.GetState(), ChannelState::kRunning);

  EXPECT_TRUE(pipeline.Stop());
  EXPECT_EQ(pipeline.GetState(), ChannelState::kStopped);
  EXPECT_TRUE(stage_ptr->shutdown_called_);
}

TEST(ChannelPipeline, ProcessFramesThroughStages) {
  ChannelPipeline pipeline(1);

  auto stage1 = std::make_unique<MockStage>("stage1");
  auto stage2 = std::make_unique<MockStage>("stage2");
  auto* s1 = stage1.get();
  auto* s2 = stage2.get();

  StageConfig config;
  config.queue_capacity = 16;

  config.name = "stage1";
  pipeline.AddStage(std::move(stage1), config);
  config.name = "stage2";
  pipeline.AddStage(std::move(stage2), config);

  EXPECT_TRUE(pipeline.Initialize());
  EXPECT_TRUE(pipeline.Start());

  // Push frames into the pipeline's input queue
  auto input_queue = pipeline.GetInputQueue();
  ASSERT_NE(input_queue, nullptr);

  constexpr int kFrameCount = 10;
  for (int i = 0; i < kFrameCount; ++i) {
    auto frame = std::make_shared<Frame>();
    frame->channel_id = 1;
    frame->pts = static_cast<int64_t>(i) * 40000;
    input_queue->Push(frame);
  }

  // Wait a bit for processing
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  EXPECT_TRUE(pipeline.Stop());

  // Both stages should have processed frames
  EXPECT_GT(s1->process_count_.load(), 0);
  EXPECT_GT(s2->process_count_.load(), 0);
}

}  // namespace
}  // namespace core
}  // namespace loong
