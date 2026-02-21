// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_STORAGE_CLEANER_STORAGE_CLEANER_H_
#define LOONG_STORAGE_STORAGE_CLEANER_STORAGE_CLEANER_H_

#include "storage/record_index/record_index.h"

#include <atomic>
#include <memory>
#include <thread>

namespace loong::storage {

/// Periodically cleans up old recordings based on storage quotas.
/// Runs in a background thread, checking every hour.
class StorageCleaner {
 public:
  explicit StorageCleaner(std::shared_ptr<RecordIndex> index);
  ~StorageCleaner();

  /// Start the cleanup loop.
  void Start();

  /// Stop the cleanup loop.
  void Stop();

  /// Run a single cleanup pass (for testing or manual trigger).
  void RunCleanup();

 private:
  void CleanupLoop();
  void CleanChannel(int channel_id, const StorageQuota& quota);

  std::shared_ptr<RecordIndex> index_;
  std::atomic<bool> running_{false};
  std::thread cleanup_thread_;
  int check_interval_sec_ = 3600;  // 1 hour
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_STORAGE_CLEANER_STORAGE_CLEANER_H_
