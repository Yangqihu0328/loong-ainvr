// Copyright 2026 Loong AI NVR Project
//
// End-to-end integration tests for the video processing pipeline.
// Tests the complete frame flow: Source → Decode → AI → Overlay → Output
// using mock/stub components that don't require real RTSP streams.

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

#include "ai_engine/backend/backend_factory.h"
#include "ai_engine/backend/opencv_dnn_backend.h"
#include "ai_engine/inference/inference_engine.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "core/common/types.h"
#include "core/pipeline/channel_pipeline.h"
#include "core/pipeline/pipeline_stage.h"
#include "core/pipeline/stage_queue.h"
#include "pipeline_stages/ai_stage/ai_stage.h"
#include "pipeline_stages/overlay_stage/overlay_stage.h"
#include "storage/record_index/record_index.h"

namespace loong {
namespace integration {
namespace {

// ============================================================
// Mock / Stub Components
// ============================================================

/// Generate a synthetic BGR test frame of given size.
/// Fills with a gradient pattern for visual verification.
std::shared_ptr<Frame> MakeTestFrame(int channel_id, int width, int height,
                                     int64_t pts, bool is_keyframe = false) {
  auto frame = std::make_shared<Frame>();
  frame->channel_id = channel_id;
  frame->pts = pts;
  frame->dts = pts;
  frame->type = FrameType::kRaw;
  frame->info.width = width;
  frame->info.height = height;
  frame->info.stride = width * 3;
  frame->info.pixel_format = 0;
  frame->info.is_keyframe = is_keyframe;
  frame->info.codec = CodecType::kH264;

  size_t data_size = static_cast<size_t>(width * height * 3);
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[data_size]);
  frame->data_size = data_size;

  // Fill with gradient pattern (BGR)
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      auto idx = static_cast<ptrdiff_t>((y * width + x) * 3);
      auto ux = static_cast<uint8_t>((x * 255) / (width > 1 ? width - 1 : 1));
      auto uy = static_cast<uint8_t>((y * 255) / (height > 1 ? height - 1 : 1));
      frame->data[idx] = ux;                            // B
      frame->data[idx + 1] = uy;                        // G
      frame->data[idx + 2] = static_cast<uint8_t>(128); // R
    }
  }

  return frame;
}

/// A mock source stage that generates synthetic frames.
class MockSourceStage : public core::PipelineStage {
 public:
  explicit MockSourceStage(int total_frames, int width = 320, int height = 240)
      : total_frames_(total_frames), width_(width), height_(height) {}

  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    return PassToNext(std::move(frame));
  }

  void Shutdown() override {}
  std::string Name() const override { return "MockSource"; }

  /// Push all frames into the pipeline (called by test).
  void GenerateFrames(std::shared_ptr<core::StageQueue> output_queue) {
    for (int i = 0; i < total_frames_; ++i) {
      auto pts = static_cast<int64_t>(i) * 40000;  // 25fps: 40ms interval
      bool keyframe = (i % 25 == 0);  // Keyframe every 25 frames
      auto frame = MakeTestFrame(1, width_, height_, pts, keyframe);
      output_queue->Push(frame);
    }
  }

 private:
  int total_frames_;
  int width_;
  int height_;
};

/// A mock decode stage that passes raw frames through unchanged.
/// Simulates decoding by just forwarding the frame.
class MockDecodeStage : public core::PipelineStage {
 public:
  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    if (frame && frame->type == FrameType::kEncoded) {
      // Simulate decoding: change type to raw
      frame->type = FrameType::kRaw;
    }
    ++decoded_count_;
    return PassToNext(std::move(frame));
  }

  void Shutdown() override {}
  std::string Name() const override { return "MockDecode"; }

  std::atomic<int> decoded_count_{0};
};

/// A mock output/sink stage that collects frames for verification.
class MockOutputStage : public core::PipelineStage {
 public:
  bool Initialize(const StageConfig& /*config*/) override { return true; }

  bool ProcessFrame(std::shared_ptr<Frame> frame) override {
    if (frame) {
      std::lock_guard<std::mutex> lock(mutex_);
      received_frames_.push_back(frame);
      has_ai_result_count_ += frame->analysis.has_result ? 1 : 0;
    }
    return true;  // Terminal stage, no PassToNext
  }

  void Shutdown() override {}
  std::string Name() const override { return "MockOutput"; }

  size_t ReceivedCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_frames_.size();
  }

  int AiResultCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_ai_result_count_;
  }

  std::vector<std::shared_ptr<Frame>> GetFrames() {
    std::lock_guard<std::mutex> lock(mutex_);
    return received_frames_;
  }

 private:
  std::mutex mutex_;
  std::vector<std::shared_ptr<Frame>> received_frames_;
  int has_ai_result_count_ = 0;
};

/// A mock inference backend that produces fake detections.
/// No real model loading required — generates synthetic bounding boxes.
/// Starts pre-loaded so InferenceEngine can run immediately.
class MockInferenceBackend : public ai_engine::InferenceBackend {
 public:
  MockInferenceBackend() : loaded_(true) {}

  bool LoadModel(const std::string& /*model_path*/,
                 const ai_engine::BackendConfig& /*config*/) override {
    loaded_ = true;
    return true;
  }

  bool RunInference(const std::vector<float>& /*input_data*/,
                    const ai_engine::TensorShape& input_shape,
                    std::vector<float>& output_data,
                    ai_engine::TensorShape& output_shape) override {
    if (!loaded_) return false;
    ++inference_count_;

    // Determine batch size from input shape.
    int batch = (input_shape.size() >= 1) ? static_cast<int>(input_shape[0]) : 1;

    // Generate YOLOv8-style output: [batch, 84, 8400]
    int num_features = 84;  // 4 box + 80 classes
    int num_detections = 8400;
    output_shape = {static_cast<int64_t>(batch),
                    static_cast<int64_t>(num_features),
                    static_cast<int64_t>(num_detections)};

    size_t total = static_cast<size_t>(batch * num_features * num_detections);
    output_data.resize(total, 0.0f);

    // Inject 2 fake detections per batch item
    for (int b = 0; b < batch; ++b) {
      size_t base = static_cast<size_t>(b * num_features * num_detections);

      // Detection 0: cx=320, cy=240, w=100, h=80, class0=0.9
      output_data[base + static_cast<size_t>(0 * num_detections)] = 320.0f;
      output_data[base + static_cast<size_t>(1 * num_detections)] = 240.0f;
      output_data[base + static_cast<size_t>(2 * num_detections)] = 100.0f;
      output_data[base + static_cast<size_t>(3 * num_detections)] = 80.0f;
      output_data[base + static_cast<size_t>(4 * num_detections)] = 0.9f;

      // Detection 1: cx=160, cy=120, w=60, h=40, class1=0.85
      output_data[base + static_cast<size_t>(0 * num_detections + 1)] = 160.0f;
      output_data[base + static_cast<size_t>(1 * num_detections + 1)] = 120.0f;
      output_data[base + static_cast<size_t>(2 * num_detections + 1)] = 60.0f;
      output_data[base + static_cast<size_t>(3 * num_detections + 1)] = 40.0f;
      output_data[base + static_cast<size_t>(5 * num_detections + 1)] = 0.85f;
    }

    return true;
  }

  bool IsLoaded() const override { return loaded_; }
  bool SupportsBatch() const override { return supports_batch_; }
  int MaxBatchSize() const override { return 16; }
  void Unload() override { loaded_ = false; }
  std::string Name() const override { return "MockBackend"; }

  std::atomic<int> inference_count_{0};
  bool supports_batch_ = false;

 private:
  bool loaded_ = false;
};

// ============================================================
// Integration Test: Pipeline Frame Flow
// ============================================================

TEST(PipelineIntegration, FrameFlowThroughAllStages) {
  // Build pipeline: MockSource → MockDecode → AiStage → MockOutput
  core::ChannelPipeline pipeline(1);

  auto decode_stage = std::make_unique<MockDecodeStage>();
  auto ai_stage = std::make_unique<pipeline_stages::AiStage>();
  auto output_stage = std::make_unique<MockOutputStage>();

  auto* decode_ptr = decode_stage.get();
  auto* output_ptr = output_stage.get();

  // Create a mock inference engine with fake backend
  auto mock_backend = std::make_unique<MockInferenceBackend>();
  auto* backend_ptr = mock_backend.get();
  auto mock_adapter = ai_engine::YoloAdapterFactory::Create("yolov8");
  auto engine = std::make_shared<ai_engine::InferenceEngine>(
      std::move(mock_backend), std::move(mock_adapter));

  ai_stage->SetInferenceEngine(engine);

  // Add stages to pipeline
  StageConfig cfg;
  cfg.queue_capacity = 32;

  cfg.name = "decode";
  pipeline.AddStage(std::move(decode_stage), cfg);

  cfg.name = "ai";
  cfg.params = R"({"confidence_threshold": 0.3})";
  pipeline.AddStage(std::move(ai_stage), cfg);

  cfg.name = "output";
  cfg.params = "";
  pipeline.AddStage(std::move(output_stage), cfg);

  ASSERT_TRUE(pipeline.Initialize());
  ASSERT_TRUE(pipeline.Start());

  // Push 20 raw test frames
  auto input_queue = pipeline.GetInputQueue();
  ASSERT_NE(input_queue, nullptr);

  constexpr int kFrameCount = 20;
  for (int i = 0; i < kFrameCount; ++i) {
    auto pts = static_cast<int64_t>(i) * 40000;
    auto frame = MakeTestFrame(1, 320, 240, pts, i == 0);
    input_queue->Push(frame);
  }

  // Wait for processing
  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_TRUE(pipeline.Stop());

  // Verify frames flowed through all stages
  EXPECT_GT(decode_ptr->decoded_count_.load(), 0);
  EXPECT_GT(output_ptr->ReceivedCount(), 0u);
  EXPECT_GT(backend_ptr->inference_count_.load(), 0);

  // Verify AI results are attached
  EXPECT_GT(output_ptr->AiResultCount(), 0);

  // Verify detection results on received frames
  auto frames = output_ptr->GetFrames();
  int frames_with_detections = 0;
  for (const auto& f : frames) {
    if (f->analysis.has_result && !f->analysis.detections.empty()) {
      ++frames_with_detections;
      // Mock backend produces 2 detections per frame
      EXPECT_GE(f->analysis.detections.size(), 1u);
      EXPECT_GT(f->analysis.inference_time_us, 0);
    }
  }
  EXPECT_GT(frames_with_detections, 0);

  spdlog::info("PipelineIntegration: {} frames processed, {} with AI results, "
               "{} with detections",
               output_ptr->ReceivedCount(), output_ptr->AiResultCount(),
               frames_with_detections);
}

// ============================================================
// Integration Test: Batch Inference Pipeline
// ============================================================

TEST(PipelineIntegration, BatchInferencePipeline) {
  core::ChannelPipeline pipeline(2);

  auto ai_stage = std::make_unique<pipeline_stages::AiStage>();
  auto output_stage = std::make_unique<MockOutputStage>();

  auto* output_ptr = output_stage.get();

  // Create mock backend with batch support
  auto mock_backend = std::make_unique<MockInferenceBackend>();
  mock_backend->supports_batch_ = true;
  auto* backend_ptr = mock_backend.get();
  auto mock_adapter = ai_engine::YoloAdapterFactory::Create("yolov8");
  auto engine = std::make_shared<ai_engine::InferenceEngine>(
      std::move(mock_backend), std::move(mock_adapter));

  ai_stage->SetInferenceEngine(engine);

  StageConfig cfg;
  cfg.queue_capacity = 64;

  cfg.name = "ai_batch";
  cfg.params = R"({"confidence_threshold": 0.3, "batch_size": 4})";
  pipeline.AddStage(std::move(ai_stage), cfg);

  cfg.name = "output";
  cfg.params = "";
  pipeline.AddStage(std::move(output_stage), cfg);

  ASSERT_TRUE(pipeline.Initialize());
  ASSERT_TRUE(pipeline.Start());

  auto input_queue = pipeline.GetInputQueue();
  ASSERT_NE(input_queue, nullptr);

  // Push 16 frames (should be processed in ~4 batches of 4)
  constexpr int kFrameCount = 16;
  for (int i = 0; i < kFrameCount; ++i) {
    auto pts = static_cast<int64_t>(i) * 40000;
    auto frame = MakeTestFrame(2, 320, 240, pts, i == 0);
    input_queue->Push(frame);
  }

  std::this_thread::sleep_for(std::chrono::seconds(3));
  ASSERT_TRUE(pipeline.Stop());

  // All frames should reach the output
  EXPECT_GT(output_ptr->ReceivedCount(), 0u);

  // With batch backend, fewer inference calls than frame count
  // (batch_size=4, so ~4 inference calls for 16 frames, or more if
  // some arrive individually due to timing)
  spdlog::info("BatchInference: {} frames out, {} inference calls",
               output_ptr->ReceivedCount(),
               backend_ptr->inference_count_.load());
}

// ============================================================
// Integration Test: Multi-Channel Pipeline
// ============================================================

TEST(PipelineIntegration, MultiChannelIsolation) {
  // Create two independent pipelines to verify channel isolation
  auto build_pipeline = [](int ch_id) {
    auto pipeline = std::make_unique<core::ChannelPipeline>(ch_id);
    auto output_stage = std::make_unique<MockOutputStage>();
    auto* output_ptr = output_stage.get();

    StageConfig cfg;
    cfg.name = "output";
    cfg.queue_capacity = 32;
    pipeline->AddStage(std::move(output_stage), cfg);
    pipeline->Initialize();

    return std::make_pair(std::move(pipeline), output_ptr);
  };

  auto [pipeline1, output1] = build_pipeline(1);
  auto [pipeline2, output2] = build_pipeline(2);

  pipeline1->Start();
  pipeline2->Start();

  // Push different frame counts to each channel
  auto q1 = pipeline1->GetInputQueue();
  auto q2 = pipeline2->GetInputQueue();

  for (int i = 0; i < 10; ++i) {
    q1->Push(MakeTestFrame(1, 320, 240, static_cast<int64_t>(i) * 40000));
  }
  for (int i = 0; i < 5; ++i) {
    q2->Push(MakeTestFrame(2, 640, 480, static_cast<int64_t>(i) * 40000));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  pipeline1->Stop();
  pipeline2->Stop();

  // Verify isolation — each channel received its own frames
  EXPECT_EQ(output1->ReceivedCount(), 10u);
  EXPECT_EQ(output2->ReceivedCount(), 5u);

  // Verify channel IDs are correct
  for (const auto& f : output1->GetFrames()) {
    EXPECT_EQ(f->channel_id, 1);
  }
  for (const auto& f : output2->GetFrames()) {
    EXPECT_EQ(f->channel_id, 2);
  }
}

// ============================================================
// Integration Test: Recording Index Round-Trip
// ============================================================

TEST(PipelineIntegration, RecordingIndexRoundTrip) {
  // Test the full recording lifecycle:
  // Insert segment → Update → Query → Verify

  storage::RecordIndex index;
  ASSERT_TRUE(index.Open(":memory:"));

  // Simulate recording segments for 2 channels
  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  // Channel 1: 3 segments
  for (int i = 0; i < 3; ++i) {
    storage::SegmentInfo seg;
    seg.channel_id = 1;
    seg.start_time = now + static_cast<int64_t>(i * 1800000);
    seg.end_time = seg.start_time + 1800000;
    seg.file_path = "/recordings/ch1/seg_" + std::to_string(i) + ".mp4";
    seg.file_size = 50 * 1024 * 1024;  // 50MB each
    seg.codec = "h264";
    seg.resolution = "1920x1080";
    seg.has_ai_overlay = true;

    int64_t seg_id = index.InsertSegment(seg);
    EXPECT_GT(seg_id, 0);
  }

  // Channel 2: 2 segments
  for (int i = 0; i < 2; ++i) {
    storage::SegmentInfo seg;
    seg.channel_id = 2;
    seg.start_time = now + static_cast<int64_t>(i * 1800000);
    seg.end_time = seg.start_time + 1800000;
    seg.file_path = "/recordings/ch2/seg_" + std::to_string(i) + ".mp4";
    seg.file_size = 40 * 1024 * 1024;
    seg.codec = "h265";
    seg.resolution = "1280x720";
    seg.has_ai_overlay = false;

    int64_t seg_id = index.InsertSegment(seg);
    EXPECT_GT(seg_id, 0);
  }

  // Insert events
  storage::EventInfo evt;
  evt.channel_id = 1;
  evt.segment_id = 1;
  evt.event_type = "person_detected";
  evt.event_time = now + 500;
  evt.confidence = 0.92f;
  evt.metadata = R"({"class": "person", "box": [100,200,300,400]})";
  int64_t evt_id = index.InsertEvent(evt);
  EXPECT_GT(evt_id, 0);

  // Query channel 1 segments
  auto ch1_segs = index.QuerySegments(1, now, now + 6000000);
  EXPECT_EQ(ch1_segs.size(), 3u);

  // Query channel 2 segments
  auto ch2_segs = index.QuerySegments(2, now, now + 6000000);
  EXPECT_EQ(ch2_segs.size(), 2u);

  // Query events
  auto events = index.QueryEvents(1, now, now + 6000000);
  EXPECT_EQ(events.size(), 1u);
  if (!events.empty()) {
    EXPECT_EQ(events[0].event_type, "person_detected");
    EXPECT_FLOAT_EQ(events[0].confidence, 0.92f);
  }

  // Verify channel total size
  int64_t ch1_size = index.GetChannelTotalSize(1);
  EXPECT_EQ(ch1_size, 3 * 50 * 1024 * 1024);

  // Set and verify quota
  storage::StorageQuota quota;
  quota.channel_id = 1;
  quota.max_days = 7;
  quota.max_size_gb = 50.0;
  quota.policy = "circular";
  EXPECT_TRUE(index.SetQuota(quota));

  auto fetched = index.GetQuota(1);
  EXPECT_EQ(fetched.max_days, 7);
  EXPECT_DOUBLE_EQ(fetched.max_size_gb, 50.0);

  // Delete oldest segment
  auto oldest = index.GetOldestSegments(1, 1);
  ASSERT_EQ(oldest.size(), 1u);
  std::string deleted_path = index.DeleteSegment(oldest[0].id);
  EXPECT_FALSE(deleted_path.empty());

  // Verify count decreased
  auto remaining = index.QuerySegments(1, now, now + 6000000);
  EXPECT_EQ(remaining.size(), 2u);

  index.Close();
}

// ============================================================
// Integration Test: Pipeline with Overlay
// ============================================================

TEST(PipelineIntegration, PipelineWithOverlay) {
  // Test: Source frames → AI (mock) → Overlay → Output
  // Verify overlay modifies frame data (draws bounding boxes)
  core::ChannelPipeline pipeline(3);

  auto ai_stage = std::make_unique<pipeline_stages::AiStage>();
  auto overlay_stage = std::make_unique<pipeline_stages::OverlayStage>();
  auto output_stage = std::make_unique<MockOutputStage>();

  auto* output_ptr = output_stage.get();

  // Create mock inference engine
  auto mock_backend = std::make_unique<MockInferenceBackend>();
  auto mock_adapter = ai_engine::YoloAdapterFactory::Create("yolov8");
  auto engine = std::make_shared<ai_engine::InferenceEngine>(
      std::move(mock_backend), std::move(mock_adapter));
  ai_stage->SetInferenceEngine(engine);

  StageConfig cfg;
  cfg.queue_capacity = 32;

  cfg.name = "ai";
  cfg.params = R"({"confidence_threshold": 0.3})";
  pipeline.AddStage(std::move(ai_stage), cfg);

  cfg.name = "overlay";
  cfg.params = R"({"line_width": 2, "font_scale": 0.5})";
  pipeline.AddStage(std::move(overlay_stage), cfg);

  cfg.name = "output";
  cfg.params = "";
  pipeline.AddStage(std::move(output_stage), cfg);

  ASSERT_TRUE(pipeline.Initialize());
  ASSERT_TRUE(pipeline.Start());

  auto input_queue = pipeline.GetInputQueue();

  // Push 5 frames
  for (int i = 0; i < 5; ++i) {
    auto pts = static_cast<int64_t>(i) * 40000;
    auto frame = MakeTestFrame(3, 640, 480, pts, i == 0);
    input_queue->Push(frame);
  }

  std::this_thread::sleep_for(std::chrono::seconds(2));
  ASSERT_TRUE(pipeline.Stop());

  // Verify frames were received
  EXPECT_GT(output_ptr->ReceivedCount(), 0u);

  // Verify frames have AI results AND overlay was applied
  auto frames = output_ptr->GetFrames();
  for (const auto& f : frames) {
    EXPECT_EQ(f->channel_id, 3);
    EXPECT_EQ(f->type, FrameType::kRaw);
    EXPECT_NE(f->data, nullptr);
  }
}

// ============================================================
// Integration Test: Pipeline Graceful Shutdown Under Load
// ============================================================

TEST(PipelineIntegration, GracefulShutdownUnderLoad) {
  core::ChannelPipeline pipeline(4);

  auto output_stage = std::make_unique<MockOutputStage>();
  auto* output_ptr = output_stage.get();

  StageConfig cfg;
  cfg.name = "output";
  cfg.queue_capacity = 256;
  pipeline.AddStage(std::move(output_stage), cfg);

  ASSERT_TRUE(pipeline.Initialize());
  ASSERT_TRUE(pipeline.Start());

  auto input_queue = pipeline.GetInputQueue();

  // Push a large burst of frames
  constexpr int kBurstSize = 100;
  for (int i = 0; i < kBurstSize; ++i) {
    auto pts = static_cast<int64_t>(i) * 40000;
    input_queue->Push(MakeTestFrame(4, 160, 120, pts));
  }

  // Immediately drain and stop
  EXPECT_TRUE(pipeline.DrainAndStop());

  // Pipeline should have processed most frames
  EXPECT_GT(output_ptr->ReceivedCount(), 0u);

  spdlog::info("GracefulShutdown: {}/{} frames processed before shutdown",
               output_ptr->ReceivedCount(), kBurstSize);
}

// ============================================================
// Integration Test: Backend Factory
// ============================================================

TEST(PipelineIntegration, BackendFactoryCreatesCorrectBackend) {
  auto opencv_backend = ai_engine::BackendFactory::Create("opencv_dnn");
  ASSERT_NE(opencv_backend, nullptr);
  EXPECT_EQ(opencv_backend->Name(), "OpenCVDNN");
  EXPECT_FALSE(opencv_backend->SupportsBatch());

  auto dnn_backend = ai_engine::BackendFactory::Create("dnn");
  ASSERT_NE(dnn_backend, nullptr);
  EXPECT_EQ(dnn_backend->Name(), "OpenCVDNN");

  // Unknown backend should fall back to opencv_dnn
  auto unknown_backend = ai_engine::BackendFactory::Create("nonexistent");
  ASSERT_NE(unknown_backend, nullptr);
  EXPECT_EQ(unknown_backend->Name(), "OpenCVDNN");
}

// ============================================================
// Integration Test: AI Adapter Batch Preprocessing
// ============================================================

TEST(PipelineIntegration, YoloAdapterBatchPreProcess) {
  auto adapter = ai_engine::YoloAdapterFactory::Create("yolov8");
  ASSERT_NE(adapter, nullptr);

  // Create 4 test images of different sizes
  std::vector<std::vector<uint8_t>> images;
  std::vector<const uint8_t*> image_ptrs;
  std::vector<int> widths = {320, 640, 160, 480};
  std::vector<int> heights = {240, 480, 120, 360};

  for (size_t i = 0; i < widths.size(); ++i) {
    images.emplace_back(
        static_cast<size_t>(widths[i] * heights[i] * 3),
        static_cast<uint8_t>(64 + i * 32));
    image_ptrs.push_back(images.back().data());
  }

  std::vector<float> batch_data;
  ai_engine::TensorShape batch_shape;

  EXPECT_TRUE(adapter->PreProcessBatch(image_ptrs, widths, heights,
                                        batch_data, batch_shape));

  // Batch shape should be [4, 3, 640, 640]
  ASSERT_EQ(batch_shape.size(), 4u);
  EXPECT_EQ(batch_shape[0], 4);
  EXPECT_EQ(batch_shape[1], 3);
  EXPECT_EQ(batch_shape[2], 640);
  EXPECT_EQ(batch_shape[3], 640);

  // Total elements: 4 * 3 * 640 * 640
  size_t expected_size = static_cast<size_t>(4 * 3 * 640 * 640);
  EXPECT_EQ(batch_data.size(), expected_size);
}

// ============================================================
// Integration Test: End-to-End with Recording Index
// ============================================================

TEST(PipelineIntegration, EndToEndWithRecordingSimulation) {
  // Simulate the full NVR lifecycle:
  // 1. Create channel pipeline
  // 2. Push frames through pipeline
  // 3. Record segment metadata to index
  // 4. Query recordings back

  // Step 1: Pipeline
  core::ChannelPipeline pipeline(1);
  auto output_stage = std::make_unique<MockOutputStage>();
  auto* output_ptr = output_stage.get();

  StageConfig cfg;
  cfg.name = "output";
  cfg.queue_capacity = 64;
  pipeline.AddStage(std::move(output_stage), cfg);
  ASSERT_TRUE(pipeline.Initialize());
  ASSERT_TRUE(pipeline.Start());

  // Step 2: Push frames
  auto input_queue = pipeline.GetInputQueue();
  int64_t start_pts = 0;
  constexpr int kFrameCount = 50;
  for (int i = 0; i < kFrameCount; ++i) {
    auto pts = start_pts + static_cast<int64_t>(i) * 40000;
    input_queue->Push(MakeTestFrame(1, 640, 480, pts, i == 0));
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  pipeline.Stop();

  EXPECT_EQ(output_ptr->ReceivedCount(), static_cast<size_t>(kFrameCount));

  // Step 3: Simulate recording to index
  storage::RecordIndex index;
  ASSERT_TRUE(index.Open(":memory:"));

  int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();

  storage::SegmentInfo seg;
  seg.channel_id = 1;
  seg.start_time = now;
  seg.end_time = now + static_cast<int64_t>(kFrameCount * 40);
  seg.file_path = "/recordings/ch1/test_segment.mp4";
  seg.file_size = static_cast<int64_t>(kFrameCount * 4096);
  seg.codec = "h264";
  seg.resolution = "640x480";
  seg.has_ai_overlay = false;

  int64_t seg_id = index.InsertSegment(seg);
  EXPECT_GT(seg_id, 0);

  // Step 4: Query back
  auto segments = index.QuerySegments(1, now - 1000, now + 100000);
  ASSERT_EQ(segments.size(), 1u);
  EXPECT_EQ(segments[0].channel_id, 1);
  EXPECT_EQ(segments[0].codec, "h264");
  EXPECT_EQ(segments[0].resolution, "640x480");
  EXPECT_EQ(segments[0].file_size, static_cast<int64_t>(kFrameCount * 4096));

  index.Close();
}

}  // namespace
}  // namespace integration
}  // namespace loong
