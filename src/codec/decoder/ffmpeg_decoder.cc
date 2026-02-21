// Copyright 2026 Loong AI NVR Project

#include "codec/decoder/ffmpeg_decoder.h"

#include "spdlog/spdlog.h"

#include <cstring>

namespace loong::codec {

FFmpegDecoder::FFmpegDecoder() = default;

FFmpegDecoder::~FFmpegDecoder() { Shutdown(); }

bool FFmpegDecoder::Initialize(CodecType codec, int width, int height) {
  codec_type_ = codec;
  width_ = width;
  height_ = height;

  AVCodecID codec_id =
      (codec == CodecType::kH264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;

  const AVCodec* av_codec = avcodec_find_decoder(codec_id);
  if (!av_codec) {
    spdlog::error("FFmpegDecoder: codec {} not found",
                  (codec == CodecType::kH264) ? "H.264" : "H.265");
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(av_codec);
  if (!codec_ctx_) {
    spdlog::error("FFmpegDecoder: failed to allocate codec context");
    return false;
  }

  codec_ctx_->width = width;
  codec_ctx_->height = height;
  codec_ctx_->thread_count = 2;

  if (avcodec_open2(codec_ctx_, av_codec, nullptr) < 0) {
    spdlog::error("FFmpegDecoder: failed to open codec");
    avcodec_free_context(&codec_ctx_);
    return false;
  }

  packet_ = av_packet_alloc();
  frame_ = av_frame_alloc();
  if (!packet_ || !frame_) {
    spdlog::error("FFmpegDecoder: failed to allocate packet/frame");
    Shutdown();
    return false;
  }

  initialized_ = true;
  spdlog::info("FFmpegDecoder: initialized {} ({}x{})",
               (codec == CodecType::kH264) ? "H.264" : "H.265", width, height);
  return true;
}

bool FFmpegDecoder::Decode(const uint8_t* data, size_t size, int64_t pts,
                           int64_t dts, bool is_keyframe) {
  if (!initialized_) return false;

  // Detect PTS discontinuity (e.g. file looped back to start) and flush the
  // decoder so stale reference frames don't cause "Missing reference picture".
  // Use DTS for more reliable ordering; fall back to PTS.
  int64_t ts = (dts != AV_NOPTS_VALUE) ? dts : pts;
  if (last_pts_ != AV_NOPTS_VALUE && ts != AV_NOPTS_VALUE &&
      ts + 1000 < last_pts_) {
    spdlog::info("FFmpegDecoder: timestamp jump ({} -> {}), flushing decoder",
                 last_pts_, ts);
    avcodec_flush_buffers(codec_ctx_);
  }
  if (ts != AV_NOPTS_VALUE) {
    last_pts_ = ts;
  }

  packet_->data = const_cast<uint8_t*>(data);
  packet_->size = static_cast<int>(size);
  packet_->pts = pts;
  packet_->dts = dts;
  if (is_keyframe) {
    packet_->flags |= AV_PKT_FLAG_KEY;
  }

  int ret = avcodec_send_packet(codec_ctx_, packet_);
  if (ret < 0) {
    spdlog::warn("FFmpegDecoder: send_packet error {}", ret);
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
      break;
    }
    if (ret < 0) {
      spdlog::warn("FFmpegDecoder: receive_frame error {}", ret);
      return false;
    }

    ConvertAndDeliver(frame_, pts);
    av_frame_unref(frame_);
  }

  return true;
}

void FFmpegDecoder::Flush() {
  if (!initialized_) return;

  avcodec_send_packet(codec_ctx_, nullptr);

  int ret = 0;
  while (ret >= 0) {
    ret = avcodec_receive_frame(codec_ctx_, frame_);
    if (ret < 0) break;
    ConvertAndDeliver(frame_, frame_->pts);
    av_frame_unref(frame_);
  }
}

void FFmpegDecoder::Shutdown() {
  if (sws_ctx_) {
    sws_freeContext(sws_ctx_);
    sws_ctx_ = nullptr;
  }
  if (frame_) {
    av_frame_free(&frame_);
    frame_ = nullptr;
  }
  if (packet_) {
    av_packet_free(&packet_);
    packet_ = nullptr;
  }
  if (codec_ctx_) {
    avcodec_free_context(&codec_ctx_);
    codec_ctx_ = nullptr;
  }
  initialized_ = false;
}

std::string FFmpegDecoder::Name() const { return "FFmpegDecoder"; }

bool FFmpegDecoder::ConvertAndDeliver(AVFrame* av_frame, int64_t pts) {
  if (!frame_callback_) return true;

  int dst_width = av_frame->width;
  int dst_height = av_frame->height;

  // Convert to BGR24 for OpenCV / AI processing
  if (!sws_ctx_) {
    sws_ctx_ = sws_getContext(dst_width, dst_height,
                              static_cast<AVPixelFormat>(av_frame->format),
                              dst_width, dst_height, AV_PIX_FMT_BGR24,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx_) {
      spdlog::error("FFmpegDecoder: failed to create sws context");
      return false;
    }
  }

  int bgr_size = dst_width * dst_height * 3;
  auto out_frame = std::make_shared<Frame>();
  out_frame->data = std::shared_ptr<uint8_t[]>(new uint8_t[bgr_size]);
  out_frame->data_size = static_cast<size_t>(bgr_size);
  out_frame->pts = pts;
  out_frame->type = FrameType::kRaw;
  out_frame->info.width = dst_width;
  out_frame->info.height = dst_height;
  out_frame->info.stride = dst_width * 3;
  out_frame->info.is_keyframe = (av_frame->key_frame != 0);

  uint8_t* dst_data[1] = {out_frame->data.get()};
  int dst_linesize[1] = {dst_width * 3};

  sws_scale(sws_ctx_, av_frame->data, av_frame->linesize, 0, dst_height,
            dst_data, dst_linesize);

  frame_callback_(std::move(out_frame));
  return true;
}

}  // namespace loong::codec
