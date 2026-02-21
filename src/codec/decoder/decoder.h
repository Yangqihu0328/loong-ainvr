// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CODEC_DECODER_DECODER_H_
#define LOONG_CODEC_DECODER_DECODER_H_

#include "core/common/types.h"

#include <functional>
#include <memory>

namespace loong::codec {

/// Abstract video decoder interface.
/// Supports H.264 and H.265, with software or hardware acceleration.
class Decoder {
 public:
  using FrameCallback = std::function<void(std::shared_ptr<Frame>)>;

  virtual ~Decoder() = default;

  /// Initialize the decoder for the given codec type and resolution.
  virtual bool Initialize(CodecType codec, int width, int height) = 0;

  /// Decode an encoded packet. Decoded frames are delivered via callback.
  virtual bool Decode(const uint8_t* data, size_t size, int64_t pts,
                      int64_t dts, bool is_keyframe) = 0;

  /// Flush any buffered frames.
  virtual void Flush() = 0;

  /// Shutdown and release resources.
  virtual void Shutdown() = 0;

  /// Set the callback for decoded frames.
  void SetFrameCallback(FrameCallback callback) {
    frame_callback_ = std::move(callback);
  }

  /// Get codec name (for logging).
  virtual std::string Name() const = 0;

 protected:
  FrameCallback frame_callback_;
};

}  // namespace loong::codec

#endif  // LOONG_CODEC_DECODER_DECODER_H_
