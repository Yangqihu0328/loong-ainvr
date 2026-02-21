// Copyright 2026 Loong AI NVR Project

#include "network/hls_stream/hls_service.h"
#include "network/hls_stream/ts_muxer.h"

#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace loong::network {

// ============================================================
// TsMuxer Tests
// ============================================================

class TsMuxerTest : public ::testing::Test {
 protected:
  TsMuxer muxer;

 public:
  static std::vector<uint8_t> MakeFakeKeyframe() {
    // Annex B start code + SPS(7) + PPS(8) + IDR(5)
    std::vector<uint8_t> data;
    // SPS NAL
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x01);
    data.push_back(0x67);  // NAL type 7 (SPS)
    data.push_back(0x42);
    data.push_back(0x00);
    data.push_back(0x1E);
    data.push_back(0xAB);
    // PPS NAL
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x01);
    data.push_back(0x68);  // NAL type 8 (PPS)
    data.push_back(0xCE);
    data.push_back(0x38);
    data.push_back(0x80);
    // IDR slice NAL
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x01);
    data.push_back(0x65);  // NAL type 5 (IDR)
    for (int i = 0; i < 100; ++i) {
      data.push_back(static_cast<uint8_t>(i & 0xFF));
    }
    return data;
  }

  static std::vector<uint8_t> MakeFakeInterframe() {
    std::vector<uint8_t> data;
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x00);
    data.push_back(0x01);
    data.push_back(0x41);  // NAL type 1 (non-IDR slice)
    for (int i = 0; i < 50; ++i) {
      data.push_back(static_cast<uint8_t>(i & 0xFF));
    }
    return data;
  }
};

TEST_F(TsMuxerTest, WritePsiTablesProduces2Packets) {
  auto psi = muxer.WritePsiTables();
  ASSERT_EQ(psi.size(), 376u);  // 2 x 188 bytes
}

TEST_F(TsMuxerTest, PsiPacketsStartWithSyncByte) {
  auto psi = muxer.WritePsiTables();
  EXPECT_EQ(psi[0], 0x47);
  EXPECT_EQ(psi[188], 0x47);
}

TEST_F(TsMuxerTest, PatHasCorrectPid) {
  auto psi = muxer.WritePsiTables();
  uint16_t pid = static_cast<uint16_t>(((psi[1] & 0x1F) << 8) | psi[2]);
  EXPECT_EQ(pid, 0x0000);
}

TEST_F(TsMuxerTest, PmtHasCorrectPid) {
  auto psi = muxer.WritePsiTables();
  uint16_t pid = static_cast<uint16_t>(((psi[189] & 0x1F) << 8) | psi[190]);
  EXPECT_EQ(pid, TsMuxer::kPmtPid);
}

TEST_F(TsMuxerTest, WriteAccessUnitProducesMultipleOf188) {
  auto frame = MakeFakeKeyframe();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  EXPECT_FALSE(ts.empty());
  EXPECT_EQ(ts.size() % 188, 0u);
}

TEST_F(TsMuxerTest, WriteAccessUnitAllPacketsHaveSyncByte) {
  auto frame = MakeFakeKeyframe();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  size_t num_packets = ts.size() / 188;
  for (size_t i = 0; i < num_packets; ++i) {
    EXPECT_EQ(ts[i * 188], 0x47) << "Packet " << i << " missing sync byte";
  }
}

TEST_F(TsMuxerTest, KeyframeHasRandomAccessIndicator) {
  auto frame = MakeFakeKeyframe();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  // First video TS packet should have adaptation field with random_access
  ASSERT_GE(ts.size(), 188u);
  uint8_t adapt_control = (ts[3] >> 4) & 0x03;
  EXPECT_EQ(adapt_control, 0x03);  // adaptation + payload
  uint8_t adapt_flags = ts[5];
  EXPECT_TRUE((adapt_flags & 0x40) != 0) << "random_access_indicator not set";
}

TEST_F(TsMuxerTest, InterframeNoRandomAccess) {
  auto frame = MakeFakeInterframe();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 1000, false);
  ASSERT_GE(ts.size(), 188u);
  // Non-keyframe: may or may not have adaptation field, but no random access
  uint8_t adapt_control = (ts[3] >> 4) & 0x03;
  if (adapt_control == 0x03 || adapt_control == 0x02) {
    uint8_t adapt_flags = ts[5];
    EXPECT_FALSE((adapt_flags & 0x40) != 0)
        << "random_access_indicator should not be set";
  }
}

TEST_F(TsMuxerTest, VideoPidIsCorrect) {
  auto frame = MakeFakeKeyframe();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  ASSERT_GE(ts.size(), 188u);
  uint16_t pid = static_cast<uint16_t>(((ts[1] & 0x1F) << 8) | ts[2]);
  EXPECT_EQ(pid, TsMuxer::kVideoPid);
}

TEST_F(TsMuxerTest, ContinuityCounterIncrements) {
  auto frame = MakeFakeKeyframe();
  auto ts1 = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  auto ts2 = muxer.WriteAccessUnit(frame.data(), frame.size(), 40000, true);
  uint8_t cc1 = ts1[3] & 0x0F;
  uint8_t cc2 = ts2[3] & 0x0F;
  EXPECT_NE(cc1, cc2);
}

TEST_F(TsMuxerTest, ResetClearsCounters) {
  auto frame = MakeFakeKeyframe();
  muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  muxer.Reset();
  auto ts = muxer.WriteAccessUnit(frame.data(), frame.size(), 0, true);
  uint8_t cc = ts[3] & 0x0F;
  EXPECT_EQ(cc, 0);
}

TEST_F(TsMuxerTest, EmptyDataReturnsEmpty) {
  auto ts = muxer.WriteAccessUnit(nullptr, 0, 0, false);
  EXPECT_TRUE(ts.empty());
}

// ============================================================
// HlsService Tests
// ============================================================

class HlsServiceTest : public ::testing::Test {
 protected:
  HlsConfig config;
  std::shared_ptr<HlsService> service;

  void SetUp() override {
    config.segment_duration_ms = 100;  // Short segments for testing
    config.max_segments = 3;
    config.max_segment_size_kb = 1024;
    service = std::make_shared<HlsService>(config);
  }

  static std::vector<uint8_t> MakeFakeKeyframe() {
    return TsMuxerTest::MakeFakeKeyframe();
  }

  static std::vector<uint8_t> MakeFakeInterframe() {
    return TsMuxerTest::MakeFakeInterframe();
  }
};

TEST_F(HlsServiceTest, RegisterAndUnregisterChannel) {
  service->RegisterChannel(1);
  EXPECT_TRUE(service->HasChannel(1));
  EXPECT_FALSE(service->HasChannel(2));

  service->UnregisterChannel(1);
  EXPECT_FALSE(service->HasChannel(1));
}

TEST_F(HlsServiceTest, EmptyPlaylistBeforeData) {
  service->RegisterChannel(1);
  auto playlist = service->GeneratePlaylist(1);
  EXPECT_TRUE(playlist.empty());
}

TEST_F(HlsServiceTest, UnregisteredChannelPlaylistEmpty) {
  auto playlist = service->GeneratePlaylist(99);
  EXPECT_TRUE(playlist.empty());
}

TEST_F(HlsServiceTest, PushFrameIgnoresUnregisteredChannel) {
  auto frame = MakeFakeKeyframe();
  service->PushFrame(99, frame.data(), frame.size(), 0, true);
  EXPECT_FALSE(service->HasChannel(99));
}

TEST_F(HlsServiceTest, FirstNonKeyframeIsDropped) {
  service->RegisterChannel(1);
  auto inter = MakeFakeInterframe();
  service->PushFrame(1, inter.data(), inter.size(), 0, false);
  auto playlist = service->GeneratePlaylist(1);
  EXPECT_TRUE(playlist.empty());
}

TEST_F(HlsServiceTest, SegmentCreatedAfterKeyframes) {
  service->RegisterChannel(1);
  auto keyframe = MakeFakeKeyframe();
  auto inter = MakeFakeInterframe();

  // First keyframe starts a segment
  service->PushFrame(1, keyframe.data(), keyframe.size(), 0, true);

  // Add some inter-frames
  for (int i = 1; i <= 5; ++i) {
    service->PushFrame(1, inter.data(), inter.size(),
                       static_cast<int64_t>(i) * 20000, false);
  }

  // Second keyframe after target duration → finalizes segment
  service->PushFrame(1, keyframe.data(), keyframe.size(), 200000, true);

  auto playlist = service->GeneratePlaylist(1);
  EXPECT_FALSE(playlist.empty());
  EXPECT_NE(playlist.find("#EXTM3U"), std::string::npos);
  EXPECT_NE(playlist.find("#EXT-X-MEDIA-SEQUENCE"), std::string::npos);
  EXPECT_NE(playlist.find(".ts"), std::string::npos);
}

TEST_F(HlsServiceTest, GetSegmentReturnsData) {
  service->RegisterChannel(1);
  auto keyframe = MakeFakeKeyframe();

  service->PushFrame(1, keyframe.data(), keyframe.size(), 0, true);
  service->PushFrame(1, keyframe.data(), keyframe.size(), 200000, true);

  auto segment = service->GetSegment(1, 0);
  ASSERT_NE(segment, nullptr);
  EXPECT_FALSE(segment->empty());
  // Data should be valid TS packets (start with 0x47)
  ASSERT_GE(segment->size(), 188u);
  EXPECT_EQ((*segment)[0], 0x47);
}

TEST_F(HlsServiceTest, SlidingWindowRemovesOldSegments) {
  service->RegisterChannel(1);
  auto keyframe = MakeFakeKeyframe();

  // Create more segments than max_segments (3)
  for (int i = 0; i < 6; ++i) {
    service->PushFrame(1, keyframe.data(), keyframe.size(),
                       static_cast<int64_t>(i) * 200000, true);
  }

  // Old segments should be gone
  EXPECT_EQ(service->GetSegment(1, 0), nullptr);
  EXPECT_EQ(service->GetSegment(1, 1), nullptr);

  // Recent segments should exist
  EXPECT_NE(service->GetSegment(1, 2), nullptr);
}

TEST_F(HlsServiceTest, PlaylistContainsCorrectFields) {
  service->RegisterChannel(1);
  auto keyframe = MakeFakeKeyframe();

  service->PushFrame(1, keyframe.data(), keyframe.size(), 0, true);
  service->PushFrame(1, keyframe.data(), keyframe.size(), 200000, true);
  service->PushFrame(1, keyframe.data(), keyframe.size(), 400000, true);

  auto playlist = service->GeneratePlaylist(1);
  EXPECT_NE(playlist.find("#EXTM3U"), std::string::npos);
  EXPECT_NE(playlist.find("#EXT-X-VERSION:3"), std::string::npos);
  EXPECT_NE(playlist.find("#EXT-X-TARGETDURATION:"), std::string::npos);
  EXPECT_NE(playlist.find("#EXTINF:"), std::string::npos);
}

TEST_F(HlsServiceTest, StopAllDeactivatesChannels) {
  service->RegisterChannel(1);
  auto keyframe = MakeFakeKeyframe();
  service->PushFrame(1, keyframe.data(), keyframe.size(), 0, true);

  service->StopAll();

  // After stop, push should be ignored
  service->PushFrame(1, keyframe.data(), keyframe.size(), 200000, true);
}

TEST_F(HlsServiceTest, MultipleChannelsIndependent) {
  service->RegisterChannel(1);
  service->RegisterChannel(2);

  auto keyframe = MakeFakeKeyframe();

  service->PushFrame(1, keyframe.data(), keyframe.size(), 0, true);
  service->PushFrame(1, keyframe.data(), keyframe.size(), 200000, true);

  // Channel 2 has no data yet
  EXPECT_TRUE(service->GeneratePlaylist(2).empty());
  // Channel 1 has data
  EXPECT_FALSE(service->GeneratePlaylist(1).empty());
}

}  // namespace loong::network
