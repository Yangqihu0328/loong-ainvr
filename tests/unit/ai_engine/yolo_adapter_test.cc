// Copyright 2026 Loong AI NVR Project

#include "ai_engine/yolo_adapter/yolov5_adapter.h"
#include "ai_engine/yolo_adapter/yolov8_adapter.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"

#include "gtest/gtest.h"

namespace loong {
namespace ai_engine {
namespace {

TEST(YoloV5Adapter, PreProcessOutputShape) {
  YoloV5Adapter adapter(640);

  // Create a fake 100x100 BGR image
  std::vector<uint8_t> image(100 * 100 * 3, 128);

  std::vector<float> input_data;
  TensorShape input_shape;

  EXPECT_TRUE(adapter.PreProcess(image.data(), 100, 100,
                                  input_data, input_shape));

  // Should produce NCHW: [1, 3, 640, 640]
  ASSERT_EQ(input_shape.size(), 4u);
  EXPECT_EQ(input_shape[0], 1);
  EXPECT_EQ(input_shape[1], 3);
  EXPECT_EQ(input_shape[2], 640);
  EXPECT_EQ(input_shape[3], 640);
  EXPECT_EQ(input_data.size(), static_cast<size_t>(1 * 3 * 640 * 640));
}

TEST(YoloV8Adapter, PreProcessOutputShape) {
  YoloV8Adapter adapter(640);

  std::vector<uint8_t> image(200 * 150 * 3, 64);

  std::vector<float> input_data;
  TensorShape input_shape;

  EXPECT_TRUE(adapter.PreProcess(image.data(), 200, 150,
                                  input_data, input_shape));

  ASSERT_EQ(input_shape.size(), 4u);
  EXPECT_EQ(input_shape[0], 1);
  EXPECT_EQ(input_shape[1], 3);
  EXPECT_EQ(input_shape[2], 640);
  EXPECT_EQ(input_shape[3], 640);
}

TEST(YoloV5Adapter, PostProcessEmpty) {
  YoloV5Adapter adapter(640);

  // Simulate empty output (all low confidence)
  std::vector<float> output_data(25200 * 85, 0.0f);
  TensorShape output_shape = {1, 25200, 85};

  std::vector<Detection> detections;
  EXPECT_TRUE(adapter.PostProcess(output_data, output_shape,
                                   1920, 1080, 0.5f, 0.45f,
                                   detections));
  EXPECT_TRUE(detections.empty());
}

TEST(YoloV8Adapter, PostProcessEmpty) {
  YoloV8Adapter adapter(640);

  std::vector<float> output_data(84 * 8400, 0.0f);
  TensorShape output_shape = {1, 84, 8400};

  std::vector<Detection> detections;
  EXPECT_TRUE(adapter.PostProcess(output_data, output_shape,
                                   1920, 1080, 0.5f, 0.45f,
                                   detections));
  EXPECT_TRUE(detections.empty());
}

TEST(YoloAdapterFactory, CreateKnownFamilies) {
  auto v5 = YoloAdapterFactory::Create("yolov5");
  EXPECT_NE(v5, nullptr);
  EXPECT_EQ(v5->ModelFamily(), "yolov5");

  auto v8 = YoloAdapterFactory::Create("yolov8");
  EXPECT_NE(v8, nullptr);
  EXPECT_EQ(v8->ModelFamily(), "yolov8");

  auto v11 = YoloAdapterFactory::Create("yolov11");
  EXPECT_NE(v11, nullptr);
  EXPECT_EQ(v11->ModelFamily(), "yolov8");  // v11 uses v8 head
}

}  // namespace
}  // namespace ai_engine
}  // namespace loong
