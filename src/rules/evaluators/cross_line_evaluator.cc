// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/cross_line_evaluator.h"

#include <algorithm>
#include <cmath>

#include "spdlog/spdlog.h"

namespace loong::rules {

bool CrossLineEvaluator::Configure(const AnalysisRule& rule) {
  rule_ = rule;

  IouTracker::Config tracker_cfg;
  tracker_cfg.iou_threshold = 0.25F;
  tracker_cfg.max_frames_missing = 10;
  tracker_cfg.max_trajectory_points = 30;
  tracker_ = IouTracker(tracker_cfg);

  last_side_.clear();

  spdlog::debug("CrossLineEvaluator: configured rule '{}' line ({:.2f},{:.2f})"
                "→({:.2f},{:.2f}) bidir={}",
                rule_.name,
                rule_.line.start.x, rule_.line.start.y,
                rule_.line.end.x, rule_.line.end.y,
                rule_.line.bidirectional);
  return true;
}

std::vector<RuleEvent> CrossLineEvaluator::Evaluate(
    const std::vector<Detection>& detections,
    const FrameContext& context) {
  std::vector<RuleEvent> events;

  auto filtered = FilterDetections(detections);
  if (filtered.empty()) {
    // Still update tracker with empty detections to age out tracks
    tracker_.Update(filtered);
    return events;
  }

  // Update tracker — this associates detections with existing tracks
  tracker_.Update(filtered);

  // Virtual line endpoints in normalized coordinates
  const auto& ls = rule_.line.start;
  const auto& le = rule_.line.end;

  for (const auto& track : tracker_.GetTracks()) {
    if (!track.active || track.trajectory.size() < 2) continue;

    // Get the center of the latest detection, normalized to 0~1
    auto center = IouTracker::NormalizePoint(
        track.trajectory.back(), context.frame_width, context.frame_height);

    // Compute which side of the line this point is on
    double cp = CrossProduct(ls, le, center);
    int current_side = (cp > 0.0) ? 1 : ((cp < 0.0) ? -1 : 0);

    if (current_side == 0) continue;  // Exactly on the line — ambiguous

    auto it = last_side_.find(track.track_id);
    if (it == last_side_.end()) {
      // First observation for this track — just record side
      last_side_[track.track_id] = current_side;
      continue;
    }

    int prev_side = it->second;
    if (prev_side != 0 && prev_side != current_side) {
      // Side changed → crossing detected!
      // Direction convention:
      //   A→B: cross product goes from positive to negative
      //   B→A: cross product goes from negative to positive
      std::string direction;
      if (prev_side > 0 && current_side < 0) {
        direction = "A_to_B";
      } else {
        direction = "B_to_A";
      }

      // If unidirectional, only report A→B
      if (!rule_.line.bidirectional && direction != "A_to_B") {
        it->second = current_side;
        continue;
      }

      RuleEvent ev;
      ev.rule_id = rule_.id;
      ev.rule_name = rule_.name;
      ev.rule_type = RuleType::kCrossLine;
      ev.channel_id = context.channel_id;
      ev.timestamp = context.timestamp_ms;
      ev.severity = RuleEventSeverity::kAlarm;
      ev.trigger_detection = track.last_detection;
      ev.direction = direction;

      events.push_back(std::move(ev));

      spdlog::debug("CrossLine: track {} crossed rule '{}' direction={}",
                    track.track_id, rule_.name, direction);
    }

    it->second = current_side;
  }

  // Clean up stale entries from last_side_ for inactive tracks
  for (auto it = last_side_.begin(); it != last_side_.end();) {
    if (tracker_.GetTrack(it->first) == nullptr) {
      it = last_side_.erase(it);
    } else {
      ++it;
    }
  }

  return events;
}

void CrossLineEvaluator::Reset() {
  tracker_.Reset();
  last_side_.clear();
}

double CrossLineEvaluator::CrossProduct(const Point2D& line_start,
                                        const Point2D& line_end,
                                        const Point2D& point) {
  // 2D cross product: (B-A) × (P-A)
  double dx_line = line_end.x - line_start.x;
  double dy_line = line_end.y - line_start.y;
  double dx_point = point.x - line_start.x;
  double dy_point = point.y - line_start.y;
  return dx_line * dy_point - dy_line * dx_point;
}

std::vector<Detection> CrossLineEvaluator::FilterDetections(
    const std::vector<Detection>& detections) const {
  std::vector<Detection> result;
  result.reserve(detections.size());

  for (const auto& det : detections) {
    if (det.confidence < rule_.min_confidence) continue;

    if (!rule_.target_classes.empty()) {
      auto it = std::find(rule_.target_classes.begin(),
                          rule_.target_classes.end(),
                          det.class_name);
      if (it == rule_.target_classes.end()) continue;
    }

    result.push_back(det);
  }

  return result;
}

}  // namespace loong::rules
