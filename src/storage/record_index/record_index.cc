// Copyright 2026 Loong AI NVR Project

#include "storage/record_index/record_index.h"

#include "spdlog/spdlog.h"

namespace loong::storage {

RecordIndex::RecordIndex() = default;

RecordIndex::~RecordIndex() { Close(); }

bool RecordIndex::Open(const std::string& db_path) {
  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("RecordIndex: failed to open db '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // Enable WAL mode for better concurrent performance
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  if (!CreateTables()) {
    Close();
    return false;
  }

  spdlog::info("RecordIndex: opened '{}'", db_path);
  return true;
}

void RecordIndex::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

int64_t RecordIndex::InsertSegment(const SegmentInfo& info) {
  const char* sql =
      "INSERT INTO recording_segments "
      "(channel_id, start_time, end_time, file_path, file_size, "
      "codec, resolution, has_ai_overlay, created_at) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now')*1000);";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("RecordIndex: prepare insert failed");
    return -1;
  }

  sqlite3_bind_int(stmt, 1, info.channel_id);
  sqlite3_bind_int64(stmt, 2, info.start_time);
  sqlite3_bind_int64(stmt, 3, info.end_time);
  sqlite3_bind_text(stmt, 4, info.file_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, info.file_size);
  sqlite3_bind_text(stmt, 6, info.codec.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, info.resolution.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, info.has_ai_overlay ? 1 : 0);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    spdlog::error("RecordIndex: insert segment failed");
    return -1;
  }

  return sqlite3_last_insert_rowid(db_);
}

bool RecordIndex::UpdateSegmentEnd(int64_t segment_id, int64_t end_time,
                                   int64_t file_size) {
  const char* sql =
      "UPDATE recording_segments SET end_time=?, file_size=? WHERE id=?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, end_time);
  sqlite3_bind_int64(stmt, 2, file_size);
  sqlite3_bind_int64(stmt, 3, segment_id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

int64_t RecordIndex::InsertEvent(const EventInfo& event) {
  const char* sql =
      "INSERT INTO recording_events "
      "(segment_id, channel_id, event_type, event_time, confidence, metadata) "
      "VALUES (?, ?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_int64(stmt, 1, event.segment_id);
  sqlite3_bind_int(stmt, 2, event.channel_id);
  sqlite3_bind_text(stmt, 3, event.event_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, event.event_time);
  sqlite3_bind_double(stmt, 5, static_cast<double>(event.confidence));
  sqlite3_bind_text(stmt, 6, event.metadata.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(db_) : -1;
}

std::vector<SegmentInfo> RecordIndex::QuerySegments(int channel_id,
                                                    int64_t start_time,
                                                    int64_t end_time) {
  const char* sql =
      "SELECT id, channel_id, start_time, end_time, file_path, file_size, "
      "codec, resolution, has_ai_overlay "
      "FROM recording_segments "
      "WHERE channel_id=? AND start_time<=? AND end_time>=? "
      "ORDER BY start_time ASC;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<SegmentInfo> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int64(stmt, 2, end_time);
  sqlite3_bind_int64(stmt, 3, start_time);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SegmentInfo info;
    info.id = sqlite3_column_int64(stmt, 0);
    info.channel_id = sqlite3_column_int(stmt, 1);
    info.start_time = sqlite3_column_int64(stmt, 2);
    info.end_time = sqlite3_column_int64(stmt, 3);
    const auto* fp =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.file_path = fp ? fp : "";
    info.file_size = sqlite3_column_int64(stmt, 5);
    const auto* cd =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    info.codec = cd ? cd : "";
    const auto* rs =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    info.resolution = rs ? rs : "";
    info.has_ai_overlay = sqlite3_column_int(stmt, 8) != 0;
    results.push_back(info);
  }

  sqlite3_finalize(stmt);
  return results;
}

std::vector<EventInfo> RecordIndex::QueryEvents(int channel_id,
                                                int64_t start_time,
                                                int64_t end_time) {
  const char* sql =
      "SELECT id, segment_id, channel_id, event_type, event_time, "
      "confidence, metadata "
      "FROM recording_events "
      "WHERE channel_id=? AND event_time>=? AND event_time<=? "
      "ORDER BY event_time ASC;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<EventInfo> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int64(stmt, 2, start_time);
  sqlite3_bind_int64(stmt, 3, end_time);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    EventInfo e;
    e.id = sqlite3_column_int64(stmt, 0);
    e.segment_id = sqlite3_column_int64(stmt, 1);
    e.channel_id = sqlite3_column_int(stmt, 2);
    const auto* et =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    e.event_type = et ? et : "";
    e.event_time = sqlite3_column_int64(stmt, 4);
    e.confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    const auto* md =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    e.metadata = md ? md : "";
    results.push_back(e);
  }

  sqlite3_finalize(stmt);
  return results;
}

int64_t RecordIndex::GetChannelTotalSize(int channel_id) {
  const char* sql =
      "SELECT COALESCE(SUM(file_size), 0) FROM recording_segments "
      "WHERE channel_id=?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  int64_t total = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    total = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return total;
}

std::vector<SegmentInfo> RecordIndex::GetOldestSegments(int channel_id,
                                                        int count) {
  const char* sql =
      "SELECT id, channel_id, start_time, end_time, file_path, file_size, "
      "codec, resolution, has_ai_overlay "
      "FROM recording_segments "
      "WHERE channel_id=? ORDER BY start_time ASC LIMIT ?;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<SegmentInfo> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int(stmt, 2, count);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    SegmentInfo info;
    info.id = sqlite3_column_int64(stmt, 0);
    info.channel_id = sqlite3_column_int(stmt, 1);
    info.start_time = sqlite3_column_int64(stmt, 2);
    info.end_time = sqlite3_column_int64(stmt, 3);
    const auto* fp =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    info.file_path = fp ? fp : "";
    info.file_size = sqlite3_column_int64(stmt, 5);
    results.push_back(info);
  }

  sqlite3_finalize(stmt);
  return results;
}

std::string RecordIndex::DeleteSegment(int64_t segment_id) {
  // First get file path
  const char* select_sql =
      "SELECT file_path FROM recording_segments WHERE id=?;";
  sqlite3_stmt* stmt = nullptr;
  std::string path;

  if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, segment_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      const auto* fp =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      path = fp ? fp : "";
    }
    sqlite3_finalize(stmt);
  }

  // Delete events for this segment
  const char* del_events = "DELETE FROM recording_events WHERE segment_id=?;";
  stmt = nullptr;
  if (sqlite3_prepare_v2(db_, del_events, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, segment_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  // Delete segment
  const char* del_seg = "DELETE FROM recording_segments WHERE id=?;";
  stmt = nullptr;
  if (sqlite3_prepare_v2(db_, del_seg, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, segment_id);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
  }

  return path;
}

bool RecordIndex::SetQuota(const StorageQuota& quota) {
  const char* sql =
      "INSERT OR REPLACE INTO storage_quota "
      "(channel_id, max_days, max_size_gb, current_size_gb, policy) "
      "VALUES (?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, quota.channel_id);
  sqlite3_bind_int(stmt, 2, quota.max_days);
  sqlite3_bind_double(stmt, 3, quota.max_size_gb);
  sqlite3_bind_double(stmt, 4, quota.current_size_gb);
  sqlite3_bind_text(stmt, 5, quota.policy.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

StorageQuota RecordIndex::GetQuota(int channel_id) {
  const char* sql =
      "SELECT max_days, max_size_gb, current_size_gb, policy "
      "FROM storage_quota WHERE channel_id=?;";

  StorageQuota quota;
  quota.channel_id = channel_id;

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return quota;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    quota.max_days = sqlite3_column_int(stmt, 0);
    quota.max_size_gb = sqlite3_column_double(stmt, 1);
    quota.current_size_gb = sqlite3_column_double(stmt, 2);
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    quota.policy = p ? p : "circular";
  }
  sqlite3_finalize(stmt);
  return quota;
}

std::vector<StorageQuota> RecordIndex::GetAllChannelsWithQuotas() {
  const char* sql =
      "SELECT channel_id, max_days, max_size_gb, current_size_gb, policy "
      "FROM storage_quota;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<StorageQuota> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    StorageQuota q;
    q.channel_id = sqlite3_column_int(stmt, 0);
    q.max_days = sqlite3_column_int(stmt, 1);
    q.max_size_gb = sqlite3_column_double(stmt, 2);
    q.current_size_gb = sqlite3_column_double(stmt, 3);
    const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    q.policy = p ? p : "circular";
    results.push_back(q);
  }

  sqlite3_finalize(stmt);
  return results;
}

bool RecordIndex::CreateTables() {
  const char* schema = R"(
    CREATE TABLE IF NOT EXISTS recording_segments (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      channel_id INTEGER NOT NULL,
      start_time INTEGER NOT NULL,
      end_time INTEGER NOT NULL,
      file_path TEXT NOT NULL,
      file_size INTEGER DEFAULT 0,
      codec TEXT,
      resolution TEXT,
      has_ai_overlay INTEGER DEFAULT 0,
      created_at INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_seg_channel_time
      ON recording_segments(channel_id, start_time);
    CREATE INDEX IF NOT EXISTS idx_seg_time
      ON recording_segments(start_time);

    CREATE TABLE IF NOT EXISTS recording_events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      segment_id INTEGER REFERENCES recording_segments(id),
      channel_id INTEGER NOT NULL,
      event_type TEXT NOT NULL,
      event_time INTEGER NOT NULL,
      confidence REAL,
      metadata TEXT
    );

    CREATE INDEX IF NOT EXISTS idx_evt_channel_time
      ON recording_events(channel_id, event_time);

    CREATE TABLE IF NOT EXISTS storage_quota (
      channel_id INTEGER PRIMARY KEY,
      max_days INTEGER DEFAULT 30,
      max_size_gb REAL DEFAULT 100,
      current_size_gb REAL DEFAULT 0,
      policy TEXT DEFAULT 'circular'
    );
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    spdlog::error("RecordIndex: create tables failed: {}",
                  err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return false;
  }

  return true;
}

}  // namespace loong::storage
