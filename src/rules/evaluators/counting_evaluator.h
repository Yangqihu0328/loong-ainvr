// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_COUNTING_EVALUATOR_H_
#define LOONG_RULES_EVALUATORS_COUNTING_EVALUATOR_H_

#include <unordered_map>
#include <vector>

#include "rules/evaluators/iou_tracker.h"
#include "rules/evaluators/rule_evaluator.h"

namespace loong::rules {

/// Counts objects crossing a virtual line in each direction.
///
/// Maintains cumulative A→B and B→A crossing counts. Each crossing
/// generates a RuleEvent with count_value set to the current total
/// (A→B minus B→A, i.e. net count of objects that have entered).
///
/// Algorithm is identical to CrossLineEvaluator (cross-product side
/// change detection) but generates kObjectCounting events with counters
/// instead of kCrossLine alarms.
class CountingEvaluator : public RuleEvaluator {
 public:
  CountingEvaluator() = default;

  bool Configure(const AnalysisRule& rule) override;
  std::vector<RuleEvent> Evaluate(
      const std::vector<Detection>& detections,
      const FrameContext& context) override;
  void Reset() override;
  RuleType GetType() const override { return RuleType::kObjectCounting; }

  int GetCountAtoB() const { return count_a_to_b_; }
  int GetCountBtoA() const { return count_b_to_a_; }
  int GetNetCount() const { return count_a_to_b_ - count_b_to_a_; }
  int GetTotalCrossings() const { return count_a_to_b_ + count_b_to_a_; }

 private:
  static double CrossProduct(const Point2D& line_start,
                             const Point2D& line_end,
                             const Point2D& point);

  std::vector<Detection> FilterDetections(
      const std::vector<Detection>& detections) const;

  AnalysisRule rule_;
  IouTracker tracker_;
  std::unordered_map<int, int> last_side_;

  int count_a_to_b_ = 0;
  int count_b_to_a_ = 0;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_COUNTING_EVALUATOR_H_
