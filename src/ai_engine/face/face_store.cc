// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_store.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace loong::ai_engine {

FaceStore::FaceStore() = default;

FaceStore::~FaceStore() { Close(); }

bool FaceStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("FaceStore: failed to open '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  CreateTables();
  spdlog::info("FaceStore: opened '{}'", db_path);
  return true;
}

void FaceStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void FaceStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS faces (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      channel_id      INTEGER DEFAULT -1,
      timestamp       INTEGER DEFAULT 0,
      confidence      REAL DEFAULT 0,
      age             INTEGER DEFAULT -1,
      gender          TEXT DEFAULT 'unknown',
      embedding       BLOB,
      snapshot_path   TEXT DEFAULT ''
    );
    CREATE INDEX IF NOT EXISTS idx_faces_timestamp ON faces(timestamp);
    CREATE INDEX IF NOT EXISTS idx_faces_channel ON faces(channel_id);
    CREATE INDEX IF NOT EXISTS idx_faces_gender ON faces(gender);
    CREATE INDEX IF NOT EXISTS idx_faces_age ON faces(age);
  )";

  char* err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    spdlog::error("FaceStore: table creation failed: {}", err ? err : "");
    sqlite3_free(err);
  }
}

int64_t FaceStore::Insert(const FaceRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return -1;

  const char* sql =
      "INSERT INTO faces (channel_id, timestamp, confidence, age, gender, "
      "embedding, snapshot_path) VALUES (?, ?, ?, ?, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_int(stmt, 1, record.channel_id);
  sqlite3_bind_int64(stmt, 2, record.timestamp);
  sqlite3_bind_double(stmt, 3, static_cast<double>(record.confidence));
  sqlite3_bind_int(stmt, 4, record.age);
  sqlite3_bind_text(stmt, 5, record.gender.c_str(), -1, SQLITE_TRANSIENT);

  if (!record.embedding.empty()) {
    sqlite3_bind_blob(stmt, 6, record.embedding.data(),
                      static_cast<int>(record.embedding.size() * sizeof(float)),
                      SQLITE_TRANSIENT);
  } else {
    sqlite3_bind_null(stmt, 6);
  }

  sqlite3_bind_text(stmt, 7, record.snapshot_path.c_str(), -1,
                    SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) return -1;

  int64_t id = sqlite3_last_insert_rowid(db_);
  spdlog::debug("FaceStore: inserted face id={}", id);
  return id;
}

std::vector<FaceRecord> FaceStore::Query(const FaceQuery& query) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<FaceRecord> results;
  if (!db_) return results;

  std::string sql =
      "SELECT id, channel_id, timestamp, confidence, age, gender, "
      "embedding, snapshot_path FROM faces WHERE 1=1";

  if (query.channel_id >= 0) {
    sql += " AND channel_id = ?";
  }
  if (query.start_time > 0) {
    sql += " AND timestamp >= ?";
  }
  if (query.end_time > 0) {
    sql += " AND timestamp <= ?";
  }
  if (!query.gender.empty()) {
    sql += " AND gender = ?";
  }
  if (query.age_min >= 0) {
    sql += " AND age >= ?";
  }
  if (query.age_max >= 0) {
    sql += " AND age <= ?";
  }

  sql += " ORDER BY timestamp DESC LIMIT ? OFFSET ?";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int param = 1;
  if (query.channel_id >= 0) {
    sqlite3_bind_int(stmt, param++, query.channel_id);
  }
  if (query.start_time > 0) {
    sqlite3_bind_int64(stmt, param++, query.start_time);
  }
  if (query.end_time > 0) {
    sqlite3_bind_int64(stmt, param++, query.end_time);
  }
  if (!query.gender.empty()) {
    sqlite3_bind_text(stmt, param++, query.gender.c_str(), -1,
                      SQLITE_TRANSIENT);
  }
  if (query.age_min >= 0) {
    sqlite3_bind_int(stmt, param++, query.age_min);
  }
  if (query.age_max >= 0) {
    sqlite3_bind_int(stmt, param++, query.age_max);
  }
  sqlite3_bind_int(stmt, param++, query.limit);
  sqlite3_bind_int(stmt, param, query.offset);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    FaceRecord r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.channel_id = sqlite3_column_int(stmt, 1);
    r.timestamp = sqlite3_column_int64(stmt, 2);
    r.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    r.age = sqlite3_column_int(stmt, 4);
    const char* g = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    r.gender = g ? g : "unknown";

    const void* blob = sqlite3_column_blob(stmt, 6);
    int blob_bytes = sqlite3_column_bytes(stmt, 6);
    r.embedding = BlobToEmbedding(blob, blob_bytes);

    const char* snap =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.snapshot_path = snap ? snap : "";
    results.push_back(std::move(r));
  }

  sqlite3_finalize(stmt);
  return results;
}

FaceRecord FaceStore::GetById(int64_t id) {
  std::lock_guard<std::mutex> lock(mutex_);
  FaceRecord r;
  if (!db_) return r;

  const char* sql =
      "SELECT id, channel_id, timestamp, confidence, age, gender, "
      "embedding, snapshot_path FROM faces WHERE id = ?";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return r;
  }

  sqlite3_bind_int64(stmt, 1, id);

  if (sqlite3_step(stmt) == SQLITE_ROW) {
    r.id = sqlite3_column_int64(stmt, 0);
    r.channel_id = sqlite3_column_int(stmt, 1);
    r.timestamp = sqlite3_column_int64(stmt, 2);
    r.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    r.age = sqlite3_column_int(stmt, 4);
    const char* g = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    r.gender = g ? g : "unknown";

    const void* blob = sqlite3_column_blob(stmt, 6);
    int blob_bytes = sqlite3_column_bytes(stmt, 6);
    r.embedding = BlobToEmbedding(blob, blob_bytes);

    const char* snap =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.snapshot_path = snap ? snap : "";
  }

  sqlite3_finalize(stmt);
  return r;
}

std::vector<std::pair<FaceRecord, float>> FaceStore::SearchByEmbedding(
    const std::vector<float>& query_embedding, float threshold, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<std::pair<FaceRecord, float>> matches;
  if (!db_ || query_embedding.empty()) return matches;

  const char* sql =
      "SELECT id, channel_id, timestamp, confidence, age, gender, "
      "embedding, snapshot_path FROM faces WHERE embedding IS NOT NULL";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return matches;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const void* blob = sqlite3_column_blob(stmt, 6);
    int blob_bytes = sqlite3_column_bytes(stmt, 6);
    auto emb = BlobToEmbedding(blob, blob_bytes);

    if (emb.empty()) continue;

    float sim = CosineSimilarity(query_embedding, emb);
    if (sim < threshold) continue;

    FaceRecord r;
    r.id = sqlite3_column_int64(stmt, 0);
    r.channel_id = sqlite3_column_int(stmt, 1);
    r.timestamp = sqlite3_column_int64(stmt, 2);
    r.confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    r.age = sqlite3_column_int(stmt, 4);
    const char* g = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    r.gender = g ? g : "unknown";
    r.embedding = std::move(emb);
    const char* snap =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    r.snapshot_path = snap ? snap : "";

    matches.emplace_back(std::move(r), sim);
  }
  sqlite3_finalize(stmt);

  std::sort(matches.begin(), matches.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  if (static_cast<int>(matches.size()) > limit) {
    matches.resize(static_cast<size_t>(limit));
  }

  return matches;
}

int FaceStore::PurgeOlderThan(int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "DELETE FROM faces WHERE timestamp < ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_int64(stmt, 1, timestamp_ms);
  sqlite3_step(stmt);
  int deleted = sqlite3_changes(db_);
  sqlite3_finalize(stmt);

  if (deleted > 0) {
    spdlog::info("FaceStore: purged {} old records", deleted);
  }
  return deleted;
}

int64_t FaceStore::Count() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "SELECT COUNT(*) FROM faces";
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

std::vector<float> FaceStore::BlobToEmbedding(const void* data, int bytes) {
  std::vector<float> result;
  if (!data || bytes <= 0) return result;

  auto num_floats = static_cast<size_t>(bytes) / sizeof(float);
  result.resize(num_floats);
  std::memcpy(result.data(), data, num_floats * sizeof(float));
  return result;
}

float FaceStore::CosineSimilarity(const std::vector<float>& a,
                                  const std::vector<float>& b) {
  if (a.size() != b.size() || a.empty()) return 0.0F;
  float dot = 0.0F, norm_a = 0.0F, norm_b = 0.0F;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  float denom = std::sqrt(norm_a) * std::sqrt(norm_b);
  return (denom > 1e-6F) ? (dot / denom) : 0.0F;
}

}  // namespace loong::ai_engine
