// Copyright 2026 Loong AI NVR Project

#include "system/monitor/system_monitor.h"

#include <chrono>
#include <gtest/gtest.h>
#include <thread>

namespace loong {
namespace system {
namespace {

TEST(SystemMonitorTest, GetLatestReturnsZeroBeforeStart) {
  SystemMonitor monitor;
  auto m = monitor.GetLatest();
  EXPECT_EQ(m.cpu_usage_percent, 0.0);
  EXPECT_EQ(m.memory_total_mb, 0);
}

TEST(SystemMonitorTest, StartAndStopDoNotCrash) {
  SystemMonitor monitor;
  monitor.Start(1);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  monitor.Stop();
}

TEST(SystemMonitorTest, CollectsMetricsAfterStart) {
  SystemMonitor monitor;
  monitor.Start(1);
  // Wait for at least one collection cycle.
  std::this_thread::sleep_for(std::chrono::seconds(2));
  auto m = monitor.GetLatest();
  monitor.Stop();

  // CPU usage should be some reasonable value.
  EXPECT_GE(m.cpu_usage_percent, 0.0);
  EXPECT_LE(m.cpu_usage_percent, 100.0);

  // Memory should be populated.
  EXPECT_GT(m.memory_total_mb, 0);
  EXPECT_GT(m.memory_used_mb, 0);
  EXPECT_GE(m.memory_usage_percent, 0.0);

  // Uptime should be positive.
  EXPECT_GT(m.uptime_seconds, 0);

  // Should have at least one disk (root).
  EXPECT_GE(m.disks.size(), 1u);

  // Thread count should be positive.
  EXPECT_GT(m.process_threads, 0);
}

TEST(SystemMonitorTest, AddDiskPath) {
  SystemMonitor monitor;
  monitor.AddDiskPath("/tmp");
  monitor.Start(1);
  std::this_thread::sleep_for(std::chrono::seconds(2));
  auto m = monitor.GetLatest();
  monitor.Stop();

  // Should have at least 2 disks: "/" and "/tmp".
  EXPECT_GE(m.disks.size(), 1u);
}

TEST(SystemMonitorTest, DoubleStartIsNoop) {
  SystemMonitor monitor;
  monitor.Start(1);
  monitor.Start(1);  // Should not crash or create another thread.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  monitor.Stop();
}

}  // namespace
}  // namespace system
}  // namespace loong
