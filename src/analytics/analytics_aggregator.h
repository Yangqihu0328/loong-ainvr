// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_ANALYTICS_ANALYTICS_AGGREGATOR_H_
#define LOONG_ANALYTICS_ANALYTICS_AGGREGATOR_H_

#include "analytics/analytics_store.h"
#include "rules/rule_types.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace loong::rules {
class RuleStore;
}

namespace loong::analytics {

/// Configuration for the aggregator.
struct AggregatorConfig {
  int aggregation_interval_sec =
      3600;                 // How often to run aggregation (default: 1 hour)
  int retention_days = 90;  // How long to keep analytics data
  bool log_detection_positions =
      true;  // Store detection positions for heatmaps
};

/// Consumes rule events from RuleStore and produces aggregated analytics data.
///
/// The aggregator can operate in two modes:
/// 1. Realtime: call IngestEvent() for each new RuleEvent as it happens.
/// 2. Batch: call RunAggregation() periodically to pull new events from
/// RuleStore.
///
/// Both modes write to AnalyticsStore's hourly aggregation tables.
/// A background thread can run periodic aggregation when Start() is called.
class AnalyticsAggregator {
 public:
  AnalyticsAggregator();
  ~AnalyticsAggregator();

  /// Set the analytics store for writing aggregated data.
  void SetAnalyticsStore(std::shared_ptr<AnalyticsStore> store);

  /// Set the rule store for batch aggregation reads.
  void SetRuleStore(std::shared_ptr<rules::RuleStore> rule_store);

  /// Set configuration.
  void SetConfig(const AggregatorConfig& config);

  /// Ingest a single event in realtime mode.
  /// Immediately updates the hourly aggregate bucket and optionally logs
  /// position.
  bool IngestEvent(const rules::RuleEvent& event);

  /// Run batch aggregation: pull events from RuleStore since
  /// last_aggregated_ms_ and aggregate them into hourly buckets. Returns number
  /// of events processed.
  int RunAggregation(int64_t up_to_ms = 0);

  /// Start background periodic aggregation thread.
  bool Start();

  /// Stop background thread.
  void Stop();

  bool IsRunning() const { return running_.load(); }

  /// Convert a RuleType enum to a string key for storage.
  static std::string RuleTypeToKey(rules::RuleType type);

  /// Align a timestamp to the start of its hour (floor to hour boundary).
  static int64_t AlignToHour(int64_t timestamp_ms);

  AnalyticsAggregator(const AnalyticsAggregator&) = delete;
  AnalyticsAggregator& operator=(const AnalyticsAggregator&) = delete;

 private:
  void BackgroundLoop();

  std::shared_ptr<AnalyticsStore> store_;
  std::shared_ptr<rules::RuleStore> rule_store_;
  AggregatorConfig config_;

  std::atomic<bool> running_{false};
  std::thread bg_thread_;
  std::mutex mutex_;

  int64_t last_aggregated_ms_ = 0;
};

}  // namespace loong::analytics

#endif  // LOONG_ANALYTICS_ANALYTICS_AGGREGATOR_H_
