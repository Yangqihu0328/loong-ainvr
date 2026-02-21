// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_PIPELINE_STAGES_OVERLAY_STAGE_OVERLAY_STAGE_H_
#define LOONG_PIPELINE_STAGES_OVERLAY_STAGE_OVERLAY_STAGE_H_

#include "core/common/types.h"
#include "core/pipeline/pipeline_stage.h"
#include "overlay/osd_renderer/osd_renderer.h"

#include <memory>
#include <string>

namespace loong::pipeline_stages {

/// Pipeline stage that applies OSD overlays on decoded video frames.
///
/// Delegates all rendering to the overlay::OsdRenderer library component.
/// Configuration is passed from ChannelOrchestrator via JSON params.
class OverlayStage : public core::PipelineStage {
 public:
  OverlayStage() = default;
  ~OverlayStage() override = default;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  void Shutdown() override;
  std::string Name() const override { return "OverlayStage"; }

 private:
  overlay::OsdRenderer renderer_;
};

}  // namespace loong::pipeline_stages

#endif  // LOONG_PIPELINE_STAGES_OVERLAY_STAGE_OVERLAY_STAGE_H_
