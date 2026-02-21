// Copyright 2026 Loong AI NVR Project

#include "network/rtsp_server/rtsp_server.h"
#include "network/rtsp_server/rtp_packetizer.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace loong {
namespace network {
namespace {

// ============================================================
// RTP Packetizer Tests
// ============================================================

TEST(RtpPacketizerTest, PacketizeSmallNal) {
  RtpPacketizer pkt(CodecType::kH264);
  pkt.SetSsrc(0xAABBCCDD);

  // Annex B start code + small NAL (SPS-like, 10 bytes).
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01};
  for (int i = 0; i < 10; ++i) {
    data.push_back(static_cast<uint8_t>(0x67 + i));
  }

  std::vector<std::vector<uint8_t>> packets;
  pkt.Packetize(data.data(), data.size(), 1000000,
                [&packets](const uint8_t* d, size_t s) {
                  packets.emplace_back(d, d + s);
                });

  ASSERT_EQ(packets.size(), 1u);

  // Check RTP-over-TCP interleaved header.
  EXPECT_EQ(packets[0][0], '$');
  EXPECT_EQ(packets[0][1], 0);  // Channel 0

  // Check RTP version.
  EXPECT_EQ((packets[0][4] >> 6) & 0x03, 2);  // V=2

  // Check SSRC (bytes 12-15 of interleaved frame = bytes 8-11 of RTP).
  uint32_t ssrc = (static_cast<uint32_t>(packets[0][12]) << 24) |
                  (static_cast<uint32_t>(packets[0][13]) << 16) |
                  (static_cast<uint32_t>(packets[0][14]) << 8) |
                  static_cast<uint32_t>(packets[0][15]);
  EXPECT_EQ(ssrc, 0xAABBCCDD);
}

TEST(RtpPacketizerTest, PacketizeLargeNalFragments) {
  RtpPacketizer pkt(CodecType::kH264);

  // Annex B + large NAL (2000 bytes).
  std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x65};  // IDR
  for (int i = 0; i < 1999; ++i) {
    data.push_back(static_cast<uint8_t>(i & 0xFF));
  }

  std::vector<std::vector<uint8_t>> packets;
  pkt.Packetize(data.data(), data.size(), 0,
                [&packets](const uint8_t* d, size_t s) {
                  packets.emplace_back(d, d + s);
                });

  // Should be fragmented into multiple packets.
  EXPECT_GT(packets.size(), 1u);
}

TEST(RtpPacketizerTest, MultipleNalUnits) {
  RtpPacketizer pkt(CodecType::kH264);

  // Two NAL units in one access unit.
  std::vector<uint8_t> data;
  // NAL 1
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0x00, 0x1E});
  // NAL 2
  data.insert(data.end(), {0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x38, 0x80});

  std::vector<std::vector<uint8_t>> packets;
  pkt.Packetize(data.data(), data.size(), 0,
                [&packets](const uint8_t* d, size_t s) {
                  packets.emplace_back(d, d + s);
                });

  EXPECT_EQ(packets.size(), 2u);

  // Only the last packet should have the marker bit.
  uint8_t last_rtp_byte1 = packets.back()[5];  // Byte 1 of RTP header
  EXPECT_NE(last_rtp_byte1 & 0x80, 0);  // Marker set
}

// ============================================================
// RTSP Server Tests
// ============================================================

TEST(RtspServerTest, StartAndStop) {
  RtspServer server("127.0.0.1", 19554);
  EXPECT_TRUE(server.Start());
  server.Stop();
}

TEST(RtspServerTest, RegisterAndUnregisterChannel) {
  RtspServer server("127.0.0.1", 19555);
  ASSERT_TRUE(server.Start());
  server.RegisterChannel(1, CodecType::kH264, 1920, 1080, 25);
  server.UnregisterChannel(1);
  server.Stop();
}

}  // namespace
}  // namespace network
}  // namespace loong
