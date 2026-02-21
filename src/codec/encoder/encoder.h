// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CODEC_ENCODER_ENCODER_H_
#define LOONG_CODEC_ENCODER_ENCODER_H_

#include "core/common/types.h"

#include <functional>
#include <memory>

namespace loong::codec {

/// Encoder configuration.
struct EncoderConfig {
  CodecType codec = CodecType::kH264;
  int width = 1920;
  int height = 1080;
  int framerate = 25;
  int bitrate_kbps = 4000;
  int gop_size = 50;              // Keyframe interval
  int max_b_frames = 0;           // B-frames (0 for low latency)
  std::string preset = "medium";  // Encoding speed preset
};

/// Abstract video encoder interface.
class Encoder {
 public:
  using PacketCallback =
      std::function<void(const uint8_t* data, size_t size, int64_t pts,
                         int64_t dts, bool is_keyframe)>;

  virtual ~Encoder() = default;

  /// Initialize the encoder with given configuration.
  virtual bool Initialize(const EncoderConfig& config) = 0;

  /// Encode a raw frame. Encoded packets delivered via callback.
  virtual bool Encode(const std::shared_ptr<Frame>& frame) = 0;

  /// Flush any buffered packets.
  virtual void Flush() = 0;

  /// Shutdown and release resources.
  virtual void Shutdown() = 0;

  /// Set the callback for encoded packets.
  void SetPacketCallback(PacketCallback callback) {
    packet_callback_ = std::move(callback);
  }

  /// Get encoder name.
  virtual std::string Name() const = 0;

 protected:
  PacketCallback packet_callback_;
};

}  // namespace loong::codec

#endif  // LOONG_CODEC_ENCODER_ENCODER_H_
