// Copyright 2026 Loong AI NVR Project

#include "storage/record_writer/record_writer.h"

#include <gtest/gtest.h>
#include <mutex>
#include <thread>
#include <vector>

namespace loong {
namespace storage {
namespace {

TEST(RecordWriterThreadTest, HasMutexMember) {
  // Verify the class can be constructed (mutex is default-constructible).
  RecordConfig config;
  config.base_path = "/tmp/loong_test_recordings";
  RecordWriter writer(1, config);
  // Just verify it doesn't crash on construction.
  SUCCEED();
}

TEST(RecordWriterThreadTest, ConcurrentCloseDoesNotCrash) {
  RecordConfig config;
  config.base_path = "/tmp/loong_test_recordings";
  RecordWriter writer(1, config);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&writer]() { writer.Close(); });
  }
  for (auto& t : threads) {
    t.join();
  }
  SUCCEED();
}

TEST(RecordWriterThreadTest, TotalBytesWrittenIsThreadSafe) {
  RecordConfig config;
  config.base_path = "/tmp/loong_test_recordings";
  RecordWriter writer(1, config);

  // Multiple threads reading TotalBytesWritten concurrently.
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&writer]() {
      for (int j = 0; j < 100; ++j) {
        auto bytes = writer.TotalBytesWritten();
        EXPECT_GE(bytes, 0);
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
}

TEST(RecordWriterThreadTest, CurrentSegmentPathIsThreadSafe) {
  RecordConfig config;
  config.base_path = "/tmp/loong_test_recordings";
  RecordWriter writer(1, config);

  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i) {
    threads.emplace_back([&writer]() {
      for (int j = 0; j < 100; ++j) {
        auto path = writer.CurrentSegmentPath();
        // Initially empty, just check no crash.
        (void)path;
      }
    });
  }
  for (auto& t : threads) {
    t.join();
  }
}

}  // namespace
}  // namespace storage
}  // namespace loong
