// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_PIPELINE_STAGES_INPUT_STAGE_INPUT_STAGE_H_
#define LOONG_PIPELINE_STAGES_INPUT_STAGE_INPUT_STAGE_H_

#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"
#include "video_input/rtsp_client/rtsp_client.h"

#include <atomic>
#include <memory>
#include <string>

namespace loong::pipeline_stages {

/// RTSP input stage — pulls video packets from an RTSP source
/// and pushes encoded frames downstream for decoding.
///
/// Delegates all RTSP connection management (including automatic
/// reconnection) to `video_input::RtspClient`.
class InputStage : public core::PipelineStage {
 public:
  InputStage() = default;
  ~InputStage() override;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  void OnPipelineStart() override;
  void OnPipelineStop() override;
  void Shutdown() override;
  std::string Name() const override { return "InputStage"; }

  /// Start the RTSP pull loop (runs in its own thread via RtspClient).
  bool StartPulling(int channel_id);

  /// Stop pulling.
  void StopPulling();

  /// Get stream info detected from the source (valid after StartPulling).
  video_input::RtspStreamInfo GetStreamInfo() const;

 private:
  /// Callback wired to RtspClient for incoming video packets.
  void OnPacket(int channel_id, const uint8_t* data, size_t size, int64_t pts,
                int64_t dts, bool is_keyframe, CodecType codec);

  video_input::RtspClientConfig rtsp_config_;
  std::unique_ptr<video_input::RtspClient> client_;
  int channel_id_ = -1;
};

}  // namespace loong::pipeline_stages

#endif  // LOONG_PIPELINE_STAGES_INPUT_STAGE_INPUT_STAGE_H_
