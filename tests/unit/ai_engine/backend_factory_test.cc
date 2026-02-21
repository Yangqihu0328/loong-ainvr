// Copyright 2026 Loong AI NVR Project

#include "ai_engine/backend/backend_factory.h"

#include <gtest/gtest.h>

namespace loong {
namespace ai_engine {
namespace {

TEST(BackendFactoryTest, CreateOpenCVDnn) {
  auto backend = BackendFactory::Create("opencv_dnn");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->Name(), "OpenCVDNN");
  EXPECT_FALSE(backend->SupportsBatch());
  EXPECT_EQ(backend->MaxBatchSize(), 1);
}

TEST(BackendFactoryTest, CreateOpenCVAlias) {
  auto b1 = BackendFactory::Create("opencv");
  ASSERT_NE(b1, nullptr);
  EXPECT_EQ(b1->Name(), "OpenCVDNN");

  auto b2 = BackendFactory::Create("dnn");
  ASSERT_NE(b2, nullptr);
  EXPECT_EQ(b2->Name(), "OpenCVDNN");
}

TEST(BackendFactoryTest, UnknownBackendFallsBack) {
  auto backend = BackendFactory::Create("nonexistent_backend");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->Name(), "OpenCVDNN");
}

TEST(BackendFactoryTest, TensorRTFallbackWhenNotCompiled) {
#ifndef LOONG_HAS_TENSORRT
  auto backend = BackendFactory::Create("tensorrt");
  ASSERT_NE(backend, nullptr);
  // Falls back to OpenCVDNN.
  EXPECT_EQ(backend->Name(), "OpenCVDNN");

  auto b2 = BackendFactory::Create("trt");
  ASSERT_NE(b2, nullptr);
  EXPECT_EQ(b2->Name(), "OpenCVDNN");
#else
  auto backend = BackendFactory::Create("tensorrt");
  ASSERT_NE(backend, nullptr);
  EXPECT_EQ(backend->Name(), "tensorrt");
#endif
}

}  // namespace
}  // namespace ai_engine
}  // namespace loong
