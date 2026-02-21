// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_LPR_PLATE_STORE_H_
#define LOONG_AI_ENGINE_LPR_PLATE_STORE_H_

#include <cstdint>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// A recognized license plate record.
struct PlateRecord {
  int64_t id = 0;
  std::string plate_number;
  std::string plate_color;  // blue_plate, green_plate, etc.
  float confidence = 0.0F;
  int channel_id = -1;
  int64_t timestamp = 0;      // Unix timestamp (ms)
  std::string snapshot_path;  // Path to plate snapshot image
};

/// Query parameters for searching plate records.
struct PlateQuery {
  std::string plate_number;  // Fuzzy match (LIKE %number%)
  int channel_id = -1;       // -1 = all channels
  int64_t start_time = 0;    // 0 = no lower bound
  int64_t end_time = 0;      // 0 = no upper bound
  int limit = 100;
  int offset = 0;
};

/// SQLite-based persistent storage for license plate records.
/// Thread-safe — all public methods are mutex-protected.
class PlateStore {
 public:
  PlateStore();
  ~PlateStore();

  bool Open(const std::string& db_path);
  void Close();

  /// Insert a new plate record. Returns the record ID, or -1 on failure.
  int64_t Insert(const PlateRecord& record);

  /// Query plate records with filters.
  std::vector<PlateRecord> Query(const PlateQuery& query);

  /// Get a single record by ID.
  PlateRecord GetById(int64_t id);

  /// Delete records older than the given timestamp.
  int PurgeOlderThan(int64_t timestamp_ms);

  /// Get total record count.
  int64_t Count();

  PlateStore(const PlateStore&) = delete;
  PlateStore& operator=(const PlateStore&) = delete;

 private:
  void CreateTables();

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_LPR_PLATE_STORE_H_
