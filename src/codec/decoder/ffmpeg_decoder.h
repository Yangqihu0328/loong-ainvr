// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CODEC_DECODER_FFMPEG_DECODER_H_
#define LOONG_CODEC_DECODER_FFMPEG_DECODER_H_

#include "codec/decoder/decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace loong::codec {

/// FFmpeg-based decoder implementation.
/// Supports H.264 and H.265 with software decoding.
class FFmpegDecoder : public Decoder {
 public:
  FFmpegDecoder();
  ~FFmpegDecoder() override;

  bool Initialize(CodecType codec, int width, int height) override;
  bool Decode(const uint8_t* data, size_t size,
              int64_t pts, int64_t dts, bool is_keyframe) override;
  void Flush() override;
  void Shutdown() override;
  std::string Name() const override;

 private:
  bool ConvertAndDeliver(AVFrame* av_frame, int64_t pts);

  AVCodecContext* codec_ctx_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  CodecType codec_type_ = CodecType::kUnknown;
  int width_ = 0;
  int height_ = 0;
  bool initialized_ = false;
  int64_t last_pts_ = AV_NOPTS_VALUE;
};

}  // namespace loong::codec

#endif  // LOONG_CODEC_DECODER_FFMPEG_DECODER_H_
