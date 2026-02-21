// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_RULE_EVALUATOR_H_
#define LOONG_RULES_EVALUATORS_RULE_EVALUATOR_H_

#include <memory>
#include <vector>

#include "core/common/types.h"
#include "rules/rule_types.h"

namespace loong::rules {

/// Frame context passed to evaluators alongside detection results.
struct FrameContext {
  int channel_id = -1;
  int64_t timestamp_ms = 0;
  int frame_width = 0;
  int frame_height = 0;
};

/// Abstract base class for all rule evaluators.
/// Each concrete evaluator implements the logic for one RuleType.
class RuleEvaluator {
 public:
  virtual ~RuleEvaluator() = default;

  /// Configure this evaluator for a specific rule definition.
  virtual bool Configure(const AnalysisRule& rule) = 0;

  /// Evaluate the current frame's detections against the rule.
  /// Returns zero or more RuleEvents if the rule is triggered.
  virtual std::vector<RuleEvent> Evaluate(
      const std::vector<Detection>& detections,
      const FrameContext& context) = 0;

  /// Reset internal state (e.g., tracking history, counters).
  virtual void Reset() = 0;

  /// Get the rule type this evaluator handles.
  virtual RuleType GetType() const = 0;
};

/// Factory function to create an evaluator by rule type.
std::unique_ptr<RuleEvaluator> CreateEvaluator(RuleType type);

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_RULE_EVALUATOR_H_
