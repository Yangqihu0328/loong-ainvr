// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CODEC_CODEC_FACTORY_CODEC_FACTORY_H_
#define LOONG_CODEC_CODEC_FACTORY_CODEC_FACTORY_H_

#include <memory>

#include "codec/decoder/decoder.h"
#include "codec/encoder/encoder.h"
#include "core/common/types.h"

namespace loong::codec {

/// Factory for creating decoders and encoders.
/// Automatically selects hardware or software implementation
/// based on platform availability.
class CodecFactory {
 public:
  /// Create a decoder for the given codec type.
  static std::unique_ptr<Decoder> CreateDecoder(CodecType codec);

  /// Create an encoder with the given configuration.
  static std::unique_ptr<Encoder> CreateEncoder(const EncoderConfig& config);

  /// Check if hardware acceleration is available.
  static bool IsHardwareAccelAvailable();
};

}  // namespace loong::codec

#endif  // LOONG_CODEC_CODEC_FACTORY_CODEC_FACTORY_H_
