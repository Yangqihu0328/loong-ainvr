// Copyright 2026 Loong AI NVR Project

#include "network/media_stream/fmp4_muxer.h"
#include "network/media_stream/ws_media_service.h"

#include <gtest/gtest.h>

#include <cstring>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace loong {
namespace network {
namespace {

// ============================================================
// Test Helpers
// ============================================================

// Build a minimal H.264 Annex B bitstream with SPS + PPS + IDR.
std::vector<uint8_t> MakeH264Keyframe() {
  std::vector<uint8_t> data;
  // SPS: start code + nal_type=7 + minimal profile info
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x67, 0x42, 0x00, 0x1E, 0xAB, 0x40, 0x80});
  // PPS: start code + nal_type=8
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x68, 0xCE, 0x38, 0x80});
  // IDR: start code + nal_type=5
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.push_back(0x65);
  for (int i = 0; i < 100; ++i) {
    data.push_back(static_cast<uint8_t>(i & 0xFF));
  }
  return data;
}

// Build a minimal H.264 Annex B P-frame.
std::vector<uint8_t> MakeH264PFrame() {
  std::vector<uint8_t> data;
  // P-frame: start code + nal_type=1
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.push_back(0x41);
  for (int i = 0; i < 50; ++i) {
    data.push_back(static_cast<uint8_t>(i & 0xFF));
  }
  return data;
}

// Build a minimal H.265 Annex B bitstream with VPS + SPS + PPS + IDR.
std::vector<uint8_t> MakeH265Keyframe() {
  std::vector<uint8_t> data;
  // VPS: start code + nal_type=32 (0x40 0x01)
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x40, 0x01, 0x0C, 0x01, 0xFF, 0xFF, 0x01, 0x60,
                           0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
                           0x03, 0x00, 0x00, 0x03, 0x00, 0x5D, 0xAC, 0x09});
  // SPS: start code + nal_type=33 (0x42 0x01)
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03,
                           0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00,
                           0x03, 0x00, 0x5D, 0xA0});
  // PPS: start code + nal_type=34 (0x44 0x01)
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x44, 0x01, 0xC0, 0xF7, 0xC0, 0xCC, 0x90});
  // IDR: start code + nal_type=19 (0x26 0x01)
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01});
  data.insert(data.end(), {0x26, 0x01});
  for (int i = 0; i < 100; ++i) {
    data.push_back(static_cast<uint8_t>(i & 0xFF));
  }
  return data;
}

// Verify an MP4 box header at a given offset.
// Returns the box size, or 0 on failure.
uint32_t CheckBox(const std::vector<uint8_t>& data, size_t offset,
                  const char type[4]) {
  if (offset + 8 > data.size()) return 0;
  uint32_t size = (static_cast<uint32_t>(data[offset]) << 24) |
                  (static_cast<uint32_t>(data[offset + 1]) << 16) |
                  (static_cast<uint32_t>(data[offset + 2]) << 8) |
                  static_cast<uint32_t>(data[offset + 3]);
  if (memcmp(&data[offset + 4], type, 4) != 0) return 0;
  return size;
}

// ============================================================
// fMP4 Muxer Tests — H.264
// ============================================================

TEST(Fmp4MuxerTest, ExtractH264Params) {
  auto keyframe = MakeH264Keyframe();
  std::vector<uint8_t> sps, pps;
  EXPECT_TRUE(
      Fmp4Muxer::ExtractH264Params(keyframe.data(), keyframe.size(), sps, pps));
  EXPECT_FALSE(sps.empty());
  EXPECT_FALSE(pps.empty());

  // SPS should start with nal_type 7
  EXPECT_EQ(sps[0] & 0x1F, 7);
  // PPS should start with nal_type 8
  EXPECT_EQ(pps[0] & 0x1F, 8);
}

TEST(Fmp4MuxerTest, ExtractH264ParamsFromPFrame) {
  auto pframe = MakeH264PFrame();
  std::vector<uint8_t> sps, pps;
  // P-frames don't contain SPS/PPS
  EXPECT_FALSE(
      Fmp4Muxer::ExtractH264Params(pframe.data(), pframe.size(), sps, pps));
}

TEST(Fmp4MuxerTest, MakeH264InitSegment) {
  auto keyframe = MakeH264Keyframe();
  std::vector<uint8_t> sps, pps;
  ASSERT_TRUE(
      Fmp4Muxer::ExtractH264Params(keyframe.data(), keyframe.size(), sps, pps));

  auto init = Fmp4Muxer::MakeH264InitSegment(sps, pps, 1920, 1080);
  EXPECT_FALSE(init.empty());

  // Verify ftyp box
  uint32_t ftyp_size = CheckBox(init, 0, "ftyp");
  EXPECT_GT(ftyp_size, 0u);

  // Verify moov box follows ftyp
  uint32_t moov_size = CheckBox(init, ftyp_size, "moov");
  EXPECT_GT(moov_size, 0u);

  // Total size should match
  EXPECT_EQ(init.size(), static_cast<size_t>(ftyp_size + moov_size));
}

TEST(Fmp4MuxerTest, MakeH264InitSegmentInvalidInput) {
  std::vector<uint8_t> empty_sps, empty_pps;
  auto init = Fmp4Muxer::MakeH264InitSegment(empty_sps, empty_pps, 1920, 1080);
  EXPECT_TRUE(init.empty());
}

TEST(Fmp4MuxerTest, MakeH264MediaSegment) {
  auto keyframe = MakeH264Keyframe();

  auto segment = Fmp4Muxer::MakeMediaSegment(
      keyframe.data(), keyframe.size(),
      0, 3600, 1, true, CodecType::kH264);
  EXPECT_FALSE(segment.empty());

  // Verify moof box
  uint32_t moof_size = CheckBox(segment, 0, "moof");
  EXPECT_GT(moof_size, 0u);

  // Verify mdat box follows moof
  uint32_t mdat_size = CheckBox(segment, moof_size, "mdat");
  EXPECT_GT(mdat_size, 0u);

  // Total size
  EXPECT_EQ(segment.size(), static_cast<size_t>(moof_size + mdat_size));
}

TEST(Fmp4MuxerTest, MakeH264MediaSegmentPFrame) {
  auto pframe = MakeH264PFrame();

  auto segment = Fmp4Muxer::MakeMediaSegment(
      pframe.data(), pframe.size(),
      3600, 3600, 2, false, CodecType::kH264);
  EXPECT_FALSE(segment.empty());

  uint32_t moof_size = CheckBox(segment, 0, "moof");
  EXPECT_GT(moof_size, 0u);
}

// ============================================================
// fMP4 Muxer Tests — H.265
// ============================================================

TEST(Fmp4MuxerTest, ExtractH265Params) {
  auto keyframe = MakeH265Keyframe();
  std::vector<uint8_t> vps, sps, pps;
  EXPECT_TRUE(Fmp4Muxer::ExtractH265Params(keyframe.data(), keyframe.size(),
                                             vps, sps, pps));
  EXPECT_FALSE(vps.empty());
  EXPECT_FALSE(sps.empty());
  EXPECT_FALSE(pps.empty());

  // VPS nal_type should be 32
  EXPECT_EQ((vps[0] >> 1) & 0x3F, 32);
  // SPS nal_type should be 33
  EXPECT_EQ((sps[0] >> 1) & 0x3F, 33);
  // PPS nal_type should be 34
  EXPECT_EQ((pps[0] >> 1) & 0x3F, 34);
}

TEST(Fmp4MuxerTest, MakeH265InitSegment) {
  auto keyframe = MakeH265Keyframe();
  std::vector<uint8_t> vps, sps, pps;
  ASSERT_TRUE(Fmp4Muxer::ExtractH265Params(keyframe.data(), keyframe.size(),
                                             vps, sps, pps));

  auto init = Fmp4Muxer::MakeH265InitSegment(vps, sps, pps, 1920, 1080);
  EXPECT_FALSE(init.empty());

  // Verify ftyp box
  uint32_t ftyp_size = CheckBox(init, 0, "ftyp");
  EXPECT_GT(ftyp_size, 0u);

  // Verify moov box follows ftyp
  uint32_t moov_size = CheckBox(init, ftyp_size, "moov");
  EXPECT_GT(moov_size, 0u);
}

TEST(Fmp4MuxerTest, MakeH265MediaSegment) {
  auto keyframe = MakeH265Keyframe();

  auto segment = Fmp4Muxer::MakeMediaSegment(
      keyframe.data(), keyframe.size(),
      0, 3600, 1, true, CodecType::kH265);
  EXPECT_FALSE(segment.empty());

  uint32_t moof_size = CheckBox(segment, 0, "moof");
  EXPECT_GT(moof_size, 0u);

  uint32_t mdat_size = CheckBox(segment, moof_size, "mdat");
  EXPECT_GT(mdat_size, 0u);
}

TEST(Fmp4MuxerTest, SequentialMediaSegments) {
  auto keyframe = MakeH264Keyframe();
  auto pframe = MakeH264PFrame();

  std::vector<uint8_t> sps, pps;
  ASSERT_TRUE(Fmp4Muxer::ExtractH264Params(keyframe.data(), keyframe.size(),
                                             sps, pps));

  auto init = Fmp4Muxer::MakeH264InitSegment(sps, pps, 1920, 1080);
  EXPECT_FALSE(init.empty());

  // Generate multiple sequential segments
  for (uint32_t seq = 1; seq <= 10; ++seq) {
    bool is_key = (seq == 1);
    const auto& frame = is_key ? keyframe : pframe;
    uint64_t decode_time = static_cast<uint64_t>((seq - 1) * 3600);

    auto segment = Fmp4Muxer::MakeMediaSegment(
        frame.data(), frame.size(), decode_time, 3600, seq,
        is_key, CodecType::kH264);
    EXPECT_FALSE(segment.empty());
  }
}

// ============================================================
// WsMediaService Tests
// ============================================================

TEST(WsMediaServiceTest, StartAndStop) {
  WsMediaService service("127.0.0.1", 19082);
  EXPECT_TRUE(service.Start());
  service.Stop();
}

TEST(WsMediaServiceTest, RegisterAndUnregisterChannel) {
  WsMediaService service("127.0.0.1", 19083);
  ASSERT_TRUE(service.Start());

  service.RegisterChannel(1, CodecType::kH264, 1920, 1080);
  service.RegisterChannel(2, CodecType::kH265, 1920, 1080);
  service.UnregisterChannel(1);
  service.UnregisterChannel(2);

  service.Stop();
}

TEST(WsMediaServiceTest, PushFrameWithoutViewers) {
  WsMediaService service("127.0.0.1", 19084);
  ASSERT_TRUE(service.Start());

  service.RegisterChannel(1, CodecType::kH264, 1920, 1080);

  auto keyframe = MakeH264Keyframe();
  service.PushFrame(1, keyframe.data(), keyframe.size(), 0, true);

  auto pframe = MakeH264PFrame();
  service.PushFrame(1, pframe.data(), pframe.size(), 40000, false);

  EXPECT_EQ(service.ViewerCount(), 0u);

  service.UnregisterChannel(1);
  service.Stop();
}

TEST(WsMediaServiceTest, PushFrameToUnregisteredChannel) {
  WsMediaService service("127.0.0.1", 19085);
  ASSERT_TRUE(service.Start());

  auto keyframe = MakeH264Keyframe();
  // Should not crash when pushing to non-existent channel
  service.PushFrame(999, keyframe.data(), keyframe.size(), 0, true);

  service.Stop();
}

TEST(WsMediaServiceTest, H265PushFrame) {
  WsMediaService service("127.0.0.1", 19086);
  ASSERT_TRUE(service.Start());

  service.RegisterChannel(1, CodecType::kH265, 1920, 1080);

  auto keyframe = MakeH265Keyframe();
  service.PushFrame(1, keyframe.data(), keyframe.size(), 0, true);

  service.UnregisterChannel(1);
  service.Stop();
}

}  // namespace
}  // namespace network
}  // namespace loong
