// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_PIPELINE_STAGES_DECODE_STAGE_DECODE_STAGE_H_
#define LOONG_PIPELINE_STAGES_DECODE_STAGE_DECODE_STAGE_H_

#include "codec/decoder/decoder.h"
#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"

#include <memory>

namespace loong::pipeline_stages {

/// Decode stage — takes encoded frames and produces raw BGR frames.
/// When codec/resolution are unknown at init time (auto-detect mode),
/// the actual decoder creation is deferred until the first encoded frame
/// arrives, whose metadata carries the real codec type.
class DecodeStage : public core::PipelineStage {
 public:
  DecodeStage() = default;
  ~DecodeStage() override = default;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  void Shutdown() override;
  std::string Name() const override { return "DecodeStage"; }

 private:
  bool InitDecoder(CodecType codec, int width, int height);

  std::unique_ptr<codec::Decoder> decoder_;
  bool initialized_ = false;
  bool decoder_ready_ = false;
  CodecType hint_codec_ = CodecType::kUnknown;
  int hint_width_ = 0;
  int hint_height_ = 0;
};

}  // namespace loong::pipeline_stages

#endif  // LOONG_PIPELINE_STAGES_DECODE_STAGE_DECODE_STAGE_H_
