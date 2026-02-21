// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_LOITERING_EVALUATOR_H_
#define LOONG_RULES_EVALUATORS_LOITERING_EVALUATOR_H_

#include "rules/evaluators/iou_tracker.h"
#include "rules/evaluators/region_intrusion_evaluator.h"
#include "rules/evaluators/rule_evaluator.h"

#include <unordered_map>
#include <vector>

namespace loong::rules {

/// Detects objects loitering (dwelling) inside a polygonal region beyond
/// a configurable time threshold.
///
/// Algorithm:
///  1. Track objects with IouTracker
///  2. For each active track, normalize center and test point-in-polygon
///  3. Accumulate per-track dwell time while inside the region
///  4. When dwell time >= loiter_time_sec, emit a kAlarm event
///  5. Re-alert after cooldown_sec if still loitering
///  6. Reset timer when object leaves the region
class LoiteringEvaluator : public RuleEvaluator {
 public:
  LoiteringEvaluator() = default;

  bool Configure(const AnalysisRule& rule) override;
  std::vector<RuleEvent> Evaluate(const std::vector<Detection>& detections,
                                  const FrameContext& context) override;
  void Reset() override;
  RuleType GetType() const override { return RuleType::kLoitering; }

 private:
  struct TrackState {
    int64_t enter_timestamp_ms = 0;  // When the track first entered region
    int64_t last_alert_ms = 0;       // Timestamp of last alert for cooldown
    bool inside = false;             // Currently inside region
    bool alerted = false;            // Has been alerted at least once
  };

  std::vector<Detection> FilterDetections(
      const std::vector<Detection>& detections) const;

  AnalysisRule rule_;
  IouTracker tracker_;
  std::unordered_map<int, TrackState> track_states_;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_LOITERING_EVALUATOR_H_
