// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CODEC_ENCODER_FFMPEG_ENCODER_H_
#define LOONG_CODEC_ENCODER_FFMPEG_ENCODER_H_

#include "codec/encoder/encoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace loong::codec {

/// FFmpeg-based encoder implementation.
/// Supports H.264 and H.265 with software encoding.
class FFmpegEncoder : public Encoder {
 public:
  FFmpegEncoder();
  ~FFmpegEncoder() override;

  bool Initialize(const EncoderConfig& config) override;
  bool Encode(const std::shared_ptr<Frame>& frame) override;
  void Flush() override;
  void Shutdown() override;
  std::string Name() const override;

 private:
  bool SendAndReceive(AVFrame* av_frame);

  AVCodecContext* codec_ctx_ = nullptr;
  AVPacket* packet_ = nullptr;
  AVFrame* frame_ = nullptr;
  SwsContext* sws_ctx_ = nullptr;
  int64_t frame_count_ = 0;
  EncoderConfig config_;
  bool initialized_ = false;
};

}  // namespace loong::codec

#endif  // LOONG_CODEC_ENCODER_FFMPEG_ENCODER_H_
