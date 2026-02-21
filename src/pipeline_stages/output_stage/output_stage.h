// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_PIPELINE_STAGES_OUTPUT_STAGE_OUTPUT_STAGE_H_
#define LOONG_PIPELINE_STAGES_OUTPUT_STAGE_OUTPUT_STAGE_H_

#include <functional>
#include <memory>

#include "codec/encoder/encoder.h"
#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"

namespace loong::pipeline_stages {

/// Callback for encoded output packets (for streaming/recording).
using OutputPacketCallback =
    std::function<void(int channel_id, const uint8_t* data, size_t size,
                       int64_t pts, bool is_keyframe)>;

/// Output stage — re-encodes processed frames and delivers them
/// to recording and streaming subsystems.
class OutputStage : public core::PipelineStage {
 public:
  OutputStage() = default;
  ~OutputStage() override = default;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  void Shutdown() override;
  std::string Name() const override { return "OutputStage"; }

  /// Set callback for encoded output.
  void SetOutputCallback(OutputPacketCallback callback) {
    output_callback_ = std::move(callback);
  }

 private:
  bool InitEncoder(int width, int height);

  std::unique_ptr<codec::Encoder> encoder_;
  OutputPacketCallback output_callback_;
  codec::EncoderConfig enc_config_;
  int channel_id_ = -1;
  bool initialized_ = false;
  bool encoder_ready_ = false;
};

}  // namespace loong::pipeline_stages

#endif  // LOONG_PIPELINE_STAGES_OUTPUT_STAGE_OUTPUT_STAGE_H_
