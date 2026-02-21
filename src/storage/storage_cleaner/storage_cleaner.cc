// Copyright 2026 Loong AI NVR Project

#include "storage/storage_cleaner/storage_cleaner.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <filesystem>

namespace loong::storage {

StorageCleaner::StorageCleaner(std::shared_ptr<RecordIndex> index)
    : index_(std::move(index)) {}

StorageCleaner::~StorageCleaner() { Stop(); }

void StorageCleaner::Start() {
  if (running_) return;
  running_ = true;
  cleanup_thread_ = std::thread(&StorageCleaner::CleanupLoop, this);
  spdlog::info("StorageCleaner: started");
}

void StorageCleaner::Stop() {
  running_ = false;
  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }
}

void StorageCleaner::RunCleanup() {
  spdlog::info("StorageCleaner: cleanup pass started");

  auto quotas = index_->GetAllChannelsWithQuotas();
  if (quotas.empty()) {
    spdlog::debug("StorageCleaner: no channels with quotas configured");
    return;
  }

  for (const auto& quota : quotas) {
    CleanChannel(quota.channel_id, quota);
  }

  spdlog::info("StorageCleaner: cleanup pass completed ({} channels checked)",
               quotas.size());
}

void StorageCleaner::CleanupLoop() {
  while (running_) {
    RunCleanup();

    // Sleep in small increments so we can detect stop quickly
    for (int i = 0; i < check_interval_sec_ && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void StorageCleaner::CleanChannel(int channel_id, const StorageQuota& quota) {
  // Check current size
  int64_t total_bytes = index_->GetChannelTotalSize(channel_id);
  double total_gb =
      static_cast<double>(total_bytes) / (1024.0 * 1024.0 * 1024.0);

  if (total_gb > quota.max_size_gb * 0.9) {
    spdlog::info("StorageCleaner: ch{} at {:.1f}GB / {:.1f}GB, cleaning",
                 channel_id, total_gb, quota.max_size_gb);

    // Delete oldest segments until under 80%
    double target_gb = quota.max_size_gb * 0.8;
    while (total_gb > target_gb) {
      auto oldest = index_->GetOldestSegments(channel_id, 5);
      if (oldest.empty()) break;

      for (const auto& seg : oldest) {
        std::string path = index_->DeleteSegment(seg.id);
        if (!path.empty()) {
          std::error_code ec;
          std::filesystem::remove(path, ec);
          if (!ec) {
            total_gb -=
                static_cast<double>(seg.file_size) / (1024.0 * 1024.0 * 1024.0);
          }
        }
      }
    }
  }

  // Check age-based cleanup
  auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  int64_t cutoff_ms =
      now_ms - static_cast<int64_t>(quota.max_days) * 86400LL * 1000LL;

  auto old_segments = index_->QuerySegments(channel_id, 0, cutoff_ms);
  for (const auto& seg : old_segments) {
    std::string path = index_->DeleteSegment(seg.id);
    if (!path.empty()) {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  }
}

}  // namespace loong::storage
