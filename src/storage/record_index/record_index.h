// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_RECORD_INDEX_RECORD_INDEX_H_
#define LOONG_STORAGE_RECORD_INDEX_RECORD_INDEX_H_

#include <cstdint>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loong::storage {

/// A recording segment metadata entry.
struct SegmentInfo {
  int64_t id = 0;
  int channel_id = 0;
  int64_t start_time = 0;  // Unix timestamp (ms)
  int64_t end_time = 0;
  std::string file_path;
  int64_t file_size = 0;
  std::string codec;  // "h264" | "h265"
  std::string resolution;
  bool has_ai_overlay = false;
};

/// A recording event metadata entry.
struct EventInfo {
  int64_t id = 0;
  int64_t segment_id = 0;
  int channel_id = 0;
  std::string event_type;
  int64_t event_time = 0;
  float confidence = 0.0F;
  std::string metadata;  // JSON
};

/// Storage quota for a channel.
struct StorageQuota {
  int channel_id = 0;
  int max_days = 30;
  double max_size_gb = 100.0;
  double current_size_gb = 0.0;
  std::string policy = "circular";  // "circular" | "stop_when_full"
};

/// SQLite-based recording index database.
/// Provides efficient querying by channel, time range, and events.
class RecordIndex {
 public:
  RecordIndex();
  ~RecordIndex();

  /// Open/create the index database.
  bool Open(const std::string& db_path);

  /// Close the database.
  void Close();

  /// Insert a new segment record. Returns segment ID.
  int64_t InsertSegment(const SegmentInfo& info);

  /// Update segment end_time and file_size after recording ends.
  bool UpdateSegmentEnd(int64_t segment_id, int64_t end_time,
                        int64_t file_size);

  /// Insert an event record.
  int64_t InsertEvent(const EventInfo& event);

  /// Query segments by channel and time range.
  std::vector<SegmentInfo> QuerySegments(int channel_id, int64_t start_time,
                                         int64_t end_time);

  /// Query events by channel and time range.
  std::vector<EventInfo> QueryEvents(int channel_id, int64_t start_time,
                                     int64_t end_time);

  /// Get total size of recordings for a channel (bytes).
  int64_t GetChannelTotalSize(int channel_id);

  /// Get oldest segments for a channel (for cleanup).
  std::vector<SegmentInfo> GetOldestSegments(int channel_id, int count);

  /// Delete a segment record and return its file path for deletion.
  std::string DeleteSegment(int64_t segment_id);

  /// Set/update storage quota for a channel.
  bool SetQuota(const StorageQuota& quota);

  /// Get storage quota for a channel.
  StorageQuota GetQuota(int channel_id);

  /// Get all channels that have quotas configured.
  std::vector<StorageQuota> GetAllChannelsWithQuotas();

  // Non-copyable
  RecordIndex(const RecordIndex&) = delete;
  RecordIndex& operator=(const RecordIndex&) = delete;

 private:
  bool CreateTables();
  sqlite3* db_ = nullptr;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_RECORD_INDEX_RECORD_INDEX_H_
