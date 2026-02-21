// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_RULE_STAGE_RULE_STAGE_H_
#define LOONG_RULES_RULE_STAGE_RULE_STAGE_H_

#include "core/pipeline/pipeline_stage.h"
#include "rules/rule_engine/rule_engine.h"

#include <memory>

namespace loong::rules {

/// Pipeline stage that evaluates analysis rules against AI detections.
///
/// Positioned after AiStage in the pipeline:
///   Input → Decode → AI → **Rule** → Overlay → Output
///
/// Reads the AnalysisResult already attached to each Frame by AiStage,
/// then delegates evaluation to the shared RuleEngine instance.
/// The frame is always forwarded to the next stage regardless of
/// whether any rules were triggered.
class RuleStage : public core::PipelineStage {
 public:
  RuleStage() = default;
  ~RuleStage() override = default;

  bool Initialize(const StageConfig& config) override;
  bool ProcessFrame(std::shared_ptr<Frame> frame) override;
  void Shutdown() override;
  std::string Name() const override { return "RuleStage"; }

  /// Set the shared rule engine (dependency injection from main.cc).
  void SetRuleEngine(std::shared_ptr<RuleEngine> engine);

 private:
  std::shared_ptr<RuleEngine> engine_;
  int channel_id_ = -1;
  bool initialized_ = false;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_RULE_STAGE_RULE_STAGE_H_
