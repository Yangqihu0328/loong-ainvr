// Copyright 2026 Loong AI NVR Project

#include "storage/record_index/record_index.h"

#include <filesystem>

#include "gtest/gtest.h"

namespace loong {
namespace storage {
namespace {

class RecordIndexTest : public ::testing::Test {
 protected:
  void SetUp() override {
    db_path_ = "/tmp/loong_test_index.db";
    std::filesystem::remove(db_path_);
    ASSERT_TRUE(index_.Open(db_path_));
  }

  void TearDown() override {
    index_.Close();
    std::filesystem::remove(db_path_);
  }

  std::string db_path_;
  RecordIndex index_;
};

TEST_F(RecordIndexTest, InsertAndQuerySegment) {
  SegmentInfo info;
  info.channel_id = 1;
  info.start_time = 1000000;
  info.end_time = 2000000;
  info.file_path = "/recordings/ch01/seg_test.mp4";
  info.file_size = 1048576;
  info.codec = "h264";
  info.resolution = "1920x1080";

  int64_t id = index_.InsertSegment(info);
  EXPECT_GT(id, 0);

  auto results = index_.QuerySegments(1, 500000, 1500000);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].channel_id, 1);
  EXPECT_EQ(results[0].file_path, "/recordings/ch01/seg_test.mp4");
}

TEST_F(RecordIndexTest, InsertAndQueryEvent) {
  SegmentInfo seg;
  seg.channel_id = 1;
  seg.start_time = 1000000;
  seg.end_time = 2000000;
  seg.file_path = "/recordings/ch01/seg.mp4";
  int64_t seg_id = index_.InsertSegment(seg);

  EventInfo event;
  event.segment_id = seg_id;
  event.channel_id = 1;
  event.event_type = "person_detected";
  event.event_time = 1500000;
  event.confidence = 0.85f;

  int64_t evt_id = index_.InsertEvent(event);
  EXPECT_GT(evt_id, 0);

  auto events = index_.QueryEvents(1, 1000000, 2000000);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].event_type, "person_detected");
}

TEST_F(RecordIndexTest, SetAndGetQuota) {
  StorageQuota quota;
  quota.channel_id = 5;
  quota.max_days = 14;
  quota.max_size_gb = 50.0;

  EXPECT_TRUE(index_.SetQuota(quota));

  auto result = index_.GetQuota(5);
  EXPECT_EQ(result.channel_id, 5);
  EXPECT_EQ(result.max_days, 14);
  EXPECT_DOUBLE_EQ(result.max_size_gb, 50.0);
}

TEST_F(RecordIndexTest, DeleteSegment) {
  SegmentInfo info;
  info.channel_id = 2;
  info.start_time = 3000000;
  info.end_time = 4000000;
  info.file_path = "/recordings/ch02/seg_delete.mp4";

  int64_t id = index_.InsertSegment(info);
  ASSERT_GT(id, 0);

  std::string path = index_.DeleteSegment(id);
  EXPECT_EQ(path, "/recordings/ch02/seg_delete.mp4");

  auto results = index_.QuerySegments(2, 2000000, 5000000);
  EXPECT_TRUE(results.empty());
}

TEST_F(RecordIndexTest, GetChannelTotalSize) {
  SegmentInfo s1;
  s1.channel_id = 3;
  s1.start_time = 1000000;
  s1.end_time = 2000000;
  s1.file_path = "/rec/s1.mp4";
  s1.file_size = 1000;
  index_.InsertSegment(s1);

  SegmentInfo s2;
  s2.channel_id = 3;
  s2.start_time = 2000000;
  s2.end_time = 3000000;
  s2.file_path = "/rec/s2.mp4";
  s2.file_size = 2000;
  index_.InsertSegment(s2);

  int64_t total = index_.GetChannelTotalSize(3);
  EXPECT_EQ(total, 3000);
}

TEST_F(RecordIndexTest, TimelineQueryMultipleSegmentsWithEvents) {
  // Insert 3 consecutive segments for channel 1
  for (int i = 0; i < 3; ++i) {
    SegmentInfo seg;
    seg.channel_id = 1;
    seg.start_time = 1000000 + i * 1000000;
    seg.end_time = 1000000 + (i + 1) * 1000000;
    seg.file_path = "/rec/ch01/seg_" + std::to_string(i) + ".mp4";
    seg.file_size = 500000;
    seg.codec = "h264";
    seg.resolution = "1920x1080";
    int64_t seg_id = index_.InsertSegment(seg);
    ASSERT_GT(seg_id, 0);

    EventInfo evt;
    evt.segment_id = seg_id;
    evt.channel_id = 1;
    evt.event_type = (i % 2 == 0) ? "person" : "vehicle";
    evt.event_time = seg.start_time + 500000;
    evt.confidence = 0.70F + static_cast<float>(i) * 0.1F;
    index_.InsertEvent(evt);
  }

  // Query full range
  auto segments = index_.QuerySegments(1, 1000000, 4000000);
  EXPECT_EQ(segments.size(), 3u);

  auto events = index_.QueryEvents(1, 1000000, 4000000);
  EXPECT_EQ(events.size(), 3u);

  // Verify partial time range returns subset
  auto partial = index_.QuerySegments(1, 1500000, 2500000);
  ASSERT_GE(partial.size(), 1u);

  // Verify event type filtering at application level
  int person_count = 0;
  for (const auto& e : events) {
    if (e.event_type == "person") ++person_count;
  }
  EXPECT_EQ(person_count, 2);
}

TEST_F(RecordIndexTest, TimelineQueryDifferentChannels) {
  for (int ch = 1; ch <= 3; ++ch) {
    SegmentInfo seg;
    seg.channel_id = ch;
    seg.start_time = 5000000;
    seg.end_time = 6000000;
    seg.file_path = "/rec/ch0" + std::to_string(ch) + "/seg.mp4";
    seg.codec = "h264";
    index_.InsertSegment(seg);
  }

  auto ch1 = index_.QuerySegments(1, 4000000, 7000000);
  EXPECT_EQ(ch1.size(), 1u);
  EXPECT_EQ(ch1[0].channel_id, 1);

  auto ch2 = index_.QuerySegments(2, 4000000, 7000000);
  EXPECT_EQ(ch2.size(), 1u);
  EXPECT_EQ(ch2[0].channel_id, 2);
}

TEST_F(RecordIndexTest, EventSearchConfidenceFiltering) {
  SegmentInfo seg;
  seg.channel_id = 1;
  seg.start_time = 10000000;
  seg.end_time = 11000000;
  seg.file_path = "/rec/seg.mp4";
  int64_t seg_id = index_.InsertSegment(seg);

  // Insert events with varying confidence
  float confidences[] = {0.3F, 0.5F, 0.7F, 0.9F, 0.95F};
  for (float c : confidences) {
    EventInfo evt;
    evt.segment_id = seg_id;
    evt.channel_id = 1;
    evt.event_type = "person";
    evt.event_time = 10500000;
    evt.confidence = c;
    index_.InsertEvent(evt);
  }

  auto all_events = index_.QueryEvents(1, 10000000, 11000000);
  EXPECT_EQ(all_events.size(), 5u);

  // Application-level confidence filtering (as done in API handler)
  int high_conf = 0;
  for (const auto& e : all_events) {
    if (e.confidence >= 0.7F) ++high_conf;
  }
  EXPECT_EQ(high_conf, 3);
}

TEST_F(RecordIndexTest, UpdateSegmentEnd) {
  SegmentInfo seg;
  seg.channel_id = 1;
  seg.start_time = 20000000;
  seg.end_time = 0;
  seg.file_path = "/rec/live.mp4";
  seg.file_size = 0;
  int64_t seg_id = index_.InsertSegment(seg);
  ASSERT_GT(seg_id, 0);

  EXPECT_TRUE(index_.UpdateSegmentEnd(seg_id, 21000000, 2048000));

  auto results = index_.QuerySegments(1, 20000000, 22000000);
  ASSERT_EQ(results.size(), 1u);
  EXPECT_EQ(results[0].end_time, 21000000);
  EXPECT_EQ(results[0].file_size, 2048000);
}

}  // namespace
}  // namespace storage
}  // namespace loong
