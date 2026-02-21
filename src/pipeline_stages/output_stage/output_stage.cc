// Copyright 2026 Loong AI NVR Project

#include "pipeline_stages/output_stage/output_stage.h"

#include <nlohmann/json.hpp>

#include "codec/codec_factory/codec_factory.h"
#include "spdlog/spdlog.h"

namespace loong::pipeline_stages {

bool OutputStage::Initialize(const StageConfig& config) {
  try {
    auto params = nlohmann::json::parse(config.params);
    std::string codec_str = params.value("codec", "h264");
    enc_config_.codec = (codec_str == "h265" || codec_str == "hevc")
                            ? CodecType::kH265
                            : CodecType::kH264;
    enc_config_.width = params.value("width", 1920);
    enc_config_.height = params.value("height", 1080);
    enc_config_.framerate = params.value("framerate", 30);
    enc_config_.bitrate_kbps = params.value("bitrate_kbps", 4000);
    enc_config_.gop_size = params.value("gop_size", 50);
    enc_config_.preset = params.value("preset", "ultrafast");
    channel_id_ = params.value("channel_id", -1);
  } catch (...) {
  }

  // Encoder creation is deferred to the first frame so we can use the
  // actual decoded resolution instead of the (possibly wrong) config values.
  initialized_ = true;
  encoder_ready_ = false;
  return true;
}

bool OutputStage::InitEncoder(int width, int height) {
  enc_config_.width = width;
  enc_config_.height = height;

  encoder_ = codec::CodecFactory::CreateEncoder(enc_config_);
  if (!encoder_->Initialize(enc_config_)) {
    spdlog::error("OutputStage: encoder init failed ({}x{})", width, height);
    return false;
  }

  encoder_->SetPacketCallback(
      [this](const uint8_t* data, size_t size,
             int64_t pts, int64_t /*dts*/, bool is_key) {
        if (output_callback_) {
          output_callback_(channel_id_, data, size, pts, is_key);
        }
      });

  encoder_ready_ = true;
  return true;
}

bool OutputStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  if (!initialized_ || !frame) return false;

  if (frame->type != FrameType::kRaw) {
    if (output_callback_ && frame->packet_data) {
      output_callback_(frame->channel_id, frame->packet_data.get(),
                       frame->packet_size, frame->pts,
                       frame->info.is_keyframe);
    }
    return true;
  }

  // Lazy-init encoder with the actual frame dimensions.
  if (!encoder_ready_) {
    if (!InitEncoder(frame->info.width, frame->info.height)) {
      return false;
    }
  }

  return encoder_->Encode(frame);
}

void OutputStage::Shutdown() {
  if (encoder_) {
    encoder_->Flush();
    encoder_->Shutdown();
  }
  initialized_ = false;
}

}  // namespace loong::pipeline_stages
