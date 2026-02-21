// Copyright 2026 Loong AI NVR Project

#include "core/common/types.h"
#include "gtest/gtest.h"
#include "overlay/compositor/channel_compositor.h"
#include "overlay/osd_renderer/osd_renderer.h"
#include "pipeline_stages/overlay_stage/overlay_stage.h"

#include <cstring>
#include <memory>

namespace loong {
namespace {

/// Helper: create a test BGR24 frame filled with a solid color.
std::shared_ptr<Frame> MakeTestFrame(int channel_id, int width, int height,
                                     uint8_t r, uint8_t g, uint8_t b) {
  auto frame = std::make_shared<Frame>();
  frame->channel_id = channel_id;
  frame->type = FrameType::kRaw;
  frame->info.width = width;
  frame->info.height = height;
  frame->info.stride = width * 3;
  frame->data_size = static_cast<size_t>(width * height * 3);
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->data_size]);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int offset = y * frame->info.stride + x * 3;
      frame->data[offset + 0] = b;
      frame->data[offset + 1] = g;
      frame->data[offset + 2] = r;
    }
  }
  return frame;
}

/// Add mock detections to a frame.
void AddDetections(std::shared_ptr<Frame>& frame, int count, float confidence) {
  frame->analysis.has_result = true;
  for (int i = 0; i < count; ++i) {
    Detection det;
    det.x1 = static_cast<float>(50 + i * 80);
    det.y1 = 50.0f;
    det.x2 = det.x1 + 60.0f;
    det.y2 = 110.0f;
    det.confidence = confidence;
    det.class_id = i % 5;
    det.class_name = "object_" + std::to_string(i);
    frame->analysis.detections.push_back(det);
  }
}

// ============================================================
// OsdRenderer Tests (standalone library)
// ============================================================

TEST(OsdRenderer, ConfigureAndRender) {
  overlay::OsdRenderer renderer;
  OverlayConfig config;
  config.show_timestamp = true;
  config.show_channel_name = true;
  renderer.Configure(config);
  renderer.SetChannelInfo(1, "Test Camera");

  int w = 640, h = 480, stride = w * 3;
  std::vector<uint8_t> data(static_cast<size_t>(stride * h), 128);
  std::vector<Detection> detections;
  Detection det{100, 100, 200, 200, 0.9f, 0, "person"};
  detections.push_back(det);

  renderer.Render(data.data(), w, h, stride, detections);

  // Frame should be modified
  bool all_same = true;
  for (size_t i = 0; i < data.size(); ++i) {
    if (data[i] != 128) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same);
}

TEST(OsdRenderer, DisabledDoesNotModify) {
  overlay::OsdRenderer renderer;
  OverlayConfig config;
  config.enabled = false;
  renderer.Configure(config);

  int w = 320, h = 240, stride = w * 3;
  std::vector<uint8_t> data(static_cast<size_t>(stride * h), 200);
  std::vector<uint8_t> original = data;

  std::vector<Detection> detections;
  Detection det{50, 50, 100, 100, 0.8f, 0, "car"};
  detections.push_back(det);

  renderer.Render(data.data(), w, h, stride, detections);
  EXPECT_TRUE(std::memcmp(original.data(), data.data(), data.size()) == 0);
}

TEST(OsdRenderer, EmptyDetections) {
  overlay::OsdRenderer renderer;
  OverlayConfig config;
  config.show_timestamp = true;
  renderer.Configure(config);

  int w = 320, h = 240, stride = w * 3;
  std::vector<uint8_t> data(static_cast<size_t>(stride * h), 100);
  std::vector<Detection> empty;

  renderer.Render(data.data(), w, h, stride, empty);
  // Should still render timestamp OSD
}

TEST(OsdRenderer, TrajectoryAccumulation) {
  overlay::OsdRenderer renderer;
  OverlayConfig config;
  config.show_trajectory = true;
  config.trajectory_max_points = 10;
  renderer.Configure(config);

  int w = 320, h = 240, stride = w * 3;
  for (int i = 0; i < 5; ++i) {
    std::vector<uint8_t> data(static_cast<size_t>(stride * h), 80);
    std::vector<Detection> dets;
    Detection det;
    det.x1 = static_cast<float>(50 + i * 20);
    det.y1 = 50;
    det.x2 = det.x1 + 40;
    det.y2 = 90;
    det.confidence = 0.9f;
    det.class_id = 0;
    dets.push_back(det);
    renderer.Render(data.data(), w, h, stride, dets);
  }

  renderer.ResetTrajectories();
}

TEST(OsdRenderer, TimestampPositions) {
  for (int pos = 0; pos < 4; ++pos) {
    overlay::OsdRenderer renderer;
    OverlayConfig config;
    config.show_timestamp = true;
    config.timestamp_position = pos;
    renderer.Configure(config);

    int w = 640, h = 480, stride = w * 3;
    std::vector<uint8_t> data(static_cast<size_t>(stride * h), 50);
    renderer.Render(data.data(), w, h, stride, {});
  }
}

// ============================================================
// OverlayStage Tests (pipeline integration)
// ============================================================

TEST(OverlayStage, InitializeWithDefaults) {
  pipeline_stages::OverlayStage stage;
  StageConfig config;
  config.params = "{}";
  EXPECT_TRUE(stage.Initialize(config));
}

TEST(OverlayStage, InitializeWithCustomConfig) {
  pipeline_stages::OverlayStage stage;
  StageConfig config;
  config.params = R"({
    "enabled": true,
    "line_thickness": 3,
    "show_trajectory": true,
    "channel_id": 5,
    "channel_name": "Test Camera"
  })";
  EXPECT_TRUE(stage.Initialize(config));
}

TEST(OverlayStage, ProcessFrameWithDetections) {
  pipeline_stages::OverlayStage stage;
  StageConfig config;
  config.params =
      R"({"show_timestamp":true,"channel_id":1,"channel_name":"Front"})";
  stage.Initialize(config);

  auto frame = MakeTestFrame(1, 640, 480, 64, 64, 64);
  AddDetections(frame, 3, 0.85f);
  EXPECT_TRUE(stage.ProcessFrame(frame));

  bool all_same = true;
  for (size_t i = 0; i < frame->data_size; ++i) {
    auto idx = static_cast<std::ptrdiff_t>(i);
    if (frame->data[idx] != 64) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same);
}

TEST(OverlayStage, DisabledOverlayPassesThrough) {
  pipeline_stages::OverlayStage stage;
  StageConfig config;
  config.params = R"({"enabled":false})";
  stage.Initialize(config);

  auto frame = MakeTestFrame(1, 320, 240, 200, 200, 200);
  AddDetections(frame, 2, 0.9f);

  std::vector<uint8_t> original(frame->data.get(),
                                frame->data.get() + frame->data_size);
  EXPECT_TRUE(stage.ProcessFrame(frame));

  bool identical =
      std::memcmp(original.data(), frame->data.get(), frame->data_size) == 0;
  EXPECT_TRUE(identical);
}

TEST(OverlayStage, SkipsEncodedFrames) {
  pipeline_stages::OverlayStage stage;
  StageConfig config;
  config.params = "{}";
  stage.Initialize(config);

  auto frame = std::make_shared<Frame>();
  frame->type = FrameType::kEncoded;
  EXPECT_TRUE(stage.ProcessFrame(frame));
}

// ============================================================
// ChannelCompositor Tests (standalone library)
// ============================================================

TEST(ChannelCompositor, CreateAndSetLayout) {
  overlay::ChannelCompositor comp(1920, 1080);
  EXPECT_EQ(comp.GetLayout(), overlay::GridLayout::k2x2);
  EXPECT_EQ(comp.CellCount(), 4);

  comp.SetLayout(overlay::GridLayout::k3x3);
  EXPECT_EQ(comp.CellCount(), 9);
  EXPECT_EQ(comp.GridCols(), 3);
  EXPECT_EQ(comp.GridRows(), 3);
}

TEST(ChannelCompositor, CellDimensions) {
  overlay::ChannelCompositor comp(1920, 1080);
  comp.SetLayout(overlay::GridLayout::k2x2);
  EXPECT_EQ(comp.CellWidth(), 960);
  EXPECT_EQ(comp.CellHeight(), 540);

  comp.SetLayout(overlay::GridLayout::k4x4);
  EXPECT_EQ(comp.CellWidth(), 480);
  EXPECT_EQ(comp.CellHeight(), 270);
}

TEST(ChannelCompositor, CompositeEmptyGrid) {
  overlay::ChannelCompositor comp(640, 480);
  comp.SetLayout(overlay::GridLayout::k2x2);

  int stride = 640 * 3;
  std::vector<uint8_t> output(static_cast<size_t>(stride * 480), 0);
  EXPECT_TRUE(comp.Composite(output.data(), stride));

  bool all_zero = true;
  for (size_t i = 0; i < output.size(); ++i) {
    if (output[i] != 0) {
      all_zero = false;
      break;
    }
  }
  EXPECT_FALSE(all_zero);
}

TEST(ChannelCompositor, UpdateAndCompositeChannel) {
  overlay::ChannelCompositor comp(640, 480);
  comp.SetLayout(overlay::GridLayout::k2x2);

  int src_w = 320, src_h = 240, src_stride = src_w * 3;
  std::vector<uint8_t> src(static_cast<size_t>(src_stride * src_h));
  for (int y = 0; y < src_h; ++y) {
    for (int x = 0; x < src_w; ++x) {
      int off = y * src_stride + x * 3;
      src[static_cast<size_t>(off + 0)] = 0;
      src[static_cast<size_t>(off + 1)] = 0;
      src[static_cast<size_t>(off + 2)] = 255;
    }
  }

  overlay::CompositorInput input;
  input.channel_id = 1;
  input.data = src.data();
  input.width = src_w;
  input.height = src_h;
  input.stride = src_stride;
  comp.UpdateChannel(input);

  int out_stride = 640 * 3;
  std::vector<uint8_t> output(static_cast<size_t>(out_stride * 480), 0);
  EXPECT_TRUE(comp.Composite(output.data(), out_stride));

  bool found_red = false;
  int cell_w = comp.CellWidth();
  int cell_h = comp.CellHeight();
  for (int y = 0; y < cell_h && !found_red; ++y) {
    for (int x = 0; x < cell_w; ++x) {
      size_t off = static_cast<size_t>(y * out_stride + x * 3);
      if (output[off + 2] > 200 && output[off + 0] < 50 &&
          output[off + 1] < 50) {
        found_red = true;
        break;
      }
    }
  }
  EXPECT_TRUE(found_red);
}

TEST(ChannelCompositor, RemoveChannel) {
  overlay::ChannelCompositor comp(640, 480);
  comp.SetLayout(overlay::GridLayout::k1x1);

  int src_w = 320, src_h = 240;
  std::vector<uint8_t> src(static_cast<size_t>(src_w * src_h * 3), 128);
  overlay::CompositorInput input;
  input.channel_id = 5;
  input.data = src.data();
  input.width = src_w;
  input.height = src_h;
  input.stride = src_w * 3;
  comp.UpdateChannel(input);
  comp.RemoveChannel(5);

  int out_stride = 640 * 3;
  std::vector<uint8_t> output(static_cast<size_t>(out_stride * 480), 0);
  EXPECT_TRUE(comp.Composite(output.data(), out_stride));
}

TEST(ChannelCompositor, CellAssignment) {
  overlay::ChannelCompositor comp(640, 480);
  comp.SetLayout(overlay::GridLayout::k2x2);
  comp.AssignCell(10, 2);

  int src_w = 320, src_h = 240;
  std::vector<uint8_t> src(static_cast<size_t>(src_w * src_h * 3), 200);
  overlay::CompositorInput input;
  input.channel_id = 10;
  input.data = src.data();
  input.width = src_w;
  input.height = src_h;
  input.stride = src_w * 3;
  comp.UpdateChannel(input);

  int out_stride = 640 * 3;
  std::vector<uint8_t> output(static_cast<size_t>(out_stride * 480), 0);
  EXPECT_TRUE(comp.Composite(output.data(), out_stride));
}

TEST(ChannelCompositor, AllLayouts) {
  for (auto layout : {overlay::GridLayout::k1x1, overlay::GridLayout::k2x2,
                      overlay::GridLayout::k3x3, overlay::GridLayout::k4x4,
                      overlay::GridLayout::k8x8}) {
    overlay::ChannelCompositor comp(1920, 1080);
    comp.SetLayout(layout);
    EXPECT_GT(comp.CellCount(), 0);
    EXPECT_GT(comp.CellWidth(), 0);
    EXPECT_GT(comp.CellHeight(), 0);

    int out_stride = 1920 * 3;
    std::vector<uint8_t> output(static_cast<size_t>(out_stride * 1080), 0);
    EXPECT_TRUE(comp.Composite(output.data(), out_stride));
  }
}

TEST(ChannelCompositor, NullOutputFails) {
  overlay::ChannelCompositor comp(640, 480);
  EXPECT_FALSE(comp.Composite(nullptr, 640 * 3));
}

}  // namespace
}  // namespace loong
