// Copyright 2026 Loong AI NVR Project

#include "pipeline_stages/input_stage/input_stage.h"

#include "spdlog/spdlog.h"

#include <cstring>
#include <nlohmann/json.hpp>

namespace loong::pipeline_stages {

InputStage::~InputStage() { Shutdown(); }

bool InputStage::Initialize(const StageConfig& config) {
  // Parse RTSP URL and channel_id from config params
  try {
    auto params = nlohmann::json::parse(config.params);
    rtsp_config_.url = params.value("rtsp_url", "");
    channel_id_ = params.value("channel_id", -1);

    // Optional transport mode (default: TCP).
    std::string transport = params.value("rtsp_transport", "tcp");
    rtsp_config_.transport = (transport == "udp")
                                 ? video_input::RtspTransport::kUdp
                                 : video_input::RtspTransport::kTcp;

    // Optional reconnection settings.
    rtsp_config_.auto_reconnect = params.value("auto_reconnect", true);
    rtsp_config_.connect_timeout_ms = params.value("connect_timeout_ms", 5000);
    rtsp_config_.reconnect_delay_ms = params.value("reconnect_delay_ms", 3000);
    rtsp_config_.max_reconnect_attempts =
        params.value("max_reconnect_attempts", 0);
  } catch (const std::exception& e) {
    spdlog::error("InputStage: failed to parse config: {}", e.what());
    return false;
  }

  if (rtsp_config_.url.empty()) {
    spdlog::error("InputStage: RTSP URL is empty");
    return false;
  }

  spdlog::info("InputStage ch{}: initialized with URL {}", channel_id_,
               rtsp_config_.url);
  return true;
}

bool InputStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  // InputStage is a source — it generates frames rather than
  // processing incoming ones. Just forward if called.
  return PassToNext(std::move(frame));
}

void InputStage::OnPipelineStart() {
  spdlog::info("InputStage ch{}: starting RTSP pull", channel_id_);
  if (!StartPulling(channel_id_)) {
    spdlog::error("InputStage ch{}: failed to start RTSP pulling", channel_id_);
  }
}

void InputStage::OnPipelineStop() {
  spdlog::info("InputStage ch{}: stopping RTSP pull", channel_id_);
  StopPulling();
}

void InputStage::Shutdown() { StopPulling(); }

bool InputStage::StartPulling(int channel_id) {
  channel_id_ = channel_id;

  client_ = std::make_unique<video_input::RtspClient>();

  // Wire the packet callback so received packets are turned into
  // Frame objects and forwarded downstream.
  client_->SetPacketCallback([this](int ch, const uint8_t* data, size_t size,
                                    int64_t pts, int64_t dts, bool is_keyframe,
                                    CodecType codec) {
    OnPacket(ch, data, size, pts, dts, is_keyframe, codec);
  });

  if (!client_->Open(rtsp_config_, channel_id_)) {
    spdlog::error("InputStage ch{}: failed to open RTSP stream", channel_id_);
    client_.reset();
    return false;
  }

  return true;
}

void InputStage::StopPulling() {
  if (client_) {
    client_->Close();
    client_.reset();
  }
}

void InputStage::OnPacket(int channel_id, const uint8_t* data, size_t size,
                          int64_t pts, int64_t dts, bool is_keyframe,
                          CodecType codec) {
  auto frame = std::make_shared<Frame>();
  frame->channel_id = channel_id;
  frame->pts = pts;
  frame->dts = dts;
  frame->type = FrameType::kEncoded;
  frame->info.is_keyframe = is_keyframe;
  frame->info.codec = codec;

  // Copy packet data into the frame.
  frame->packet_data = std::shared_ptr<uint8_t[]>(new uint8_t[size]);
  std::memcpy(frame->packet_data.get(), data, size);
  frame->packet_size = size;

  PassToNext(std::move(frame));
}

video_input::RtspStreamInfo InputStage::GetStreamInfo() const {
  if (client_) {
    return client_->GetStreamInfo();
  }
  return {};
}

}  // namespace loong::pipeline_stages
