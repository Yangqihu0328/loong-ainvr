// Copyright 2026 Loong AI NVR Project

#include "ai_engine/lpr/plate_store.h"

#include "spdlog/spdlog.h"

namespace loong::ai_engine {

PlateStore::PlateStore() = default;

PlateStore::~PlateStore() { Close(); }

bool PlateStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("PlateStore: failed to open '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  CreateTables();
  spdlog::info("PlateStore: opened '{}'", db_path);
  return true;
}

void PlateStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void PlateStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS plates (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      plate_number    TEXT NOT NULL,
      plate_color     TEXT DEFAULT '',
      confidence      REAL DEFAULT 0,
      channel_id      INTEGER DEFAULT -1,
      timestamp       INTEGER DEFAULT 0,
      snapshot_path   TEXT DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_plates_number ON plates(plate_number);
    CREATE INDEX IF NOT EXISTS idx_plates_timestamp ON plates(timestamp);
    CREATE INDEX IF NOT EXISTS idx_plates_channel ON plates(channel_id);
  )";

  char* err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    spdlog::error("PlateStore: table creation failed: {}", err ? err : "");
    sqlite3_free(err);
  }
}

int64_t PlateStore::Insert(const PlateRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return -1;

  const char* sql =
      "INSERT INTO plates (plate_number, plate_color, confidence, "
      "channel_id, timestamp, snapshot_path) VALUES (?, ?, ?, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, record.plate_number.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, record.plate_color.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 3, static_cast<double>(record.confidence));
  sqlite3_bind_int(stmt, 4, record.channel_id);
  sqlite3_bind_int64(stmt, 5, record.timestamp);
  sqlite3_bind_text(stmt, 6, record.snapshot_path.c_str(), -1,
                    SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) return -1;

  int64_t id = sqlite3_last_insert_rowid(db_);
  spdlog::debug("PlateStore: inserted plate '{}' id={}", record.plate_number,
                id);
  return id;
}

std::vector<PlateRecord> PlateStore::Query(const PlateQuery& query) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PlateRecord> results;
  if (!db_) return results;

  std::string sql =
      "SELECT id, plate_number, plate_color, confidence, "
      "channel_id, timestamp, snapshot_path FROM plates WHERE 1=1";

  if (!query.plate_number.empty()) {
    sql += " AND plate_number LIKE ?";
  }
  if (query.channel_id >= 0) {
    sql += " AND channel_id = ?";
  }
  if (query.start_time > 0) {
    sql += " AND timestamp >= ?";
  }
  if (query.end_time > 0) {
    sql += " AND timestamp <= ?";
  }

  sql += " ORDER BY timestamp DESC LIMIT ? OFFSET ?";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int param = 1;
  if (!query.plate_number.empty()) {
    std::string like = "%" + query.plate_number + "%";
    sqlite3_bind_text(stmt, param++, like.c_str(), -1, SQLITE_TRANSIENT);
  }
  if (query.channel_id >= 0) {
    sqlite3_bind_int(stmt, param++, query.channel_id);
  }
  if (query.start_time > 0) {
    sqlite3_bind_int64(stmt, param++, query.start_time);
  }
  if (query.end_time > 0) {
    sqlite3_bind_int64(stmt, param++, query.end_time);
  }
  sqlite3_bind_int(stmt, param++, query.limit);
  sqlite3_bind_int(stmt, param, query.offset);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    PlateRecord r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.plate_number =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* color =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.plate_color = color ? color : "";
    r.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    r.channel_id = sqlite3_column_int(stmt, 4);
    r.timestamp = sqlite3_column_int64(stmt, 5);
    const char* snap =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    r.snapshot_path = snap ? snap : "";
    results.push_back(std::move(r));
  }

  sqlite3_finalize(stmt);
  return results;
}

PlateRecord PlateStore::GetById(int64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  PlateRecord r;
  if (!db_) return r;

  const char* sql =
      "SELECT id, plate_number, plate_color, confidence, channel_id, "
      "timestamp, snapshot_path FROM plates WHERE id = ?";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return r;
  }

  sqlite3_bind_int64(stmt, 1, id);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    r.id = sqlite3_column_int64(stmt, 0);
    r.plate_number =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    const char* color =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    r.plate_color = color ? color : "";
    r.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    r.channel_id = sqlite3_column_int(stmt, 4);
    r.timestamp = sqlite3_column_int64(stmt, 5);
    const char* snap =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    r.snapshot_path = snap ? snap : "";
  }

  sqlite3_finalize(stmt);
  return r;
}

int PlateStore::PurgeOlderThan(int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "DELETE FROM plates WHERE timestamp < ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_int64(stmt, 1, timestamp_ms);
  sqlite3_step(stmt);
  int deleted = sqlite3_changes(db_);
  sqlite3_finalize(stmt);

  if (deleted > 0) {
    spdlog::info("PlateStore: purged {} old records", deleted);
  }
  return deleted;
}

int64_t PlateStore::Count() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "SELECT COUNT(*) FROM plates";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int64_t count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int64(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

}  // namespace loong::ai_engine
