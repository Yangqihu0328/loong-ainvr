// Copyright 2026 Loong AI NVR Project

#include "codec/codec_factory/codec_factory.h"

extern "C" {
#include <libavcodec/avcodec.h>
}

#include "codec/decoder/ffmpeg_decoder.h"
#include "codec/encoder/ffmpeg_encoder.h"
#include "spdlog/spdlog.h"

namespace loong::codec {

std::unique_ptr<Decoder> CodecFactory::CreateDecoder(CodecType codec) {
  auto decoder = std::make_unique<FFmpegDecoder>();
  spdlog::info("CodecFactory: created FFmpeg decoder for {}",
               (codec == CodecType::kH264) ? "H.264" : "H.265");
  return decoder;
}

std::unique_ptr<Encoder> CodecFactory::CreateEncoder(
    const EncoderConfig& config) {
  auto encoder = std::make_unique<FFmpegEncoder>();
  spdlog::info("CodecFactory: created FFmpeg encoder for {}",
               (config.codec == CodecType::kH264) ? "H.264" : "H.265");
  return encoder;
}

bool CodecFactory::IsHardwareAccelAvailable() {
  // Probe FFmpeg for hardware decoder availability at runtime.
  // Check VAAPI (Linux), NVDEC/CUVID (NVIDIA), QSV (Intel) in order.
  const char* hw_decoders[] = {"h264_cuvid", "h264_vaapi", "h264_qsv"};

  for (const char* name : hw_decoders) {
    const AVCodec* codec = avcodec_find_decoder_by_name(name);
    if (codec != nullptr) {
      spdlog::info("CodecFactory: hardware decoder '{}' available", name);
      return true;
    }
  }

  return false;
}

}  // namespace loong::codec
