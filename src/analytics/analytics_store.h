// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_ANALYTICS_ANALYTICS_STORE_H_
#define LOONG_ANALYTICS_ANALYTICS_STORE_H_

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace loong::analytics {

/// Granularity for time-series aggregation.
enum class TimeGranularity {
  kHourly,
  kDaily,
  kWeekly,
  kMonthly,
};

inline const char* TimeGranularityToString(TimeGranularity g) {
  switch (g) {
    case TimeGranularity::kHourly:  return "hourly";
    case TimeGranularity::kDaily:   return "daily";
    case TimeGranularity::kWeekly:  return "weekly";
    case TimeGranularity::kMonthly: return "monthly";
  }
  return "unknown";
}

/// A single bucket in a time-series aggregation.
struct TrendBucket {
  int64_t bucket_start = 0;  // Unix timestamp (ms) of the bucket start
  int event_count = 0;
  int count_sum = 0;          // Sum of count_value for counting rules
  std::string rule_type;      // "" means all types aggregated together
  int channel_id = -1;        // -1 means all channels aggregated
};

/// Summary statistics over a time range.
struct AnalyticsSummary {
  int64_t start_time = 0;
  int64_t end_time = 0;
  int total_events = 0;
  int cross_line_events = 0;
  int region_intrusion_events = 0;
  int object_counting_events = 0;
  int loitering_events = 0;
  int total_count_value = 0;   // Sum of all counting events
  int active_channels = 0;     // Number of channels with events
};

/// A cell in a spatial heatmap grid.
struct HeatmapCell {
  int grid_x = 0;             // Grid column index
  int grid_y = 0;             // Grid row index
  int hit_count = 0;          // Number of detections in this cell
};

/// Peak hour entry.
struct PeakHourEntry {
  int hour = 0;               // 0-23
  int event_count = 0;
  float percentage = 0.0F;    // Percentage of total events
};

/// Pre-aggregated hourly row stored in SQLite.
struct HourlyAggregate {
  int64_t hour_start = 0;     // Unix timestamp (ms), aligned to hour
  int channel_id = -1;
  std::string rule_type;
  int event_count = 0;
  int count_sum = 0;
};

/// SQLite-based storage for analytics aggregation data.
/// Thread-safe — all public methods are mutex-protected.
class AnalyticsStore {
 public:
  AnalyticsStore();
  ~AnalyticsStore();

  bool Open(const std::string& db_path);
  void Close();

  /// Upsert an hourly aggregate row. If a row with the same
  /// (hour_start, channel_id, rule_type) exists, increments counts.
  bool UpsertHourly(const HourlyAggregate& agg);

  /// Query hourly aggregates within a time range.
  std::vector<HourlyAggregate> QueryHourly(
      int channel_id, const std::string& rule_type,
      int64_t start_ms, int64_t end_ms);

  /// Get aggregated summary for a time range.
  AnalyticsSummary GetSummary(int64_t start_ms, int64_t end_ms,
                              int channel_id = -1);

  /// Get trend data (bucketed event counts) for a time range.
  std::vector<TrendBucket> GetTrends(
      int64_t start_ms, int64_t end_ms,
      TimeGranularity granularity,
      int channel_id = -1,
      const std::string& rule_type = "");

  /// Get heatmap data from stored detection positions.
  std::vector<HeatmapCell> GetHeatmap(
      int64_t start_ms, int64_t end_ms,
      int channel_id = -1,
      int grid_cols = 20, int grid_rows = 15,
      int image_width = 1920, int image_height = 1080);

  /// Get peak hour distribution.
  std::vector<PeakHourEntry> GetPeakHours(
      int64_t start_ms, int64_t end_ms,
      int channel_id = -1);

  /// Store a detection position for heatmap accumulation.
  bool LogDetectionPosition(int channel_id, int64_t timestamp,
                            float center_x, float center_y);

  /// Purge analytics data older than the given timestamp.
  int PurgeOlderThan(int64_t timestamp_ms);

  /// Get total row count in hourly table.
  int64_t CountHourly();

  AnalyticsStore(const AnalyticsStore&) = delete;
  AnalyticsStore& operator=(const AnalyticsStore&) = delete;

 private:
  void CreateTables();

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::analytics

#endif  // LOONG_ANALYTICS_ANALYTICS_STORE_H_
