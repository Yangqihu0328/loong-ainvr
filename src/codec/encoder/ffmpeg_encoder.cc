// Copyright 2026 Loong AI NVR Project

#include "codec/encoder/ffmpeg_encoder.h"

#include "spdlog/spdlog.h"

namespace loong::codec {

FFmpegEncoder::FFmpegEncoder() = default;

FFmpegEncoder::~FFmpegEncoder() { Shutdown(); }

bool FFmpegEncoder::Initialize(const EncoderConfig& config) {
  config_ = config;

  AVCodecID codec_id =
      (config.codec == CodecType::kH264) ? AV_CODEC_ID_H264 : AV_CODEC_ID_HEVC;

  const AVCodec* av_codec = avcodec_find_encoder(codec_id);
  if (!av_codec) {
    spdlog::error("FFmpegEncoder: encoder {} not found",
                  (config.codec == CodecType::kH264) ? "H.264" : "H.265");
    return false;
  }

  codec_ctx_ = avcodec_alloc_context3(av_codec);
  if (!codec_ctx_) {
    spdlog::error("FFmpegEncoder: failed to allocate codec context");
    return false;
  }

  codec_ctx_->width = config.width;
  codec_ctx_->height = config.height;
  codec_ctx_->time_base = {1, 1000000};  // Microsecond timebase for FLV/HLS
  codec_ctx_->framerate = {config.framerate, 1};
  codec_ctx_->bit_rate = static_cast<int64_t>(config.bitrate_kbps) * 1000;
  codec_ctx_->gop_size = config.gop_size;
  codec_ctx_->max_b_frames = config.max_b_frames;
  codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;
  codec_ctx_->thread_count = 2;

  // Low-latency tuning
  if (config.codec == CodecType::kH264) {
    av_opt_set(codec_ctx_->priv_data, "preset", config.preset.c_str(), 0);
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);
  }

  if (avcodec_open2(codec_ctx_, av_codec, nullptr) < 0) {
    spdlog::error("FFmpegEncoder: failed to open encoder");
    avcodec_free_context(&codec_ctx_);
    return false;
  }

  packet_ = av_packet_alloc();
  frame_ = av_frame_alloc();
  if (!packet_ || !frame_) {
    spdlog::error("FFmpegEncoder: failed to allocate packet/frame");
    Shutdown();
    return false;
  }

  frame_->format = AV_PIX_FMT_YUV420P;
  frame_->width = config.width;
  frame_->height = config.height;
  if (av_frame_get_buffer(frame_, 0) < 0) {
    spdlog::error("FFmpegEncoder: failed to allocate frame buffer");
    Shutdown();
    return false;
  }

  initialized_ = true;
  spdlog::info("FFmpegEncoder: initialized {} ({}x{} @{}kbps)",
               (config.codec == CodecType::kH264) ? "H.264" : "H.265",
               config.width, config.height, config.bitrate_kbps);
  return true;
}

bool FFmpegEncoder::Encode(const std::shared_ptr<Frame>& input_frame) {
  if (!initialized_ || !input_frame) return false;

  if (av_frame_make_writable(frame_) < 0) {
    spdlog::warn("FFmpegEncoder: frame not writable");
    return false;
  }

  // Convert BGR24 input to YUV420P
  if (!sws_ctx_) {
    sws_ctx_ = sws_getContext(input_frame->info.width, input_frame->info.height,
                              AV_PIX_FMT_BGR24, config_.width, config_.height,
                              AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr,
                              nullptr, nullptr);
    if (!sws_ctx_) {
      spdlog::error("FFmpegEncoder: failed to create sws context");
      return false;
    }
  }

  const uint8_t* src_data[1] = {input_frame->data.get()};
  int src_linesize[1] = {input_frame->info.stride};

  sws_scale(sws_ctx_, src_data, src_linesize, 0, input_frame->info.height,
            frame_->data, frame_->linesize);

  int fps = config_.framerate > 0 ? config_.framerate : 30;
  frame_->pts = frame_count_++ * 1000000LL / fps;

  return SendAndReceive(frame_);
}

void FFmpegEncoder::Flush() {
  if (!initialized_) return;
  SendAndReceive(nullptr);
}

void FFmpegEncoder::Shutdown() {
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
  frame_count_ = 0;
}

std::string FFmpegEncoder::Name() const { return "FFmpegEncoder"; }

bool FFmpegEncoder::SendAndReceive(AVFrame* av_frame) {
  int ret = avcodec_send_frame(codec_ctx_, av_frame);
  if (ret < 0) {
    spdlog::warn("FFmpegEncoder: send_frame error {}", ret);
    return false;
  }

  while (ret >= 0) {
    ret = avcodec_receive_packet(codec_ctx_, packet_);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) {
      spdlog::warn("FFmpegEncoder: receive_packet error {}", ret);
      return false;
    }

    if (packet_callback_) {
      bool is_key = (packet_->flags & AV_PKT_FLAG_KEY) != 0;
      packet_callback_(packet_->data, static_cast<size_t>(packet_->size),
                       packet_->pts, packet_->dts, is_key);
    }

    av_packet_unref(packet_);
  }

  return true;
}

}  // namespace loong::codec
