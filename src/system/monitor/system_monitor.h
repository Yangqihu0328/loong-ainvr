// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_MONITOR_SYSTEM_MONITOR_H_
#define LOONG_SYSTEM_MONITOR_SYSTEM_MONITOR_H_

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace loong::system {

/// Disk usage information for a single mount point.
struct DiskInfo {
  std::string path;
  int64_t total_gb = 0;
  int64_t free_gb = 0;
  double usage_percent = 0.0;
};

/// Snapshot of system resource metrics.
struct SystemMetrics {
  // CPU
  double cpu_usage_percent = 0.0;

  // Memory
  int64_t memory_total_mb = 0;
  int64_t memory_used_mb = 0;
  double memory_usage_percent = 0.0;

  // Disk
  std::vector<DiskInfo> disks;

  // GPU (optional — requires NVML)
  bool gpu_available = false;
  double gpu_usage_percent = 0.0;
  int64_t gpu_memory_used_mb = 0;
  int64_t gpu_memory_total_mb = 0;
  double gpu_temp_celsius = 0.0;

  // System
  int64_t uptime_seconds = 0;
  int process_threads = 0;
};

/// Periodically collects system resource metrics (CPU, memory, disk, GPU).
/// Publishes snapshots via EventBus under "system.metrics".
class SystemMonitor {
 public:
  SystemMonitor();
  ~SystemMonitor();

  /// Start the collection loop.
  /// @param interval_seconds  How often to sample (default 5 s).
  void Start(int interval_seconds = 5);

  /// Stop the collection loop.
  void Stop();

  /// Get the most recently collected metrics (thread-safe).
  SystemMetrics GetLatest() const;

  /// Add a disk mount path to monitor (e.g. "/", "/recordings").
  void AddDiskPath(const std::string& path);

  // Non-copyable
  SystemMonitor(const SystemMonitor&) = delete;
  SystemMonitor& operator=(const SystemMonitor&) = delete;

 private:
  void CollectLoop();
  void CollectCpu(SystemMetrics& m);
  void CollectMemory(SystemMetrics& m);
  void CollectDisk(SystemMetrics& m);
  void CollectGpu(SystemMetrics& m);
  void CollectUptime(SystemMetrics& m);
  void CollectThreads(SystemMetrics& m);

  mutable std::mutex mutex_;
  SystemMetrics latest_;
  std::vector<std::string> disk_paths_;

  std::atomic<bool> running_{false};
  int interval_seconds_ = 5;
  std::thread worker_;

  // CPU sampling state (previous values for delta calculation)
  int64_t prev_cpu_total_ = 0;
  int64_t prev_cpu_idle_ = 0;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_MONITOR_SYSTEM_MONITOR_H_
