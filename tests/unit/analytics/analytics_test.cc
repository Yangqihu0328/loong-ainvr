// Copyright 2026 Loong AI NVR Project

#include "analytics/analytics_aggregator.h"
#include "analytics/analytics_store.h"
#include "gtest/gtest.h"
#include "rules/rule_types.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace loong::analytics {
namespace {

// Helper: create a timestamp at a specific hour offset from epoch
int64_t HourMs(int hour) { return static_cast<int64_t>(hour) * 3600LL * 1000; }

// ============================================================
// AnalyticsStore tests
// ============================================================

class AnalyticsStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = "/tmp/analytics_test_" + std::to_string(getpid()) + ".db";
    store_.Open(db_path_);
  }

  void TearDown() override {
    store_.Close();
    std::remove(db_path_.c_str());
  }

  std::string db_path_;
  AnalyticsStore store_;
};

TEST_F(AnalyticsStoreTest, UpsertAndCountHourly) {
  EXPECT_EQ(store_.CountHourly(), 0);

  HourlyAggregate agg;
  agg.hour_start = HourMs(100);
  agg.channel_id = 0;
  agg.rule_type = "cross_line";
  agg.event_count = 5;
  agg.count_sum = 0;

  EXPECT_TRUE(store_.UpsertHourly(agg));
  EXPECT_EQ(store_.CountHourly(), 1);

  // Upsert same key → should increment
  agg.event_count = 3;
  EXPECT_TRUE(store_.UpsertHourly(agg));
  EXPECT_EQ(store_.CountHourly(), 1);

  auto rows = store_.QueryHourly(0, "cross_line", HourMs(99), HourMs(101));
  ASSERT_EQ(rows.size(), 1U);
  EXPECT_EQ(rows[0].event_count, 8);  // 5 + 3
}

TEST_F(AnalyticsStoreTest, UpsertDifferentTypes) {
  HourlyAggregate a1;
  a1.hour_start = HourMs(100);
  a1.channel_id = 0;
  a1.rule_type = "cross_line";
  a1.event_count = 3;

  HourlyAggregate a2;
  a2.hour_start = HourMs(100);
  a2.channel_id = 0;
  a2.rule_type = "loitering";
  a2.event_count = 2;

  store_.UpsertHourly(a1);
  store_.UpsertHourly(a2);

  EXPECT_EQ(store_.CountHourly(), 2);
}

TEST_F(AnalyticsStoreTest, QueryHourlyByChannel) {
  HourlyAggregate a;
  a.hour_start = HourMs(100);
  a.rule_type = "cross_line";
  a.event_count = 1;

  a.channel_id = 0;
  store_.UpsertHourly(a);
  a.channel_id = 1;
  store_.UpsertHourly(a);

  auto ch0 = store_.QueryHourly(0, "", 0, HourMs(200));
  EXPECT_EQ(ch0.size(), 1U);

  auto all = store_.QueryHourly(-1, "", 0, HourMs(200));
  EXPECT_EQ(all.size(), 2U);
}

TEST_F(AnalyticsStoreTest, GetSummary) {
  HourlyAggregate a;
  a.hour_start = HourMs(100);
  a.channel_id = 0;
  a.event_count = 10;
  a.count_sum = 0;

  a.rule_type = "cross_line";
  store_.UpsertHourly(a);

  a.rule_type = "region_intrusion";
  a.event_count = 5;
  store_.UpsertHourly(a);

  a.rule_type = "object_counting";
  a.event_count = 3;
  a.count_sum = 42;
  store_.UpsertHourly(a);

  a.rule_type = "loitering";
  a.event_count = 2;
  a.count_sum = 0;
  store_.UpsertHourly(a);

  auto s = store_.GetSummary(HourMs(99), HourMs(101));
  EXPECT_EQ(s.total_events, 20);  // 10+5+3+2
  EXPECT_EQ(s.cross_line_events, 10);
  EXPECT_EQ(s.region_intrusion_events, 5);
  EXPECT_EQ(s.object_counting_events, 3);
  EXPECT_EQ(s.loitering_events, 2);
  EXPECT_EQ(s.total_count_value, 42);
  EXPECT_EQ(s.active_channels, 1);
}

TEST_F(AnalyticsStoreTest, GetSummaryMultipleChannels) {
  HourlyAggregate a;
  a.hour_start = HourMs(100);
  a.rule_type = "cross_line";
  a.event_count = 1;

  a.channel_id = 0;
  store_.UpsertHourly(a);
  a.channel_id = 1;
  store_.UpsertHourly(a);
  a.channel_id = 2;
  store_.UpsertHourly(a);

  auto s = store_.GetSummary(HourMs(99), HourMs(101));
  EXPECT_EQ(s.active_channels, 3);
  EXPECT_EQ(s.total_events, 3);
}

TEST_F(AnalyticsStoreTest, GetTrendsHourly) {
  for (int h = 0; h < 3; ++h) {
    HourlyAggregate a;
    a.hour_start = HourMs(100 + h);
    a.channel_id = 0;
    a.rule_type = "cross_line";
    a.event_count = h + 1;
    store_.UpsertHourly(a);
  }

  auto trends = store_.GetTrends(HourMs(99), HourMs(103),
                                 TimeGranularity::kHourly, 0, "cross_line");
  EXPECT_EQ(trends.size(), 3U);
  EXPECT_EQ(trends[0].event_count, 1);
  EXPECT_EQ(trends[1].event_count, 2);
  EXPECT_EQ(trends[2].event_count, 3);
}

TEST_F(AnalyticsStoreTest, GetTrendsDaily) {
  // Insert 48 hours of data (2 days)
  for (int h = 0; h < 48; ++h) {
    HourlyAggregate a;
    a.hour_start = HourMs(h);
    a.channel_id = 0;
    a.rule_type = "cross_line";
    a.event_count = 1;
    store_.UpsertHourly(a);
  }

  auto trends = store_.GetTrends(0, HourMs(48), TimeGranularity::kDaily);
  EXPECT_EQ(trends.size(), 2U);
  EXPECT_EQ(trends[0].event_count, 24);
  EXPECT_EQ(trends[1].event_count, 24);
}

TEST_F(AnalyticsStoreTest, LogDetectionPositionAndHeatmap) {
  store_.LogDetectionPosition(0, 1000, 100.0F, 200.0F);
  store_.LogDetectionPosition(0, 2000, 105.0F, 205.0F);
  store_.LogDetectionPosition(0, 3000, 960.0F, 540.0F);  // center

  auto cells = store_.GetHeatmap(0, 5000, 0, 10, 10, 1920, 1080);
  EXPECT_GE(cells.size(), 1U);

  // The first two detections should be in the same grid cell
  // (100/192=0, 200/108=1) and (105/192=0, 205/108=1)
  bool found_top_left = false;
  for (const auto& c : cells) {
    if (c.grid_x == 0 && c.grid_y == 1) {
      EXPECT_EQ(c.hit_count, 2);
      found_top_left = true;
    }
  }
  EXPECT_TRUE(found_top_left);
}

TEST_F(AnalyticsStoreTest, GetPeakHours) {
  // Insert events at specific hours of day
  // Hour 8 (morning rush): 10 events
  // Hour 17 (evening rush): 15 events
  // Hour 3 (night): 1 event
  HourlyAggregate a;
  a.channel_id = 0;
  a.rule_type = "cross_line";

  a.hour_start = HourMs(8);  // 08:00 UTC
  a.event_count = 10;
  store_.UpsertHourly(a);

  a.hour_start = HourMs(17);  // 17:00 UTC
  a.event_count = 15;
  store_.UpsertHourly(a);

  a.hour_start = HourMs(3);  // 03:00 UTC
  a.event_count = 1;
  store_.UpsertHourly(a);

  auto peaks = store_.GetPeakHours(0, HourMs(24));
  ASSERT_EQ(peaks.size(), 3U);

  // Results should be sorted by hour
  EXPECT_EQ(peaks[0].hour, 3);
  EXPECT_EQ(peaks[0].event_count, 1);

  EXPECT_EQ(peaks[1].hour, 8);
  EXPECT_EQ(peaks[1].event_count, 10);

  EXPECT_EQ(peaks[2].hour, 17);
  EXPECT_EQ(peaks[2].event_count, 15);

  // Check percentages sum to 100%
  float total_pct = 0.0F;
  for (const auto& p : peaks) total_pct += p.percentage;
  EXPECT_NEAR(total_pct, 100.0F, 0.1F);
}

TEST_F(AnalyticsStoreTest, PurgeOlderThan) {
  HourlyAggregate a;
  a.channel_id = 0;
  a.rule_type = "cross_line";
  a.event_count = 1;

  a.hour_start = HourMs(100);
  store_.UpsertHourly(a);
  a.hour_start = HourMs(200);
  store_.UpsertHourly(a);
  a.hour_start = HourMs(300);
  store_.UpsertHourly(a);

  store_.LogDetectionPosition(0, HourMs(100), 100, 100);
  store_.LogDetectionPosition(0, HourMs(300), 200, 200);

  int deleted = store_.PurgeOlderThan(HourMs(250));
  EXPECT_GE(deleted, 2);  // 2 hourly + 1 position

  EXPECT_EQ(store_.CountHourly(), 1);
}

TEST_F(AnalyticsStoreTest, EmptyStoreReturnsDefaults) {
  auto s = store_.GetSummary(0, HourMs(1000));
  EXPECT_EQ(s.total_events, 0);
  EXPECT_EQ(s.active_channels, 0);

  auto trends = store_.GetTrends(0, HourMs(1000), TimeGranularity::kHourly);
  EXPECT_TRUE(trends.empty());

  auto heatmap = store_.GetHeatmap(0, HourMs(1000));
  EXPECT_TRUE(heatmap.empty());

  auto peaks = store_.GetPeakHours(0, HourMs(1000));
  EXPECT_TRUE(peaks.empty());
}

// ============================================================
// AnalyticsAggregator tests
// ============================================================

class AggregatorTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = "/tmp/agg_test_" + std::to_string(getpid()) + ".db";
    store_ = std::make_shared<AnalyticsStore>();
    store_->Open(db_path_);
    aggregator_.SetAnalyticsStore(store_);
  }

  void TearDown() override {
    store_->Close();
    std::remove(db_path_.c_str());
  }

  rules::RuleEvent MakeEvent(rules::RuleType type, int channel, int64_t ts,
                             int count_val = 0) {
    rules::RuleEvent ev;
    ev.rule_id = 1;
    ev.rule_name = "test_rule";
    ev.rule_type = type;
    ev.channel_id = channel;
    ev.timestamp = ts;
    ev.count_value = count_val;
    ev.trigger_detection = {100.0F, 200.0F, 150.0F, 250.0F, 0.9F, 0, "person"};
    return ev;
  }

  std::string db_path_;
  std::shared_ptr<AnalyticsStore> store_;
  AnalyticsAggregator aggregator_;
};

TEST_F(AggregatorTest, RuleTypeToKey) {
  EXPECT_EQ(AnalyticsAggregator::RuleTypeToKey(rules::RuleType::kCrossLine),
            "cross_line");
  EXPECT_EQ(
      AnalyticsAggregator::RuleTypeToKey(rules::RuleType::kRegionIntrusion),
      "region_intrusion");
  EXPECT_EQ(
      AnalyticsAggregator::RuleTypeToKey(rules::RuleType::kObjectCounting),
      "object_counting");
  EXPECT_EQ(AnalyticsAggregator::RuleTypeToKey(rules::RuleType::kLoitering),
            "loitering");
}

TEST_F(AggregatorTest, AlignToHour) {
  // 1.5 hours in ms
  int64_t ts = HourMs(1) + 1800LL * 1000;
  EXPECT_EQ(AnalyticsAggregator::AlignToHour(ts), HourMs(1));

  EXPECT_EQ(AnalyticsAggregator::AlignToHour(HourMs(5)), HourMs(5));
  EXPECT_EQ(AnalyticsAggregator::AlignToHour(0), 0);
}

TEST_F(AggregatorTest, IngestSingleEvent) {
  auto ev = MakeEvent(rules::RuleType::kCrossLine, 0, HourMs(100) + 1000);
  EXPECT_TRUE(aggregator_.IngestEvent(ev));

  auto s = store_->GetSummary(HourMs(99), HourMs(101));
  EXPECT_EQ(s.total_events, 1);
  EXPECT_EQ(s.cross_line_events, 1);
}

TEST_F(AggregatorTest, IngestMultipleEventsAggregates) {
  // 5 events in the same hour bucket
  for (int i = 0; i < 5; ++i) {
    auto ev = MakeEvent(rules::RuleType::kCrossLine, 0,
                        HourMs(100) + static_cast<int64_t>(i) * 60000);
    aggregator_.IngestEvent(ev);
  }

  EXPECT_EQ(store_->CountHourly(), 1);  // All in same bucket

  auto s = store_->GetSummary(HourMs(99), HourMs(101));
  EXPECT_EQ(s.total_events, 5);
}

TEST_F(AggregatorTest, IngestCountingEventWithValue) {
  auto ev = MakeEvent(rules::RuleType::kObjectCounting, 0, HourMs(100), 42);
  aggregator_.IngestEvent(ev);

  auto s = store_->GetSummary(HourMs(99), HourMs(101));
  EXPECT_EQ(s.object_counting_events, 1);
  EXPECT_EQ(s.total_count_value, 42);
}

TEST_F(AggregatorTest, IngestLogsDetectionPosition) {
  AggregatorConfig config;
  config.log_detection_positions = true;
  aggregator_.SetConfig(config);

  auto ev = MakeEvent(rules::RuleType::kCrossLine, 0, 1000);
  aggregator_.IngestEvent(ev);

  auto cells = store_->GetHeatmap(0, 5000, 0, 10, 10, 1920, 1080);
  EXPECT_GE(cells.size(), 1U);
}

TEST_F(AggregatorTest, IngestWithPositionDisabled) {
  AggregatorConfig config;
  config.log_detection_positions = false;
  aggregator_.SetConfig(config);

  auto ev = MakeEvent(rules::RuleType::kCrossLine, 0, 1000);
  aggregator_.IngestEvent(ev);

  auto cells = store_->GetHeatmap(0, 5000, 0, 10, 10, 1920, 1080);
  EXPECT_TRUE(cells.empty());
}

TEST_F(AggregatorTest, IngestDifferentHours) {
  aggregator_.IngestEvent(
      MakeEvent(rules::RuleType::kCrossLine, 0, HourMs(100)));
  aggregator_.IngestEvent(
      MakeEvent(rules::RuleType::kCrossLine, 0, HourMs(101)));
  aggregator_.IngestEvent(
      MakeEvent(rules::RuleType::kCrossLine, 0, HourMs(102)));

  EXPECT_EQ(store_->CountHourly(), 3);

  auto trends = store_->GetTrends(HourMs(99), HourMs(103),
                                  TimeGranularity::kHourly, 0, "cross_line");
  EXPECT_EQ(trends.size(), 3U);
}

TEST_F(AggregatorTest, IngestWithoutStoreReturnsFalse) {
  AnalyticsAggregator no_store;
  auto ev = MakeEvent(rules::RuleType::kCrossLine, 0, 1000);
  EXPECT_FALSE(no_store.IngestEvent(ev));
}

TEST(TimeGranularityTest, ToStringCoversAll) {
  EXPECT_STREQ(TimeGranularityToString(TimeGranularity::kHourly), "hourly");
  EXPECT_STREQ(TimeGranularityToString(TimeGranularity::kDaily), "daily");
  EXPECT_STREQ(TimeGranularityToString(TimeGranularity::kWeekly), "weekly");
  EXPECT_STREQ(TimeGranularityToString(TimeGranularity::kMonthly), "monthly");
}

}  // namespace
}  // namespace loong::analytics
