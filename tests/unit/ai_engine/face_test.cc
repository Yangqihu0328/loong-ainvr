// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_attribute_adapter.h"
#include "ai_engine/face/face_detector_adapter.h"
#include "ai_engine/face/face_store.h"
#include "ai_engine/yolo_adapter/yolo_model_adapter.h"
#include "gtest/gtest.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

namespace loong::ai_engine {
namespace {

// ============================================================
// FaceDetectorAdapter tests
// ============================================================

TEST(FaceDetector, FactoryCreatesAdapter) {
  auto adapter = YoloAdapterFactory::Create("face_detector");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->ModelFamily(), "face_detector");
}

TEST(FaceDetector, FactoryAliases) {
  auto scrfd = YoloAdapterFactory::Create("scrfd");
  ASSERT_NE(scrfd, nullptr);
  EXPECT_EQ(scrfd->ModelFamily(), "face_detector");

  auto retina = YoloAdapterFactory::Create("retinaface");
  ASSERT_NE(retina, nullptr);
  EXPECT_EQ(retina->ModelFamily(), "face_detector");
}

TEST(FaceDetector, InputShape) {
  FaceDetectorAdapter adapter(640);
  auto shape = adapter.InputShape();
  ASSERT_EQ(shape.size(), 4U);
  EXPECT_EQ(shape[0], 1);
  EXPECT_EQ(shape[1], 3);
  EXPECT_EQ(shape[2], 640);
  EXPECT_EQ(shape[3], 640);
}

TEST(FaceDetector, PreProcessValidImage) {
  FaceDetectorAdapter adapter(320);
  std::vector<uint8_t> img(100 * 80 * 3, 128);
  std::vector<float> input_data;
  TensorShape shape;
  EXPECT_TRUE(adapter.PreProcess(img.data(), 100, 80, input_data, shape));
  ASSERT_EQ(shape.size(), 4U);
  EXPECT_EQ(shape[0], 1);
  EXPECT_EQ(shape[1], 3);
  EXPECT_EQ(shape[2], 320);
  EXPECT_EQ(shape[3], 320);
  EXPECT_EQ(input_data.size(), static_cast<size_t>(3 * 320 * 320));
}

TEST(FaceDetector, PreProcessNullImage) {
  FaceDetectorAdapter adapter(640);
  std::vector<float> input_data;
  TensorShape shape;
  EXPECT_FALSE(adapter.PreProcess(nullptr, 100, 100, input_data, shape));
}

TEST(FaceDetector, PostProcessBboxOnly) {
  FaceDetectorAdapter adapter(640);

  // Simulate pre-process to set scale/pad
  std::vector<uint8_t> img(640 * 480 * 3, 128);
  std::vector<float> input_data;
  TensorShape input_shape;
  adapter.PreProcess(img.data(), 640, 480, input_data, input_shape);

  // Output: 2 detections, 5 cols (bbox-only, no landmarks)
  // [x1, y1, x2, y2, score]
  std::vector<float> output = {
      100.0F, 100.0F, 200.0F, 200.0F, 0.95F,
      300.0F, 300.0F, 400.0F, 400.0F, 0.30F,  // below threshold
  };
  TensorShape out_shape = {2, 5};

  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 640, 480, 0.5F, 0.45F, dets));
  EXPECT_EQ(dets.size(), 1U);
  EXPECT_EQ(dets[0].class_name, "face");
  EXPECT_FLOAT_EQ(dets[0].confidence, 0.95F);
}

TEST(FaceDetector, PostProcessWithLandmarks) {
  FaceDetectorAdapter adapter(640);

  std::vector<uint8_t> img(640 * 640 * 3, 128);
  std::vector<float> input_data;
  TensorShape input_shape;
  adapter.PreProcess(img.data(), 640, 640, input_data, input_shape);

  // Output: 1 detection, 15 cols (4 bbox + 1 score + 10 landmarks)
  std::vector<float> output = {
      50.0F,  50.0F,  150.0F, 150.0F, 0.9F, 70.0F, 80.0F,  // left eye
      120.0F, 80.0F,                                       // right eye
      95.0F,  105.0F,                                      // nose
      75.0F,  125.0F,                                      // mouth left
      115.0F, 125.0F,                                      // mouth right
  };
  TensorShape out_shape = {1, 15};

  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 640, 640, 0.5F, 0.45F, dets));
  ASSERT_EQ(dets.size(), 1U);
  EXPECT_EQ(dets[0].class_name, "face");

  auto& faces = adapter.GetLastFaceDetections();
  ASSERT_EQ(faces.size(), 1U);
  EXPECT_GT(faces[0].landmarks[0].x, 0.0F);  // left eye x
  EXPECT_GT(faces[0].landmarks[2].y, 0.0F);  // nose y
}

TEST(FaceDetector, NMSSuppressesDuplicates) {
  FaceDetectorAdapter adapter(640);

  std::vector<uint8_t> img(640 * 640 * 3, 128);
  std::vector<float> input_data;
  TensorShape input_shape;
  adapter.PreProcess(img.data(), 640, 640, input_data, input_shape);

  // Two highly overlapping detections
  std::vector<float> output = {
      100.0F, 100.0F, 200.0F, 200.0F, 0.95F,
      105.0F, 105.0F, 205.0F, 205.0F, 0.88F,
  };
  TensorShape out_shape = {2, 5};

  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 640, 640, 0.5F, 0.3F, dets));
  EXPECT_EQ(dets.size(), 1U);
  EXPECT_FLOAT_EQ(dets[0].confidence, 0.95F);
}

// ============================================================
// FaceAttributeAdapter tests
// ============================================================

TEST(FaceAttribute, FactoryCreatesAdapter) {
  auto adapter = YoloAdapterFactory::Create("face_attribute");
  ASSERT_NE(adapter, nullptr);
  EXPECT_EQ(adapter->ModelFamily(), "face_attribute");
}

TEST(FaceAttribute, FactoryAliases) {
  auto arcface = YoloAdapterFactory::Create("arcface");
  ASSERT_NE(arcface, nullptr);
  EXPECT_EQ(arcface->ModelFamily(), "face_attribute");

  auto insight = YoloAdapterFactory::Create("insightface");
  ASSERT_NE(insight, nullptr);
  EXPECT_EQ(insight->ModelFamily(), "face_attribute");
}

TEST(FaceAttribute, InputShape) {
  FaceAttributeAdapter adapter(112);
  auto shape = adapter.InputShape();
  ASSERT_EQ(shape.size(), 4U);
  EXPECT_EQ(shape[0], 1);
  EXPECT_EQ(shape[1], 3);
  EXPECT_EQ(shape[2], 112);
  EXPECT_EQ(shape[3], 112);
}

TEST(FaceAttribute, PreProcessValidImage) {
  FaceAttributeAdapter adapter(112);
  std::vector<uint8_t> img(112 * 112 * 3, 127);
  std::vector<float> input_data;
  TensorShape shape;
  EXPECT_TRUE(adapter.PreProcess(img.data(), 112, 112, input_data, shape));
  EXPECT_EQ(input_data.size(), static_cast<size_t>(3 * 112 * 112));

  // Value 127 → (127 - 127.5) / 127.5 ≈ -0.00392
  EXPECT_NEAR(input_data[0], -0.00392F, 0.01F);
}

TEST(FaceAttribute, PreProcessNullImage) {
  FaceAttributeAdapter adapter(112);
  std::vector<float> input_data;
  TensorShape shape;
  EXPECT_FALSE(adapter.PreProcess(nullptr, 112, 112, input_data, shape));
}

TEST(FaceAttribute, PostProcessFullModel) {
  FaceAttributeAdapter adapter(112);

  // 514 floats: 512-d embedding + gender_logit + age
  std::vector<float> output(514, 0.0F);
  // Set embedding to a known pattern
  for (int i = 0; i < 512; ++i) {
    output[static_cast<size_t>(i)] = static_cast<float>(i) * 0.01F;
  }
  output[512] = 2.0F;   // gender logit → male (sigmoid > 0.5)
  output[513] = 25.3F;  // age

  TensorShape out_shape = {1, 514};
  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 112, 112, 0.5F, 0.45F, dets));
  EXPECT_EQ(dets.size(), 1U);
  EXPECT_EQ(dets[0].class_name, "face_attr");

  auto& attrs = adapter.GetLastAttributes();
  EXPECT_EQ(attrs.gender, "male");
  EXPECT_EQ(attrs.age, 25);
  EXPECT_EQ(static_cast<int>(attrs.embedding.size()), 512);

  // L2-normalized
  float norm = 0.0F;
  for (float v : attrs.embedding) norm += v * v;
  EXPECT_NEAR(std::sqrt(norm), 1.0F, 0.01F);
}

TEST(FaceAttribute, PostProcessEmbeddingOnly) {
  FaceAttributeAdapter adapter(112);

  std::vector<float> output(512, 1.0F);
  TensorShape out_shape = {1, 512};
  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 112, 112, 0.5F, 0.45F, dets));

  auto& attrs = adapter.GetLastAttributes();
  EXPECT_EQ(attrs.gender, "unknown");
  EXPECT_EQ(attrs.age, -1);
  EXPECT_EQ(static_cast<int>(attrs.embedding.size()), 512);
}

TEST(FaceAttribute, PostProcessFemale) {
  FaceAttributeAdapter adapter(112);

  std::vector<float> output(514, 0.0F);
  for (int i = 0; i < 512; ++i) {
    output[static_cast<size_t>(i)] = 1.0F;
  }
  output[512] = -3.0F;  // sigmoid(-3) ≈ 0.047 → female
  output[513] = 30.0F;

  TensorShape out_shape = {1, 514};
  std::vector<Detection> dets;
  EXPECT_TRUE(
      adapter.PostProcess(output, out_shape, 112, 112, 0.5F, 0.45F, dets));

  auto& attrs = adapter.GetLastAttributes();
  EXPECT_EQ(attrs.gender, "female");
  EXPECT_EQ(attrs.age, 30);
}

TEST(FaceAttribute, PostProcessTooSmallOutput) {
  FaceAttributeAdapter adapter(112);

  std::vector<float> output(100, 0.0F);
  TensorShape out_shape = {1, 100};
  std::vector<Detection> dets;
  EXPECT_FALSE(
      adapter.PostProcess(output, out_shape, 112, 112, 0.5F, 0.45F, dets));
}

TEST(FaceAttribute, L2Normalize) {
  std::vector<float> vec = {3.0F, 4.0F};
  FaceAttributeAdapter::L2Normalize(vec);
  EXPECT_NEAR(vec[0], 0.6F, 0.001F);
  EXPECT_NEAR(vec[1], 0.8F, 0.001F);
}

TEST(FaceAttribute, CosineSimilarityIdentical) {
  std::vector<float> a = {1.0F, 0.0F, 0.0F};
  EXPECT_NEAR(FaceAttributeAdapter::CosineSimilarity(a, a), 1.0F, 0.001F);
}

TEST(FaceAttribute, CosineSimilarityOrthogonal) {
  std::vector<float> a = {1.0F, 0.0F};
  std::vector<float> b = {0.0F, 1.0F};
  EXPECT_NEAR(FaceAttributeAdapter::CosineSimilarity(a, b), 0.0F, 0.001F);
}

TEST(FaceAttribute, CosineSimilarityOpposite) {
  std::vector<float> a = {1.0F, 0.0F};
  std::vector<float> b = {-1.0F, 0.0F};
  EXPECT_NEAR(FaceAttributeAdapter::CosineSimilarity(a, b), -1.0F, 0.001F);
}

// ============================================================
// FaceStore tests
// ============================================================

class FaceStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = "/tmp/face_test_" + std::to_string(getpid()) + ".db";
    store_.Open(db_path_);
  }

  void TearDown() override {
    store_.Close();
    std::remove(db_path_.c_str());
  }

  FaceRecord MakeRecord(int channel, int64_t ts, const std::string& gender,
                        int age, float conf = 0.9F) {
    FaceRecord r;
    r.channel_id = channel;
    r.timestamp = ts;
    r.gender = gender;
    r.age = age;
    r.confidence = conf;
    r.snapshot_path = "/snap/" + std::to_string(ts) + ".jpg";

    // Generate deterministic embedding
    r.embedding.resize(512);
    for (int i = 0; i < 512; ++i) {
      r.embedding[static_cast<size_t>(i)] = static_cast<float>(i + ts) * 0.001F;
    }
    return r;
  }

  std::string db_path_;
  FaceStore store_;
};

TEST_F(FaceStoreTest, InsertAndGetById) {
  auto rec = MakeRecord(0, 1000, "male", 25);
  int64_t id = store_.Insert(rec);
  EXPECT_GT(id, 0);

  auto got = store_.GetById(id);
  EXPECT_EQ(got.id, id);
  EXPECT_EQ(got.channel_id, 0);
  EXPECT_EQ(got.timestamp, 1000);
  EXPECT_EQ(got.gender, "male");
  EXPECT_EQ(got.age, 25);
  EXPECT_EQ(static_cast<int>(got.embedding.size()), 512);
}

TEST_F(FaceStoreTest, QueryByChannel) {
  store_.Insert(MakeRecord(0, 1000, "male", 20));
  store_.Insert(MakeRecord(1, 2000, "female", 30));
  store_.Insert(MakeRecord(0, 3000, "male", 40));

  FaceQuery q;
  q.channel_id = 0;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 2U);
}

TEST_F(FaceStoreTest, QueryByGender) {
  store_.Insert(MakeRecord(0, 1000, "male", 20));
  store_.Insert(MakeRecord(0, 2000, "female", 30));
  store_.Insert(MakeRecord(0, 3000, "female", 40));

  FaceQuery q;
  q.gender = "female";
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 2U);
}

TEST_F(FaceStoreTest, QueryByAgeRange) {
  store_.Insert(MakeRecord(0, 1000, "male", 18));
  store_.Insert(MakeRecord(0, 2000, "male", 25));
  store_.Insert(MakeRecord(0, 3000, "male", 45));

  FaceQuery q;
  q.age_min = 20;
  q.age_max = 30;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].age, 25);
}

TEST_F(FaceStoreTest, QueryByTimeRange) {
  store_.Insert(MakeRecord(0, 1000, "male", 20));
  store_.Insert(MakeRecord(0, 2000, "female", 30));
  store_.Insert(MakeRecord(0, 3000, "male", 40));

  FaceQuery q;
  q.start_time = 1500;
  q.end_time = 2500;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].timestamp, 2000);
}

TEST_F(FaceStoreTest, QueryLimitOffset) {
  for (int i = 0; i < 5; ++i) {
    store_.Insert(
        MakeRecord(0, static_cast<int64_t>(i * 1000), "male", 20 + i));
  }

  FaceQuery q;
  q.limit = 2;
  q.offset = 1;
  auto results = store_.Query(q);
  EXPECT_EQ(results.size(), 2U);
}

TEST_F(FaceStoreTest, Count) {
  EXPECT_EQ(store_.Count(), 0);
  store_.Insert(MakeRecord(0, 1000, "male", 20));
  store_.Insert(MakeRecord(0, 2000, "female", 30));
  EXPECT_EQ(store_.Count(), 2);
}

TEST_F(FaceStoreTest, PurgeOlderThan) {
  store_.Insert(MakeRecord(0, 1000, "male", 20));
  store_.Insert(MakeRecord(0, 2000, "female", 30));
  store_.Insert(MakeRecord(0, 3000, "male", 40));

  int deleted = store_.PurgeOlderThan(2500);
  EXPECT_EQ(deleted, 2);
  EXPECT_EQ(store_.Count(), 1);
}

TEST_F(FaceStoreTest, GetByIdNotFound) {
  auto r = store_.GetById(999);
  EXPECT_EQ(r.id, 0);
}

TEST_F(FaceStoreTest, SearchByEmbedding) {
  auto rec1 = MakeRecord(0, 1000, "male", 25);
  auto rec2 = MakeRecord(0, 2000, "female", 30);
  store_.Insert(rec1);
  store_.Insert(rec2);

  // Query with the same embedding as rec1 → should match with high similarity
  auto matches = store_.SearchByEmbedding(rec1.embedding, 0.5F, 10);
  ASSERT_GE(matches.size(), 1U);
  EXPECT_GT(matches[0].second, 0.9F);
  EXPECT_EQ(matches[0].first.timestamp, 1000);
}

TEST_F(FaceStoreTest, SearchByEmbeddingThreshold) {
  store_.Insert(MakeRecord(0, 1000, "male", 25));

  // Orthogonal vector → similarity ≈ 0
  std::vector<float> ortho(512, 0.0F);
  ortho[0] = 1.0F;

  auto matches = store_.SearchByEmbedding(ortho, 0.99F, 10);
  EXPECT_EQ(matches.size(), 0U);
}

TEST_F(FaceStoreTest, EmbeddingBlobRoundTrip) {
  FaceRecord rec;
  rec.channel_id = 0;
  rec.timestamp = 1000;
  rec.gender = "male";
  rec.age = 25;
  rec.confidence = 0.95F;
  rec.embedding.resize(512);
  for (int i = 0; i < 512; ++i) {
    rec.embedding[static_cast<size_t>(i)] = static_cast<float>(i) * 0.002F;
  }

  int64_t id = store_.Insert(rec);
  auto got = store_.GetById(id);

  ASSERT_EQ(got.embedding.size(), rec.embedding.size());
  for (size_t i = 0; i < rec.embedding.size(); ++i) {
    EXPECT_FLOAT_EQ(got.embedding[i], rec.embedding[i]);
  }
}

}  // namespace
}  // namespace loong::ai_engine
