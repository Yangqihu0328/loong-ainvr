// Copyright 2026 Loong AI NVR Project

#include "ai_engine/lpr/lpr_detector_adapter.h"
#include "ai_engine/lpr/lpr_ocr_adapter.h"
#include "ai_engine/lpr/plate_store.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "gtest/gtest.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace loong::ai_engine {
namespace {

// ============================================================
// LprDetectorAdapter tests
// ============================================================

TEST(LprDetector, FactoryCreatesAdapter) {
  auto adapter = YoloAdapterFactory::Create("lpr_detector");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->ModelFamily(), "lpr_detector");
}

TEST(LprDetector, DefaultPlateClasses) {
  LprDetectorAdapter adapter;
  EXPECT_EQ(adapter.ModelFamily(), "lpr_detector");
  auto shape = adapter.InputShape();
  EXPECT_EQ(shape.size(), 4U);
  EXPECT_EQ(shape[1], 3);  // channels
}

// ============================================================
// LprOcrAdapter tests
// ============================================================

TEST(LprOcr, FactoryCreatesAdapter) {
  auto adapter = YoloAdapterFactory::Create("lpr_ocr");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->ModelFamily(), "lpr_ocr");
}

TEST(LprOcr, InputShape) {
  LprOcrAdapter adapter(100, 32);
  auto shape = adapter.InputShape();
  ASSERT_EQ(shape.size(), 4U);
  EXPECT_EQ(shape[0], 1);
  EXPECT_EQ(shape[1], 1);    // grayscale
  EXPECT_EQ(shape[2], 32);   // height
  EXPECT_EQ(shape[3], 100);  // width
}

TEST(LprOcr, DefaultChineseCharset) {
  auto charset = LprOcrAdapter::DefaultChineseCharset();
  EXPECT_GT(charset.size(), 60U);
  EXPECT_EQ(charset[0], "");  // blank
  EXPECT_EQ(charset[1], "京");
  EXPECT_EQ(charset[31], "宁");  // last province
  // Digits start at index 32
  EXPECT_EQ(charset[32], "0");
  EXPECT_EQ(charset[41], "9");
}

TEST(LprOcr, PreProcessCreatesValidTensor) {
  LprOcrAdapter adapter(100, 32);

  // Create a small dummy BGR image (10x10)
  constexpr int kW = 10, kH = 10;
  std::vector<uint8_t> image(kW * kH * 3, 128);

  std::vector<float> input_data;
  TensorShape shape;
  bool ok = adapter.PreProcess(image.data(), kW, kH, input_data, shape);

  EXPECT_TRUE(ok);
  EXPECT_EQ(input_data.size(), 100U * 32U);
  EXPECT_EQ(shape, (TensorShape{1, 1, 32, 100}));

  // Values should be in [-1, 1] range
  for (float v : input_data) {
    EXPECT_GE(v, -1.1F);
    EXPECT_LE(v, 1.1F);
  }
}

TEST(LprOcr, CtcGreedyDecodeSimple) {
  // Simulate CRNN output: 5 time steps, 5 classes (blank=0, A=1, B=2, C=3, D=4)
  std::vector<std::string> charset = {"", "A", "B", "C", "D"};
  constexpr int kT = 5, kC = 5;

  // Each row is logits for one time step
  // We want to decode: A B C (with blanks between)
  std::vector<float> logits = {
      // t=0: A is highest
      -1.0F,
      5.0F,
      0.0F,
      0.0F,
      0.0F,
      // t=1: blank is highest
      5.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      // t=2: B is highest
      0.0F,
      0.0F,
      5.0F,
      0.0F,
      0.0F,
      // t=3: blank is highest
      5.0F,
      0.0F,
      0.0F,
      0.0F,
      0.0F,
      // t=4: C is highest
      0.0F,
      0.0F,
      0.0F,
      5.0F,
      0.0F,
  };

  float avg_conf = 0.0F;
  auto text =
      LprOcrAdapter::CtcGreedyDecode(logits, kT, kC, charset, &avg_conf);

  EXPECT_EQ(text, "ABC");
  EXPECT_GT(avg_conf, 0.9F);
}

TEST(LprOcr, CtcGreedyDecodeCollapsesRepeats) {
  std::vector<std::string> charset = {"", "A", "B"};
  constexpr int kT = 4, kC = 3;

  // A, A, B, B → should decode to "AB" (collapse repeats)
  std::vector<float> logits = {
      -1.0F, 5.0F, 0.0F,  // A
      -1.0F, 5.0F, 0.0F,  // A (repeat → skip)
      -1.0F, 0.0F, 5.0F,  // B
      -1.0F, 0.0F, 5.0F,  // B (repeat → skip)
  };

  auto text = LprOcrAdapter::CtcGreedyDecode(logits, kT, kC, charset);
  EXPECT_EQ(text, "AB");
}

TEST(LprOcr, CtcGreedyDecodeAllBlanks) {
  std::vector<std::string> charset = {"", "A"};
  constexpr int kT = 3, kC = 2;

  std::vector<float> logits = {
      5.0F, 0.0F, 5.0F, 0.0F, 5.0F, 0.0F,
  };

  float avg_conf = 0.0F;
  auto text =
      LprOcrAdapter::CtcGreedyDecode(logits, kT, kC, charset, &avg_conf);
  EXPECT_TRUE(text.empty());
  EXPECT_FLOAT_EQ(avg_conf, 0.0F);
}

TEST(LprOcr, CtcWithChineseCharset) {
  auto charset = LprOcrAdapter::DefaultChineseCharset();
  // 京=1, 0=32, 1=33, ..., 9=41, A=42, B=43, ...
  // Encode: 京 A 1 2 3 4 5
  constexpr int kC = 70;
  constexpr int kT = 9;

  std::vector<float> logits(kT * kC, -5.0F);

  auto set_best = [&](int t, int class_idx) {
    logits[static_cast<size_t>(t * kC + class_idx)] = 10.0F;
  };

  set_best(0, 1);   // 京
  set_best(1, 0);   // blank
  set_best(2, 42);  // A (index 42 in charset)
  set_best(3, 0);   // blank
  set_best(4, 33);  // 1 (index 33)
  set_best(5, 34);  // 2
  set_best(6, 35);  // 3
  set_best(7, 36);  // 4
  set_best(8, 37);  // 5

  auto text = LprOcrAdapter::CtcGreedyDecode(logits, kT, kC, charset);
  EXPECT_EQ(text, "京A12345");
}

TEST(LprOcr, PostProcessBelowThreshold) {
  LprOcrAdapter adapter;

  // All blanks → empty text → no detection
  std::vector<float> output(5 * 70, 0.0F);
  for (int t = 0; t < 5; ++t) {
    output[static_cast<size_t>(t * 70)] = 10.0F;  // blank is highest
  }

  std::vector<Detection> dets;
  bool ok = adapter.PostProcess(output, {5, 70}, 100, 32, 0.5F, 0.4F, dets);
  EXPECT_TRUE(ok);
  EXPECT_TRUE(dets.empty());
}

// ============================================================
// PlateStore tests
// ============================================================

class PlateStoreTest : public ::testing::Test {
 protected:
  void SetUp() override { store_.Open(":memory:"); }
  PlateStore store_;
};

TEST_F(PlateStoreTest, InsertAndGetById) {
  PlateRecord r;
  r.plate_number = "京A12345";
  r.plate_color = "blue_plate";
  r.confidence = 0.95F;
  r.channel_id = 1;
  r.timestamp = 1000000;
  r.snapshot_path = "/snap/001.jpg";

  int64_t id = store_.Insert(r);
  EXPECT_GT(id, 0);

  auto got = store_.GetById(id);
  EXPECT_EQ(got.id, id);
  EXPECT_EQ(got.plate_number, "京A12345");
  EXPECT_EQ(got.plate_color, "blue_plate");
  EXPECT_NEAR(got.confidence, 0.95F, 0.01F);
  EXPECT_EQ(got.channel_id, 1);
  EXPECT_EQ(got.timestamp, 1000000);
  EXPECT_EQ(got.snapshot_path, "/snap/001.jpg");
}

TEST_F(PlateStoreTest, QueryByPlateNumber) {
  PlateRecord r1;
  r1.plate_number = "京A12345";
  r1.channel_id = 1;
  r1.timestamp = 1000;
  store_.Insert(r1);

  PlateRecord r2;
  r2.plate_number = "沪B67890";
  r2.channel_id = 2;
  r2.timestamp = 2000;
  store_.Insert(r2);

  PlateQuery q;
  q.plate_number = "A123";
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].plate_number, "京A12345");
}

TEST_F(PlateStoreTest, QueryByChannel) {
  for (int i = 0; i < 5; ++i) {
    PlateRecord r;
    r.plate_number = "TEST" + std::to_string(i);
    r.channel_id = (i < 3) ? 1 : 2;
    r.timestamp = i * 1000;
    store_.Insert(r);
  }

  PlateQuery q;
  q.channel_id = 1;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 3U);
}

TEST_F(PlateStoreTest, QueryByTimeRange) {
  for (int i = 0; i < 5; ++i) {
    PlateRecord r;
    r.plate_number = "T" + std::to_string(i);
    r.timestamp = (i + 1) * 1000;
    store_.Insert(r);
  }

  PlateQuery q;
  q.start_time = 2000;
  q.end_time = 4000;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 3U);
}

TEST_F(PlateStoreTest, QueryLimitOffset) {
  for (int i = 0; i < 10; ++i) {
    PlateRecord r;
    r.plate_number = "P" + std::to_string(i);
    r.timestamp = i * 1000;
    store_.Insert(r);
  }

  PlateQuery q;
  q.limit = 3;
  q.offset = 2;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 3U);
}

TEST_F(PlateStoreTest, Count) {
  EXPECT_EQ(store_.Count(), 0);

  PlateRecord r;
  r.plate_number = "X1";
  store_.Insert(r);
  r.plate_number = "X2";
  store_.Insert(r);

  EXPECT_EQ(store_.Count(), 2);
}

TEST_F(PlateStoreTest, PurgeOlderThan) {
  for (int i = 0; i < 5; ++i) {
    PlateRecord r;
    r.plate_number = "DEL" + std::to_string(i);
    r.timestamp = (i + 1) * 1000;
    store_.Insert(r);
  }
  EXPECT_EQ(store_.Count(), 5);

  int deleted = store_.PurgeOlderThan(3000);
  EXPECT_EQ(deleted, 2);
  EXPECT_EQ(store_.Count(), 3);
}

TEST_F(PlateStoreTest, GetByIdNotFound) {
  auto r = store_.GetById(999);
  EXPECT_EQ(r.id, 0);
  EXPECT_TRUE(r.plate_number.empty());
}

}  // namespace
}  // namespace loong::ai_engine
