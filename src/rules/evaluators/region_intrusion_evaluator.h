// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_REGION_INTRUSION_EVALUATOR_H_
#define LOONG_RULES_EVALUATORS_REGION_INTRUSION_EVALUATOR_H_

#include "rules/evaluators/iou_tracker.h"
#include "rules/evaluators/rule_evaluator.h"

#include <unordered_map>
#include <vector>

namespace loong::rules {

/// Detects objects entering a user-defined polygonal region.
///
/// Algorithm:
///  1. Track objects across frames using IouTracker
///  2. For each tracked object, normalize its center point and test
///     whether it falls inside the configured polygon (ray-casting)
///  3. When an object transitions from outside → inside, generate an
///     intrusion event
///  4. Optionally filter by target class and min confidence
class RegionIntrusionEvaluator : public RuleEvaluator {
 public:
  RegionIntrusionEvaluator() = default;

  bool Configure(const AnalysisRule& rule) override;
  std::vector<RuleEvent> Evaluate(const std::vector<Detection>& detections,
                                  const FrameContext& context) override;
  void Reset() override;
  RuleType GetType() const override { return RuleType::kRegionIntrusion; }

  /// Ray-casting point-in-polygon test.
  /// Works for any simple (non-self-intersecting) polygon.
  static bool PointInPolygon(const Point2D& point,
                             const std::vector<Point2D>& polygon);

 private:
  std::vector<Detection> FilterDetections(
      const std::vector<Detection>& detections) const;

  AnalysisRule rule_;
  IouTracker tracker_;

  // track_id → was inside the region on the last frame
  std::unordered_map<int, bool> was_inside_;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_REGION_INTRUSION_EVALUATOR_H_
