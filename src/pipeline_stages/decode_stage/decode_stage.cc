// Copyright 2026 Loong AI NVR Project

#include "pipeline_stages/decode_stage/decode_stage.h"

#include <nlohmann/json.hpp>

#include "codec/codec_factory/codec_factory.h"
#include "spdlog/spdlog.h"

namespace loong::pipeline_stages {

bool DecodeStage::Initialize(const StageConfig& config) {
  try {
    auto params = nlohmann::json::parse(config.params);
    std::string codec_str = params.value("codec", "");
    if (codec_str == "h265" || codec_str == "hevc") {
      hint_codec_ = CodecType::kH265;
    } else if (codec_str == "h264") {
      hint_codec_ = CodecType::kH264;
    }
    hint_width_ = params.value("width", 0);
    hint_height_ = params.value("height", 0);
  } catch (...) {}

  if (hint_codec_ != CodecType::kUnknown && hint_width_ > 0 && hint_height_ > 0) {
    if (!InitDecoder(hint_codec_, hint_width_, hint_height_)) {
      return false;
    }
  }

  initialized_ = true;
  return true;
}

bool DecodeStage::InitDecoder(CodecType codec, int width, int height) {
  decoder_ = codec::CodecFactory::CreateDecoder(codec);
  if (!decoder_->Initialize(codec, width, height)) {
    spdlog::error("DecodeStage: decoder init failed");
    return false;
  }

  decoder_->SetFrameCallback(
      [this](std::shared_ptr<Frame> decoded) {
        PassToNext(std::move(decoded));
      });

  decoder_ready_ = true;
  spdlog::info("DecodeStage: initialized {} {}x{}",
               codec == CodecType::kH265 ? "H.265" : "H.264", width, height);
  return true;
}

bool DecodeStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  if (!initialized_ || !frame) return false;

  if (frame->type != FrameType::kEncoded) {
    return PassToNext(std::move(frame));
  }

  if (!decoder_ready_) {
    auto codec = frame->info.codec;
    if (codec == CodecType::kUnknown) codec = CodecType::kH264;
    if (!InitDecoder(codec, hint_width_ > 0 ? hint_width_ : 1920,
                     hint_height_ > 0 ? hint_height_ : 1080)) {
      return false;
    }
  }

  return decoder_->Decode(frame->packet_data.get(), frame->packet_size,
                          frame->pts, frame->dts,
                          frame->info.is_keyframe);
}

void DecodeStage::Shutdown() {
  if (decoder_) {
    decoder_->Flush();
    decoder_->Shutdown();
  }
  initialized_ = false;
  decoder_ready_ = false;
}

}  // namespace loong::pipeline_stages
