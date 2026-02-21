// Copyright 2026 Loong AI NVR Project

#include "analytics/analytics_store.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

#include "spdlog/spdlog.h"

namespace loong::analytics {

AnalyticsStore::AnalyticsStore() = default;

AnalyticsStore::~AnalyticsStore() {
  Close();
}

bool AnalyticsStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("AnalyticsStore: failed to open '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  CreateTables();
  spdlog::info("AnalyticsStore: opened '{}'", db_path);
  return true;
}

void AnalyticsStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void AnalyticsStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS analytics_hourly (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      hour_start  INTEGER NOT NULL,
      channel_id  INTEGER DEFAULT -1,
      rule_type   TEXT DEFAULT '',
      event_count INTEGER DEFAULT 0,
      count_sum   INTEGER DEFAULT 0
    );
    CREATE UNIQUE INDEX IF NOT EXISTS idx_hourly_unique
      ON analytics_hourly(hour_start, channel_id, rule_type);
    CREATE INDEX IF NOT EXISTS idx_hourly_time
      ON analytics_hourly(hour_start);

    CREATE TABLE IF NOT EXISTS detection_positions (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      channel_id  INTEGER DEFAULT -1,
      timestamp   INTEGER DEFAULT 0,
      center_x    REAL DEFAULT 0,
      center_y    REAL DEFAULT 0
    );
    CREATE INDEX IF NOT EXISTS idx_detpos_time
      ON detection_positions(timestamp);
    CREATE INDEX IF NOT EXISTS idx_detpos_channel
      ON detection_positions(channel_id);
  )";

  char* err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    spdlog::error("AnalyticsStore: table creation failed: {}", err ? err : "");
    sqlite3_free(err);
  }
}

bool AnalyticsStore::UpsertHourly(const HourlyAggregate& agg) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "INSERT INTO analytics_hourly (hour_start, channel_id, rule_type, "
      "event_count, count_sum) VALUES (?, ?, ?, ?, ?) "
      "ON CONFLICT(hour_start, channel_id, rule_type) DO UPDATE SET "
      "event_count = event_count + excluded.event_count, "
      "count_sum = count_sum + excluded.count_sum";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, agg.hour_start);
  sqlite3_bind_int(stmt, 2, agg.channel_id);
  sqlite3_bind_text(stmt, 3, agg.rule_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, agg.event_count);
  sqlite3_bind_int(stmt, 5, agg.count_sum);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

std::vector<HourlyAggregate> AnalyticsStore::QueryHourly(
    int channel_id, const std::string& rule_type,
    int64_t start_ms, int64_t end_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<HourlyAggregate> results;
  if (!db_) return results;

  std::string sql =
      "SELECT hour_start, channel_id, rule_type, event_count, count_sum "
      "FROM analytics_hourly WHERE 1=1";

  if (channel_id >= 0) sql += " AND channel_id = ?";
  if (!rule_type.empty()) sql += " AND rule_type = ?";
  if (start_ms > 0) sql += " AND hour_start >= ?";
  if (end_ms > 0) sql += " AND hour_start <= ?";
  sql += " ORDER BY hour_start ASC";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int p = 1;
  if (channel_id >= 0) sqlite3_bind_int(stmt, p++, channel_id);
  if (!rule_type.empty())
    sqlite3_bind_text(stmt, p++, rule_type.c_str(), -1, SQLITE_TRANSIENT);
  if (start_ms > 0) sqlite3_bind_int64(stmt, p++, start_ms);
  if (end_ms > 0) sqlite3_bind_int64(stmt, p++, end_ms);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    HourlyAggregate h;
    h.hour_start = sqlite3_column_int64(stmt, 0);
    h.channel_id = sqlite3_column_int(stmt, 1);
    const char* rt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    h.rule_type = rt ? rt : "";
    h.event_count = sqlite3_column_int(stmt, 3);
    h.count_sum = sqlite3_column_int(stmt, 4);
    results.push_back(std::move(h));
  }

  sqlite3_finalize(stmt);
  return results;
}

AnalyticsSummary AnalyticsStore::GetSummary(int64_t start_ms, int64_t end_ms,
                                            int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  AnalyticsSummary s;
  s.start_time = start_ms;
  s.end_time = end_ms;
  if (!db_) return s;

  // Per-type aggregation
  std::string sql =
      "SELECT rule_type, SUM(event_count), SUM(count_sum) "
      "FROM analytics_hourly WHERE hour_start >= ? AND hour_start <= ?";
  if (channel_id >= 0) sql += " AND channel_id = ?";
  sql += " GROUP BY rule_type";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return s;
  }

  int p = 1;
  sqlite3_bind_int64(stmt, p++, start_ms);
  sqlite3_bind_int64(stmt, p++, end_ms);
  if (channel_id >= 0) sqlite3_bind_int(stmt, p, channel_id);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* rt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string type = rt ? rt : "";
    int count = sqlite3_column_int(stmt, 1);
    int count_sum = sqlite3_column_int(stmt, 2);

    s.total_events += count;

    if (type == "cross_line")         s.cross_line_events = count;
    else if (type == "region_intrusion") s.region_intrusion_events = count;
    else if (type == "object_counting") {
      s.object_counting_events = count;
      s.total_count_value += count_sum;
    }
    else if (type == "loitering")     s.loitering_events = count;
  }
  sqlite3_finalize(stmt);

  // Count distinct active channels
  std::string ch_sql =
      "SELECT COUNT(DISTINCT channel_id) FROM analytics_hourly "
      "WHERE hour_start >= ? AND hour_start <= ?";
  if (channel_id >= 0) ch_sql += " AND channel_id = ?";

  if (sqlite3_prepare_v2(db_, ch_sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
    p = 1;
    sqlite3_bind_int64(stmt, p++, start_ms);
    sqlite3_bind_int64(stmt, p++, end_ms);
    if (channel_id >= 0) sqlite3_bind_int(stmt, p, channel_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      s.active_channels = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  return s;
}

std::vector<TrendBucket> AnalyticsStore::GetTrends(
    int64_t start_ms, int64_t end_ms,
    TimeGranularity granularity,
    int channel_id,
    const std::string& rule_type) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<TrendBucket> results;
  if (!db_) return results;

  // Determine bucket size in milliseconds
  int64_t bucket_ms = 0;
  switch (granularity) {
    case TimeGranularity::kHourly:  bucket_ms = 3600LL * 1000;       break;
    case TimeGranularity::kDaily:   bucket_ms = 86400LL * 1000;      break;
    case TimeGranularity::kWeekly:  bucket_ms = 7LL * 86400 * 1000;  break;
    case TimeGranularity::kMonthly: bucket_ms = 30LL * 86400 * 1000; break;
  }

  // Query from hourly table and bucket
  std::string sql =
      "SELECT hour_start, event_count, count_sum, rule_type, channel_id "
      "FROM analytics_hourly WHERE hour_start >= ? AND hour_start <= ?";
  if (channel_id >= 0) sql += " AND channel_id = ?";
  if (!rule_type.empty()) sql += " AND rule_type = ?";
  sql += " ORDER BY hour_start ASC";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int p = 1;
  sqlite3_bind_int64(stmt, p++, start_ms);
  sqlite3_bind_int64(stmt, p++, end_ms);
  if (channel_id >= 0) sqlite3_bind_int(stmt, p++, channel_id);
  if (!rule_type.empty())
    sqlite3_bind_text(stmt, p, rule_type.c_str(), -1, SQLITE_TRANSIENT);

  // Aggregate rows into buckets in-memory
  std::unordered_map<int64_t, TrendBucket> buckets;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int64_t hour = sqlite3_column_int64(stmt, 0);
    int ev_count = sqlite3_column_int(stmt, 1);
    int cnt_sum = sqlite3_column_int(stmt, 2);

    int64_t bucket_key = (hour / bucket_ms) * bucket_ms;
    auto& b = buckets[bucket_key];
    b.bucket_start = bucket_key;
    b.event_count += ev_count;
    b.count_sum += cnt_sum;
    b.channel_id = channel_id;
    b.rule_type = rule_type;
  }
  sqlite3_finalize(stmt);

  results.reserve(buckets.size());
  for (auto& [key, bucket] : buckets) {
    results.push_back(std::move(bucket));
  }
  std::sort(results.begin(), results.end(),
            [](const TrendBucket& a, const TrendBucket& b) {
              return a.bucket_start < b.bucket_start;
            });

  return results;
}

std::vector<HeatmapCell> AnalyticsStore::GetHeatmap(
    int64_t start_ms, int64_t end_ms,
    int channel_id,
    int grid_cols, int grid_rows,
    int image_width, int image_height) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<HeatmapCell> results;
  if (!db_ || grid_cols <= 0 || grid_rows <= 0) return results;

  std::string sql =
      "SELECT center_x, center_y FROM detection_positions "
      "WHERE timestamp >= ? AND timestamp <= ?";
  if (channel_id >= 0) sql += " AND channel_id = ?";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int p = 1;
  sqlite3_bind_int64(stmt, p++, start_ms);
  sqlite3_bind_int64(stmt, p++, end_ms);
  if (channel_id >= 0) sqlite3_bind_int(stmt, p, channel_id);

  float cell_w = static_cast<float>(image_width) / static_cast<float>(grid_cols);
  float cell_h = static_cast<float>(image_height) / static_cast<float>(grid_rows);

  // grid_cols × grid_rows flat grid
  auto grid_size = static_cast<size_t>(grid_cols) * static_cast<size_t>(grid_rows);
  std::vector<int> grid(grid_size, 0);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto cx = static_cast<float>(sqlite3_column_double(stmt, 0));
    auto cy = static_cast<float>(sqlite3_column_double(stmt, 1));

    int gx = static_cast<int>(cx / cell_w);
    int gy = static_cast<int>(cy / cell_h);
    if (gx < 0) gx = 0;
    if (gx >= grid_cols) gx = grid_cols - 1;
    if (gy < 0) gy = 0;
    if (gy >= grid_rows) gy = grid_rows - 1;

    grid[static_cast<size_t>(gy) * static_cast<size_t>(grid_cols) +
         static_cast<size_t>(gx)]++;
  }
  sqlite3_finalize(stmt);

  for (int y = 0; y < grid_rows; ++y) {
    for (int x = 0; x < grid_cols; ++x) {
      int count = grid[static_cast<size_t>(y) * static_cast<size_t>(grid_cols) +
                       static_cast<size_t>(x)];
      if (count > 0) {
        results.push_back({x, y, count});
      }
    }
  }

  return results;
}

std::vector<PeakHourEntry> AnalyticsStore::GetPeakHours(
    int64_t start_ms, int64_t end_ms,
    int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PeakHourEntry> results;
  if (!db_) return results;

  // Extract hour-of-day from hour_start and sum event counts
  // hour_start is in milliseconds; (hour_start / 3600000) % 24 gives hour-of-day (UTC)
  std::string sql =
      "SELECT (hour_start / 3600000) % 24 AS hod, SUM(event_count) "
      "FROM analytics_hourly WHERE hour_start >= ? AND hour_start <= ?";
  if (channel_id >= 0) sql += " AND channel_id = ?";
  sql += " GROUP BY hod ORDER BY hod ASC";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  int p = 1;
  sqlite3_bind_int64(stmt, p++, start_ms);
  sqlite3_bind_int64(stmt, p++, end_ms);
  if (channel_id >= 0) sqlite3_bind_int(stmt, p, channel_id);

  int total = 0;
  std::vector<std::pair<int, int>> raw;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int hour = sqlite3_column_int(stmt, 0);
    int count = sqlite3_column_int(stmt, 1);
    raw.emplace_back(hour, count);
    total += count;
  }
  sqlite3_finalize(stmt);

  results.reserve(raw.size());
  for (auto& [hour, count] : raw) {
    float pct = (total > 0) ? (static_cast<float>(count) /
                               static_cast<float>(total) * 100.0F)
                            : 0.0F;
    results.push_back({hour, count, pct});
  }

  return results;
}

bool AnalyticsStore::LogDetectionPosition(int channel_id, int64_t timestamp,
                                          float center_x, float center_y) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "INSERT INTO detection_positions (channel_id, timestamp, center_x, "
      "center_y) VALUES (?, ?, ?, ?)";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int64(stmt, 2, timestamp);
  sqlite3_bind_double(stmt, 3, static_cast<double>(center_x));
  sqlite3_bind_double(stmt, 4, static_cast<double>(center_y));

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

int AnalyticsStore::PurgeOlderThan(int64_t timestamp_ms) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  int total_deleted = 0;

  {
    const char* sql = "DELETE FROM analytics_hourly WHERE hour_start < ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, timestamp_ms);
      sqlite3_step(stmt);
      total_deleted += sqlite3_changes(db_);
      sqlite3_finalize(stmt);
    }
  }

  {
    const char* sql = "DELETE FROM detection_positions WHERE timestamp < ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
      sqlite3_bind_int64(stmt, 1, timestamp_ms);
      sqlite3_step(stmt);
      total_deleted += sqlite3_changes(db_);
      sqlite3_finalize(stmt);
    }
  }

  if (total_deleted > 0) {
    spdlog::info("AnalyticsStore: purged {} old records", total_deleted);
  }
  return total_deleted;
}

int64_t AnalyticsStore::CountHourly() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "SELECT COUNT(*) FROM analytics_hourly";
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

}  // namespace loong::analytics
