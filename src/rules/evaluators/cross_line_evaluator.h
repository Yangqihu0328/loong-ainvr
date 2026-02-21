// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_CROSS_LINE_EVALUATOR_H_
#define LOONG_RULES_EVALUATORS_CROSS_LINE_EVALUATOR_H_

#include "rules/evaluators/iou_tracker.h"
#include "rules/evaluators/rule_evaluator.h"

#include <unordered_map>
#include <vector>

namespace loong::rules {

/// Detects objects crossing a virtual line.
///
/// Algorithm:
///  1. Track objects across frames using IouTracker
///  2. For each tracked object, compute which side of the virtual line
///     its center point is on (using the cross product sign)
///  3. When the side changes between consecutive frames → crossing detected
///  4. Determine direction: A→B (positive→negative cross product change)
///     or B→A (negative→positive)
///  5. If the rule is unidirectional, only report one direction
class CrossLineEvaluator : public RuleEvaluator {
 public:
  CrossLineEvaluator() = default;

  bool Configure(const AnalysisRule& rule) override;
  std::vector<RuleEvent> Evaluate(const std::vector<Detection>& detections,
                                  const FrameContext& context) override;
  void Reset() override;
  RuleType GetType() const override { return RuleType::kCrossLine; }

 private:
  /// Compute which side of the line a point is on.
  /// Returns >0 for one side, <0 for the other, 0 = on the line.
  /// The sign corresponds to the cross product of (line_end - line_start)
  /// and (point - line_start).
  static double CrossProduct(const Point2D& line_start, const Point2D& line_end,
                             const Point2D& point);

  /// Filter detections by target class and min confidence.
  std::vector<Detection> FilterDetections(
      const std::vector<Detection>& detections) const;

  AnalysisRule rule_;
  IouTracker tracker_;

  // track_id → last known side sign (+1 / -1 / 0)
  std::unordered_map<int, int> last_side_;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_CROSS_LINE_EVALUATOR_H_
