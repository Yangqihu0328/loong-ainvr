// Copyright 2026 Loong AI NVR Project

#include "codec/codec_factory/codec_factory.h"
#include "codec/decoder/ffmpeg_decoder.h"
#include "codec/encoder/ffmpeg_encoder.h"

#include <atomic>
#include <cstring>
#include <memory>
#include <vector>

#include "core/common/types.h"
#include "gtest/gtest.h"

namespace loong::codec {
namespace {

constexpr int kTestWidth = 320;
constexpr int kTestHeight = 240;

/// Create a BGR24 test frame with a gradient pattern.
std::shared_ptr<Frame> MakeTestFrame(int width, int height) {
  auto frame = std::make_shared<Frame>();
  frame->type = FrameType::kRaw;
  frame->info.width = width;
  frame->info.height = height;
  frame->info.stride = width * 3;
  frame->data_size = static_cast<size_t>(width * height * 3);
  frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[frame->data_size]);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int offset = y * frame->info.stride + x * 3;
      frame->data[offset + 0] = static_cast<uint8_t>(x % 256);
      frame->data[offset + 1] = static_cast<uint8_t>(y % 256);
      frame->data[offset + 2] = static_cast<uint8_t>((x + y) % 256);
    }
  }
  return frame;
}

// ============================================================================
// EncoderConfig Tests
// ============================================================================

TEST(EncoderConfig, DefaultValues) {
  EncoderConfig config;
  EXPECT_EQ(config.codec, CodecType::kH264);
  EXPECT_EQ(config.width, 1920);
  EXPECT_EQ(config.height, 1080);
  EXPECT_EQ(config.framerate, 25);
  EXPECT_EQ(config.bitrate_kbps, 4000);
  EXPECT_EQ(config.gop_size, 50);
  EXPECT_EQ(config.max_b_frames, 0);
  EXPECT_EQ(config.preset, "medium");
}

TEST(EncoderConfig, CustomValues) {
  EncoderConfig config;
  config.codec = CodecType::kH265;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.framerate = 30;
  config.bitrate_kbps = 2000;
  config.gop_size = 30;
  config.preset = "ultrafast";

  EXPECT_EQ(config.codec, CodecType::kH265);
  EXPECT_EQ(config.width, kTestWidth);
  EXPECT_EQ(config.height, kTestHeight);
  EXPECT_EQ(config.framerate, 30);
  EXPECT_EQ(config.bitrate_kbps, 2000);
  EXPECT_EQ(config.gop_size, 30);
  EXPECT_EQ(config.preset, "ultrafast");
}

// ============================================================================
// CodecFactory Tests
// ============================================================================

TEST(CodecFactory, CreateDecoderH264) {
  auto decoder = CodecFactory::CreateDecoder(CodecType::kH264);
  ASSERT_NE(decoder, nullptr);
  EXPECT_EQ(decoder->Name(), "FFmpegDecoder");
}

TEST(CodecFactory, CreateDecoderH265) {
  auto decoder = CodecFactory::CreateDecoder(CodecType::kH265);
  ASSERT_NE(decoder, nullptr);
  EXPECT_EQ(decoder->Name(), "FFmpegDecoder");
}

TEST(CodecFactory, CreateEncoder) {
  EncoderConfig config;
  config.width = kTestWidth;
  config.height = kTestHeight;
  auto encoder = CodecFactory::CreateEncoder(config);
  ASSERT_NE(encoder, nullptr);
  EXPECT_EQ(encoder->Name(), "FFmpegEncoder");
}

TEST(CodecFactory, HardwareAccelNotAvailable) {
  EXPECT_FALSE(CodecFactory::IsHardwareAccelAvailable());
}

// ============================================================================
// FFmpegDecoder Tests
// ============================================================================

TEST(FFmpegDecoder, InitializeH264) {
  FFmpegDecoder decoder;
  EXPECT_TRUE(decoder.Initialize(CodecType::kH264, kTestWidth, kTestHeight));
  EXPECT_EQ(decoder.Name(), "FFmpegDecoder");
}

TEST(FFmpegDecoder, InitializeH265) {
  FFmpegDecoder decoder;
  EXPECT_TRUE(decoder.Initialize(CodecType::kH265, kTestWidth, kTestHeight));
}

TEST(FFmpegDecoder, DecodeWithoutInitReturnsFailure) {
  FFmpegDecoder decoder;
  uint8_t dummy_data[] = {0x00, 0x00, 0x00, 0x01};
  EXPECT_FALSE(decoder.Decode(dummy_data, sizeof(dummy_data), 0, 0, true));
}

TEST(FFmpegDecoder, FlushWithoutInitIsSafe) {
  FFmpegDecoder decoder;
  EXPECT_NO_FATAL_FAILURE(decoder.Flush());
}

TEST(FFmpegDecoder, ShutdownWithoutInitIsSafe) {
  FFmpegDecoder decoder;
  EXPECT_NO_FATAL_FAILURE(decoder.Shutdown());
}

TEST(FFmpegDecoder, DoubleShutdownIsSafe) {
  FFmpegDecoder decoder;
  decoder.Initialize(CodecType::kH264, kTestWidth, kTestHeight);
  EXPECT_NO_FATAL_FAILURE(decoder.Shutdown());
  EXPECT_NO_FATAL_FAILURE(decoder.Shutdown());
}

TEST(FFmpegDecoder, SetFrameCallback) {
  FFmpegDecoder decoder;
  int callback_count = 0;
  decoder.SetFrameCallback(
      [&callback_count](std::shared_ptr<Frame>) { ++callback_count; });
  decoder.Initialize(CodecType::kH264, kTestWidth, kTestHeight);
  EXPECT_EQ(callback_count, 0);
}

TEST(FFmpegDecoder, DecodeInvalidDataGraceful) {
  FFmpegDecoder decoder;
  decoder.Initialize(CodecType::kH264, kTestWidth, kTestHeight);

  uint8_t garbage[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x02, 0x03};
  // Invalid data should not crash, may return false
  decoder.Decode(garbage, sizeof(garbage), 0, 0, false);
}

// ============================================================================
// FFmpegEncoder Tests
// ============================================================================

TEST(FFmpegEncoder, InitializeH264) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.codec = CodecType::kH264;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.framerate = 25;
  config.bitrate_kbps = 1000;
  config.preset = "ultrafast";
  EXPECT_TRUE(encoder.Initialize(config));
  EXPECT_EQ(encoder.Name(), "FFmpegEncoder");
}

TEST(FFmpegEncoder, InitializeH265) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.codec = CodecType::kH265;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.framerate = 25;
  config.bitrate_kbps = 1000;
  EXPECT_TRUE(encoder.Initialize(config));
}

TEST(FFmpegEncoder, EncodeWithoutInitReturnsFailure) {
  FFmpegEncoder encoder;
  auto frame = MakeTestFrame(kTestWidth, kTestHeight);
  EXPECT_FALSE(encoder.Encode(frame));
}

TEST(FFmpegEncoder, EncodeNullFrameReturnsFailure) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.preset = "ultrafast";
  encoder.Initialize(config);
  EXPECT_FALSE(encoder.Encode(nullptr));
}

TEST(FFmpegEncoder, FlushWithoutInitIsSafe) {
  FFmpegEncoder encoder;
  EXPECT_NO_FATAL_FAILURE(encoder.Flush());
}

TEST(FFmpegEncoder, ShutdownWithoutInitIsSafe) {
  FFmpegEncoder encoder;
  EXPECT_NO_FATAL_FAILURE(encoder.Shutdown());
}

TEST(FFmpegEncoder, DoubleShutdownIsSafe) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.preset = "ultrafast";
  encoder.Initialize(config);
  EXPECT_NO_FATAL_FAILURE(encoder.Shutdown());
  EXPECT_NO_FATAL_FAILURE(encoder.Shutdown());
}

TEST(FFmpegEncoder, EncodeFrameProducesPacket) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.codec = CodecType::kH264;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.framerate = 25;
  config.bitrate_kbps = 1000;
  config.gop_size = 10;
  config.preset = "ultrafast";
  ASSERT_TRUE(encoder.Initialize(config));

  std::atomic<int> packet_count{0};
  encoder.SetPacketCallback(
      [&packet_count](const uint8_t* data, size_t size,
                      int64_t /*pts*/, int64_t /*dts*/, bool /*is_keyframe*/) {
        EXPECT_NE(data, nullptr);
        EXPECT_GT(size, 0U);
        ++packet_count;
      });

  // Encode multiple frames to trigger packet output (encoder may buffer)
  for (int i = 0; i < 30; ++i) {
    auto frame = MakeTestFrame(kTestWidth, kTestHeight);
    frame->pts = static_cast<int64_t>(i) * 40000;
    EXPECT_TRUE(encoder.Encode(frame));
  }

  encoder.Flush();
  EXPECT_GT(packet_count.load(), 0)
      << "Encoder should produce at least one packet after 30 frames + flush";
}

TEST(FFmpegEncoder, PacketCallbackReceivesKeyframe) {
  FFmpegEncoder encoder;
  EncoderConfig config;
  config.codec = CodecType::kH264;
  config.width = kTestWidth;
  config.height = kTestHeight;
  config.framerate = 25;
  config.bitrate_kbps = 1000;
  config.gop_size = 5;
  config.preset = "ultrafast";
  ASSERT_TRUE(encoder.Initialize(config));

  bool got_keyframe = false;
  encoder.SetPacketCallback(
      [&got_keyframe](const uint8_t* /*data*/, size_t /*size*/,
                      int64_t /*pts*/, int64_t /*dts*/, bool is_keyframe) {
        if (is_keyframe) got_keyframe = true;
      });

  for (int i = 0; i < 20; ++i) {
    auto frame = MakeTestFrame(kTestWidth, kTestHeight);
    frame->pts = static_cast<int64_t>(i) * 40000;
    encoder.Encode(frame);
  }
  encoder.Flush();

  EXPECT_TRUE(got_keyframe) << "Should receive at least one keyframe";
}

// ============================================================================
// End-to-End: Encode → Decode roundtrip
// ============================================================================

TEST(CodecRoundtrip, EncodeAndDecodeH264) {
  // Step 1: Encode test frames to produce H.264 packets
  FFmpegEncoder encoder;
  EncoderConfig enc_config;
  enc_config.codec = CodecType::kH264;
  enc_config.width = kTestWidth;
  enc_config.height = kTestHeight;
  enc_config.framerate = 25;
  enc_config.bitrate_kbps = 1000;
  enc_config.gop_size = 10;
  enc_config.preset = "ultrafast";
  ASSERT_TRUE(encoder.Initialize(enc_config));

  struct Packet {
    std::vector<uint8_t> data;
    int64_t pts;
    int64_t dts;
    bool is_keyframe;
  };
  std::vector<Packet> packets;

  encoder.SetPacketCallback(
      [&packets](const uint8_t* data, size_t size,
                 int64_t pts, int64_t dts, bool is_keyframe) {
        Packet pkt;
        pkt.data.assign(data, data + size);
        pkt.pts = pts;
        pkt.dts = dts;
        pkt.is_keyframe = is_keyframe;
        packets.push_back(std::move(pkt));
      });

  for (int i = 0; i < 20; ++i) {
    auto frame = MakeTestFrame(kTestWidth, kTestHeight);
    frame->pts = static_cast<int64_t>(i) * 40000;
    encoder.Encode(frame);
  }
  encoder.Flush();
  ASSERT_GT(packets.size(), 0U) << "Encoder must produce packets";

  // Step 2: Decode the encoded packets
  FFmpegDecoder decoder;
  ASSERT_TRUE(decoder.Initialize(CodecType::kH264, kTestWidth, kTestHeight));

  std::atomic<int> decoded_frames{0};
  decoder.SetFrameCallback(
      [&decoded_frames](std::shared_ptr<Frame> frame) {
        ASSERT_NE(frame, nullptr);
        EXPECT_EQ(frame->type, FrameType::kRaw);
        EXPECT_GT(frame->data_size, 0U);
        EXPECT_EQ(frame->info.width, kTestWidth);
        EXPECT_EQ(frame->info.height, kTestHeight);
        ++decoded_frames;
      });

  for (const auto& pkt : packets) {
    decoder.Decode(pkt.data.data(), pkt.data.size(),
                   pkt.pts, pkt.dts, pkt.is_keyframe);
  }
  decoder.Flush();

  EXPECT_GT(decoded_frames.load(), 0)
      << "Decoder should produce frames from valid H.264 packets";
}

}  // namespace
}  // namespace loong::codec
