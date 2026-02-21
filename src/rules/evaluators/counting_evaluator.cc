// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/counting_evaluator.h"

#include "spdlog/spdlog.h"

#include <algorithm>

namespace loong::rules {

bool CountingEvaluator::Configure(const AnalysisRule& rule) {
  rule_ = rule;

  IouTracker::Config tracker_cfg;
  tracker_cfg.iou_threshold = 0.25F;
  tracker_cfg.max_frames_missing = 10;
  tracker_cfg.max_trajectory_points = 30;
  tracker_ = IouTracker(tracker_cfg);

  last_side_.clear();
  count_a_to_b_ = 0;
  count_b_to_a_ = 0;

  spdlog::debug(
      "CountingEvaluator: configured rule '{}' line ({:.2f},{:.2f})"
      "→({:.2f},{:.2f}) on ch={}",
      rule_.name, rule_.line.start.x, rule_.line.start.y, rule_.line.end.x,
      rule_.line.end.y, rule_.channel_id);
  return true;
}

std::vector<RuleEvent> CountingEvaluator::Evaluate(
    const std::vector<Detection>& detections, const FrameContext& context) {
  std::vector<RuleEvent> events;

  auto filtered = FilterDetections(detections);
  tracker_.Update(filtered);

  const auto& ls = rule_.line.start;
  const auto& le = rule_.line.end;

  for (const auto& track : tracker_.GetTracks()) {
    if (!track.active || track.trajectory.size() < 2) continue;

    auto center = IouTracker::NormalizePoint(
        track.trajectory.back(), context.frame_width, context.frame_height);

    double cp = CrossProduct(ls, le, center);
    int current_side = (cp > 0.0) ? 1 : ((cp < 0.0) ? -1 : 0);
    if (current_side == 0) continue;

    auto it = last_side_.find(track.track_id);
    if (it == last_side_.end()) {
      last_side_[track.track_id] = current_side;
      continue;
    }

    int prev_side = it->second;
    if (prev_side != 0 && prev_side != current_side) {
      std::string direction;
      if (prev_side > 0 && current_side < 0) {
        direction = "A_to_B";
        count_a_to_b_++;
      } else {
        direction = "B_to_A";
        count_b_to_a_++;
      }

      RuleEvent ev;
      ev.rule_id = rule_.id;
      ev.rule_name = rule_.name;
      ev.rule_type = RuleType::kObjectCounting;
      ev.channel_id = context.channel_id;
      ev.timestamp = context.timestamp_ms;
      ev.severity = RuleEventSeverity::kInfo;
      ev.trigger_detection = track.last_detection;
      ev.direction = direction;
      ev.count_value = count_a_to_b_ - count_b_to_a_;

      events.push_back(std::move(ev));

      spdlog::debug(
          "Counting: track {} crossed '{}' {} (A→B={}, B→A={}, "
          "net={})",
          track.track_id, rule_.name, direction, count_a_to_b_, count_b_to_a_,
          GetNetCount());
    }

    it->second = current_side;
  }

  for (auto it = last_side_.begin(); it != last_side_.end();) {
    if (tracker_.GetTrack(it->first) == nullptr) {
      it = last_side_.erase(it);
    } else {
      ++it;
    }
  }

  return events;
}

void CountingEvaluator::Reset() {
  tracker_.Reset();
  last_side_.clear();
  count_a_to_b_ = 0;
  count_b_to_a_ = 0;
}

double CountingEvaluator::CrossProduct(const Point2D& line_start,
                                       const Point2D& line_end,
                                       const Point2D& point) {
  double dx_line = line_end.x - line_start.x;
  double dy_line = line_end.y - line_start.y;
  double dx_point = point.x - line_start.x;
  double dy_point = point.y - line_start.y;
  return dx_line * dy_point - dy_line * dx_point;
}

std::vector<Detection> CountingEvaluator::FilterDetections(
    const std::vector<Detection>& detections) const {
  std::vector<Detection> result;
  result.reserve(detections.size());
  for (const auto& det : detections) {
    if (det.confidence < rule_.min_confidence) continue;
    if (!rule_.target_classes.empty()) {
      if (std::find(rule_.target_classes.begin(), rule_.target_classes.end(),
                    det.class_name) == rule_.target_classes.end()) {
        continue;
      }
    }
    result.push_back(det);
  }
  return result;
}

}  // namespace loong::rules
