// Copyright 2026 Loong AI NVR Project

#include "ai_engine/cascade/model_cascade.h"

#include "ai_engine/inference/inference_engine.h"
#include "core/common/types.h"
#include "gtest/gtest.h"

#include <memory>
#include <string>
#include <vector>

namespace loong::ai_engine {
namespace {

// ============================================================
// Mock InferenceEngine for testing (no real model needed)
// ============================================================

/// A fake backend that returns pre-configured detections.
class FakeBackend : public InferenceBackend {
 public:
  bool LoadModel(const std::string& /*path*/,
                 const BackendConfig& /*config*/) override {
    return true;
  }
  bool RunInference(const std::vector<float>& /*input*/,
                    const TensorShape& /*shape*/, std::vector<float>& output,
                    TensorShape& out_shape) override {
    output.clear();
    out_shape = {0};
    return true;
  }
  bool IsLoaded() const override { return true; }
  bool SupportsBatch() const override { return false; }
  int MaxBatchSize() const override { return 1; }
  void Unload() override {}
  std::string Name() const override { return "fake"; }
};

/// A fake adapter that produces configurable detections.
class FakeAdapter : public YoloModelAdapter {
 public:
  void SetDetections(std::vector<Detection> dets) {
    fake_detections_ = std::move(dets);
  }

  bool PreProcess(const uint8_t* /*image*/, int /*w*/, int /*h*/,
                  std::vector<float>& input_data, TensorShape& shape) override {
    input_data = {0};
    shape = {1, 3, 640, 640};
    return true;
  }

  bool PostProcess(const std::vector<float>& /*output*/,
                   const TensorShape& /*shape*/, int /*w*/, int /*h*/,
                   float /*conf*/, float /*nms*/,
                   std::vector<Detection>& detections) override {
    detections = fake_detections_;
    return true;
  }

  std::string ModelFamily() const override { return "fake"; }
  TensorShape InputShape() const override { return {1, 3, 640, 640}; }

 private:
  std::vector<Detection> fake_detections_;
};

/// Helper: create a FakeInferenceEngine that returns specific detections.
static std::shared_ptr<InferenceEngine> MakeFakeEngine(
    std::vector<Detection> detections) {
  auto backend = std::make_unique<FakeBackend>();
  auto adapter = std::make_unique<FakeAdapter>();
  adapter->SetDetections(std::move(detections));
  return std::make_shared<InferenceEngine>(std::move(backend),
                                           std::move(adapter));
}

// ============================================================
// ModelCascade unit tests
// ============================================================

TEST(ModelCascade, EmptyReturnsFailure) {
  ModelCascade cascade;
  EXPECT_TRUE(cascade.Empty());
  EXPECT_EQ(cascade.StepCount(), 0U);

  AnalysisResult result;
  EXPECT_FALSE(cascade.Run(nullptr, 640, 480, result));
}

TEST(ModelCascade, SinglePrimaryStep) {
  ModelCascade cascade;

  CascadeStep step;
  step.name = "primary";
  step.model_name = "yolov8n";
  step.mode = CascadeMode::kPrimary;
  step.confidence_threshold = 0.5F;

  auto engine = MakeFakeEngine({
      {100, 200, 300, 400, 0.9F, 0, "person"},
      {500, 100, 700, 300, 0.8F, 1, "car"},
  });

  cascade.AddStep(step, engine);
  EXPECT_FALSE(cascade.Empty());
  EXPECT_EQ(cascade.StepCount(), 1U);

  std::vector<uint8_t> dummy_image(640 * 480 * 3, 128);
  AnalysisResult result;
  bool ok = cascade.Run(dummy_image.data(), 640, 480, result);

  EXPECT_TRUE(ok);
  EXPECT_TRUE(result.has_result);
  EXPECT_EQ(result.detections.size(), 2U);
  EXPECT_EQ(result.detections[0].class_name, "person");
  EXPECT_EQ(result.detections[1].class_name, "car");
  EXPECT_TRUE(result.secondary_results.empty());
  EXPECT_GT(result.inference_time_us, 0);
}

TEST(ModelCascade, ParallelStepMergesDetections) {
  ModelCascade cascade;

  CascadeStep primary_step;
  primary_step.name = "primary";
  primary_step.model_name = "yolov8n";
  primary_step.mode = CascadeMode::kPrimary;

  auto primary_engine = MakeFakeEngine({
      {100, 200, 300, 400, 0.9F, 0, "person"},
  });

  CascadeStep parallel_step;
  parallel_step.name = "fire_det";
  parallel_step.model_name = "fire_detector";
  parallel_step.mode = CascadeMode::kParallel;

  auto parallel_engine = MakeFakeEngine({
      {50, 50, 150, 150, 0.7F, 0, "fire"},
      {200, 200, 400, 400, 0.6F, 1, "smoke"},
  });

  cascade.AddStep(primary_step, primary_engine);
  cascade.AddStep(parallel_step, parallel_engine);

  EXPECT_EQ(cascade.StepCount(), 2U);

  std::vector<uint8_t> dummy_image(640 * 480 * 3, 128);
  AnalysisResult result;
  cascade.Run(dummy_image.data(), 640, 480, result);

  // Primary (1 det) + Parallel (2 dets) = 3 total
  EXPECT_EQ(result.detections.size(), 3U);
  EXPECT_EQ(result.detections[0].class_name, "person");
  EXPECT_EQ(result.detections[1].class_name, "fire");
  EXPECT_EQ(result.detections[2].class_name, "smoke");
}

TEST(ModelCascade, CropStepTriggeredByClass) {
  ModelCascade cascade;

  CascadeStep primary_step;
  primary_step.name = "primary";
  primary_step.model_name = "yolov8n";
  primary_step.mode = CascadeMode::kPrimary;

  auto primary_engine = MakeFakeEngine({
      {100, 100, 300, 300, 0.9F, 0, "vehicle"},
      {400, 400, 500, 500, 0.8F, 1, "person"},
  });

  CascadeStep crop_step;
  crop_step.name = "lpr";
  crop_step.model_name = "lpr_model";
  crop_step.mode = CascadeMode::kCrop;
  crop_step.trigger_classes = {"vehicle"};

  auto crop_engine = MakeFakeEngine({
      {10, 20, 80, 40, 0.95F, 0, "plate"},
  });

  cascade.AddStep(primary_step, primary_engine);
  cascade.AddStep(crop_step, crop_engine);

  std::vector<uint8_t> dummy_image(640 * 480 * 3, 128);
  AnalysisResult result;
  cascade.Run(dummy_image.data(), 640, 480, result);

  EXPECT_EQ(result.detections.size(), 2U);

  // Only "vehicle" should trigger the crop step, not "person"
  EXPECT_EQ(result.secondary_results.size(), 1U);
  EXPECT_EQ(result.secondary_results[0].parent_index, 0);
  EXPECT_EQ(result.secondary_results[0].model_name, "lpr_model");
  EXPECT_EQ(result.secondary_results[0].detections.size(), 1U);
  EXPECT_EQ(result.secondary_results[0].detections[0].class_name, "plate");
}

TEST(ModelCascade, CropStepAllClassesTrigger) {
  ModelCascade cascade;

  CascadeStep primary_step;
  primary_step.name = "primary";
  primary_step.model_name = "yolov8n";
  primary_step.mode = CascadeMode::kPrimary;

  auto primary_engine = MakeFakeEngine({
      {100, 100, 200, 200, 0.9F, 0, "cat"},
      {300, 300, 400, 400, 0.8F, 1, "dog"},
  });

  CascadeStep crop_step;
  crop_step.name = "classifier";
  crop_step.model_name = "breed_classifier";
  crop_step.mode = CascadeMode::kCrop;
  // Empty trigger_classes = all primary detections

  auto crop_engine = MakeFakeEngine({
      {0, 0, 50, 50, 0.7F, 0, "siamese"},
  });

  cascade.AddStep(primary_step, primary_engine);
  cascade.AddStep(crop_step, crop_engine);

  std::vector<uint8_t> dummy_image(640 * 480 * 3, 128);
  AnalysisResult result;
  cascade.Run(dummy_image.data(), 640, 480, result);

  // Both primary detections trigger the crop step
  EXPECT_EQ(result.secondary_results.size(), 2U);
  EXPECT_EQ(result.secondary_results[0].parent_index, 0);
  EXPECT_EQ(result.secondary_results[1].parent_index, 1);
}

TEST(ModelCascade, CropStepNoTriggerNoSecondary) {
  ModelCascade cascade;

  CascadeStep primary_step;
  primary_step.name = "primary";
  primary_step.model_name = "yolov8n";
  primary_step.mode = CascadeMode::kPrimary;

  auto primary_engine = MakeFakeEngine({
      {100, 100, 200, 200, 0.9F, 0, "person"},
  });

  CascadeStep crop_step;
  crop_step.name = "lpr";
  crop_step.model_name = "lpr_model";
  crop_step.mode = CascadeMode::kCrop;
  crop_step.trigger_classes = {"vehicle"};

  auto crop_engine = MakeFakeEngine({
      {10, 20, 80, 40, 0.95F, 0, "plate"},
  });

  cascade.AddStep(primary_step, primary_engine);
  cascade.AddStep(crop_step, crop_engine);

  std::vector<uint8_t> dummy_image(640 * 480 * 3, 128);
  AnalysisResult result;
  cascade.Run(dummy_image.data(), 640, 480, result);

  // No "vehicle" in primary detections → no secondary results
  EXPECT_EQ(result.detections.size(), 1U);
  EXPECT_TRUE(result.secondary_results.empty());
}

TEST(ModelCascade, StepsAccessor) {
  ModelCascade cascade;

  CascadeStep s1;
  s1.name = "s1";
  s1.mode = CascadeMode::kPrimary;

  CascadeStep s2;
  s2.name = "s2";
  s2.mode = CascadeMode::kCrop;
  s2.trigger_classes = {"vehicle"};

  cascade.AddStep(s1, MakeFakeEngine({}));
  cascade.AddStep(s2, MakeFakeEngine({}));

  const auto& steps = cascade.Steps();
  ASSERT_EQ(steps.size(), 2U);
  EXPECT_EQ(steps[0].name, "s1");
  EXPECT_EQ(steps[1].name, "s2");
  EXPECT_EQ(steps[0].mode, CascadeMode::kPrimary);
  EXPECT_EQ(steps[1].mode, CascadeMode::kCrop);
}

TEST(ModelCascade, CascadeModeToStringCoversAll) {
  EXPECT_STREQ(CascadeModeToString(CascadeMode::kPrimary), "primary");
  EXPECT_STREQ(CascadeModeToString(CascadeMode::kCrop), "crop");
  EXPECT_STREQ(CascadeModeToString(CascadeMode::kParallel), "parallel");
}

TEST(ModelCascade, FullCascadePrimaryParallelCrop) {
  ModelCascade cascade;

  CascadeStep primary;
  primary.name = "det";
  primary.mode = CascadeMode::kPrimary;
  auto primary_engine = MakeFakeEngine({
      {100, 100, 200, 200, 0.9F, 0, "car"},
  });

  CascadeStep parallel;
  parallel.name = "fire";
  parallel.mode = CascadeMode::kParallel;
  auto parallel_engine = MakeFakeEngine({
      {50, 50, 100, 100, 0.8F, 0, "flame"},
  });

  CascadeStep crop;
  crop.name = "lpr";
  crop.mode = CascadeMode::kCrop;
  crop.trigger_classes = {"car"};
  auto crop_engine = MakeFakeEngine({
      {5, 5, 30, 15, 0.95F, 0, "plate"},
  });

  cascade.AddStep(primary, primary_engine);
  cascade.AddStep(parallel, parallel_engine);
  cascade.AddStep(crop, crop_engine);

  std::vector<uint8_t> img(640 * 480 * 3, 0);
  AnalysisResult result;
  bool ok = cascade.Run(img.data(), 640, 480, result);

  EXPECT_TRUE(ok);
  EXPECT_EQ(result.detections.size(), 2U);         // car + flame
  EXPECT_EQ(result.secondary_results.size(), 1U);  // plate from car
  EXPECT_EQ(result.secondary_results[0].parent_index, 0);
}

// ============================================================
// CropRoi utility tests
// ============================================================

TEST(ModelCascade, CropRoiProducesCorrectDimensions) {
  // 10x10 image, 3 channels
  constexpr int kW = 10, kH = 10, kC = 3;
  std::vector<uint8_t> img(kW * kH * kC, 42);

  int crop_w = 0, crop_h = 0;
  auto cropped =
      ModelCascade::CropRoi(img.data(), kW, kH, kC, 2.0F, 3.0F, 7.0F, 8.0F,
                            0.0F,  // no expand
                            crop_w, crop_h);

  EXPECT_EQ(crop_w, 5);
  EXPECT_EQ(crop_h, 5);
  EXPECT_EQ(cropped.size(), static_cast<size_t>(crop_w * crop_h * kC));
}

TEST(ModelCascade, CropRoiClampsToImageBounds) {
  constexpr int kW = 100, kH = 100, kC = 3;
  std::vector<uint8_t> img(kW * kH * kC, 0);

  int crop_w = 0, crop_h = 0;
  auto cropped = ModelCascade::CropRoi(img.data(), kW, kH, kC, -20.0F, -10.0F,
                                       120.0F, 110.0F, 0.1F, crop_w, crop_h);

  EXPECT_EQ(crop_w, kW);
  EXPECT_EQ(crop_h, kH);
}

// ============================================================
// SecondaryResult structure tests
// ============================================================

TEST(SecondaryResult, DefaultValues) {
  SecondaryResult sec;
  EXPECT_EQ(sec.parent_index, -1);
  EXPECT_TRUE(sec.model_name.empty());
  EXPECT_TRUE(sec.detections.empty());
  EXPECT_TRUE(sec.attributes.empty());
}

TEST(SecondaryResult, AttributeStorage) {
  SecondaryResult sec;
  sec.parent_index = 0;
  sec.model_name = "classifier";
  sec.attributes["color"] = "red";
  sec.attributes["make"] = "Toyota";

  EXPECT_EQ(sec.attributes.size(), 2U);
  EXPECT_EQ(sec.attributes.at("color"), "red");
  EXPECT_EQ(sec.attributes.at("make"), "Toyota");
}

}  // namespace
}  // namespace loong::ai_engine
