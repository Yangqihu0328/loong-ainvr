// Copyright 2026 Loong AI NVR Project

#include "analytics/analytics_aggregator.h"

#include <chrono>

#include "rules/rule_store/rule_store.h"
#include "spdlog/spdlog.h"

namespace loong::analytics {

AnalyticsAggregator::AnalyticsAggregator() = default;

AnalyticsAggregator::~AnalyticsAggregator() {
  Stop();
}

void AnalyticsAggregator::SetAnalyticsStore(
    std::shared_ptr<AnalyticsStore> store) {
  store_ = std::move(store);
}

void AnalyticsAggregator::SetRuleStore(
    std::shared_ptr<rules::RuleStore> rule_store) {
  rule_store_ = std::move(rule_store);
}

void AnalyticsAggregator::SetConfig(const AggregatorConfig& config) {
  config_ = config;
}

std::string AnalyticsAggregator::RuleTypeToKey(rules::RuleType type) {
  switch (type) {
    case rules::RuleType::kCrossLine:         return "cross_line";
    case rules::RuleType::kRegionIntrusion:   return "region_intrusion";
    case rules::RuleType::kObjectCounting:    return "object_counting";
    case rules::RuleType::kLoitering:         return "loitering";
  }
  return "unknown";
}

int64_t AnalyticsAggregator::AlignToHour(int64_t timestamp_ms) {
  const int64_t kHourMs = 3600LL * 1000;
  return (timestamp_ms / kHourMs) * kHourMs;
}

bool AnalyticsAggregator::IngestEvent(const rules::RuleEvent& event) {
  if (!store_) return false;

  HourlyAggregate agg;
  agg.hour_start = AlignToHour(event.timestamp);
  agg.channel_id = event.channel_id;
  agg.rule_type = RuleTypeToKey(event.rule_type);
  agg.event_count = 1;
  agg.count_sum = event.count_value;

  bool ok = store_->UpsertHourly(agg);

  if (ok && config_.log_detection_positions) {
    const auto& det = event.trigger_detection;
    float cx = (det.x1 + det.x2) / 2.0F;
    float cy = (det.y1 + det.y2) / 2.0F;
    if (cx > 0.0F || cy > 0.0F) {
      store_->LogDetectionPosition(event.channel_id, event.timestamp, cx, cy);
    }
  }

  return ok;
}

int AnalyticsAggregator::RunAggregation(int64_t up_to_ms) {
  if (!store_ || !rule_store_) return 0;

  if (up_to_ms <= 0) {
    up_to_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
  }

  // Pull all events from last_aggregated_ms_ to up_to_ms.
  // Query across all channels (channel_id = -1).
  auto events = rule_store_->QueryEvents(-1, last_aggregated_ms_, up_to_ms,
                                         10000);

  int processed = 0;
  for (const auto& event : events) {
    if (IngestEvent(event)) {
      ++processed;
    }
  }

  if (processed > 0) {
    last_aggregated_ms_ = up_to_ms;
    spdlog::info("AnalyticsAggregator: aggregated {} events up to {}",
                 processed, up_to_ms);
  }

  // Purge old data
  if (config_.retention_days > 0) {
    int64_t cutoff = up_to_ms -
                     static_cast<int64_t>(config_.retention_days) * 86400LL * 1000;
    store_->PurgeOlderThan(cutoff);
  }

  return processed;
}

bool AnalyticsAggregator::Start() {
  if (running_.load()) return false;
  if (!store_) {
    spdlog::error("AnalyticsAggregator: cannot start without analytics store");
    return false;
  }

  running_.store(true);
  bg_thread_ = std::thread(&AnalyticsAggregator::BackgroundLoop, this);
  spdlog::info("AnalyticsAggregator: background thread started "
               "(interval={}s, retention={}d)",
               config_.aggregation_interval_sec, config_.retention_days);
  return true;
}

void AnalyticsAggregator::Stop() {
  if (!running_.load()) return;
  running_.store(false);
  if (bg_thread_.joinable()) {
    bg_thread_.join();
  }
  spdlog::info("AnalyticsAggregator: stopped");
}

void AnalyticsAggregator::BackgroundLoop() {
  while (running_.load()) {
    RunAggregation();

    // Sleep in small increments to allow fast shutdown
    int remaining = config_.aggregation_interval_sec;
    while (remaining > 0 && running_.load()) {
      int sleep_sec = std::min(remaining, 5);
      std::this_thread::sleep_for(std::chrono::seconds(sleep_sec));
      remaining -= sleep_sec;
    }
  }
}

}  // namespace loong::analytics
