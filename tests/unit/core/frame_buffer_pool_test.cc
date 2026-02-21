// Copyright 2026 Loong AI NVR Project

#include "core/memory_pool/frame_buffer_pool.h"

#include "gtest/gtest.h"

namespace loong {
namespace core {
namespace {

TEST(FrameBufferPool, CreateAndAcquire) {
  FrameBufferPool pool(4, 1024);
  EXPECT_EQ(pool.TotalCount(), 4u);
  EXPECT_EQ(pool.AvailableCount(), 4u);

  auto buf = pool.Acquire();
  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(buf->Size(), 1024u);
  EXPECT_EQ(pool.AvailableCount(), 3u);
}

TEST(FrameBufferPool, AcquireAndRelease) {
  FrameBufferPool pool(2, 512);

  {
    auto buf1 = pool.Acquire();
    auto buf2 = pool.Acquire();
    EXPECT_EQ(pool.AvailableCount(), 0u);

    auto buf3 = pool.Acquire();
    EXPECT_EQ(buf3, nullptr);
  }

  // After shared_ptrs go out of scope, buffers should be returned.
  EXPECT_EQ(pool.AvailableCount(), 2u);
}

TEST(FrameBufferPool, WriteAndReadData) {
  FrameBufferPool pool(1, 256);
  auto buf = pool.Acquire();
  ASSERT_NE(buf, nullptr);

  const char* test_data = "Hello, Loong AI NVR!";
  size_t len = strlen(test_data);
  memcpy(buf->Data(), test_data, len);
  buf->SetActualSize(len);

  EXPECT_EQ(buf->ActualSize(), len);
  EXPECT_EQ(memcmp(buf->Data(), test_data, len), 0);
}

}  // namespace
}  // namespace core
}  // namespace loong
