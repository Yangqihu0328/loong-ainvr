// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/region_intrusion_evaluator.h"

#include <algorithm>
#include <cmath>

#include "spdlog/spdlog.h"

namespace loong::rules {

bool RegionIntrusionEvaluator::Configure(const AnalysisRule& rule) {
  rule_ = rule;

  if (rule_.region.vertices.size() < 3) {
    spdlog::warn("RegionIntrusionEvaluator: rule '{}' has < 3 vertices, "
                 "region detection disabled",
                 rule_.name);
    return false;
  }

  IouTracker::Config tracker_cfg;
  tracker_cfg.iou_threshold = 0.25F;
  tracker_cfg.max_frames_missing = 10;
  tracker_cfg.max_trajectory_points = 30;
  tracker_ = IouTracker(tracker_cfg);

  was_inside_.clear();

  spdlog::debug("RegionIntrusionEvaluator: configured rule '{}' with "
                "{} vertices on channel {}",
                rule_.name, rule_.region.vertices.size(), rule_.channel_id);
  return true;
}

std::vector<RuleEvent> RegionIntrusionEvaluator::Evaluate(
    const std::vector<Detection>& detections,
    const FrameContext& context) {
  std::vector<RuleEvent> events;

  if (rule_.region.vertices.size() < 3) return events;

  auto filtered = FilterDetections(detections);
  tracker_.Update(filtered);

  for (const auto& track : tracker_.GetTracks()) {
    if (!track.active || track.trajectory.empty()) continue;

    auto center = IouTracker::NormalizePoint(
        track.trajectory.back(), context.frame_width, context.frame_height);

    bool inside = PointInPolygon(center, rule_.region.vertices);

    auto it = was_inside_.find(track.track_id);
    if (it == was_inside_.end()) {
      was_inside_[track.track_id] = inside;
      if (inside) {
        // Object first observed already inside — generate event immediately
        RuleEvent ev;
        ev.rule_id = rule_.id;
        ev.rule_name = rule_.name;
        ev.rule_type = RuleType::kRegionIntrusion;
        ev.channel_id = context.channel_id;
        ev.timestamp = context.timestamp_ms;
        ev.severity = RuleEventSeverity::kAlarm;
        ev.trigger_detection = track.last_detection;
        ev.direction = "enter";
        events.push_back(std::move(ev));
      }
      continue;
    }

    bool was = it->second;
    if (!was && inside) {
      // Transition: outside → inside = intrusion event
      RuleEvent ev;
      ev.rule_id = rule_.id;
      ev.rule_name = rule_.name;
      ev.rule_type = RuleType::kRegionIntrusion;
      ev.channel_id = context.channel_id;
      ev.timestamp = context.timestamp_ms;
      ev.severity = RuleEventSeverity::kAlarm;
      ev.trigger_detection = track.last_detection;
      ev.direction = "enter";
      events.push_back(std::move(ev));

      spdlog::debug("RegionIntrusion: track {} entered region '{}' on ch={}",
                    track.track_id, rule_.name, context.channel_id);
    }

    it->second = inside;
  }

  // Clean up stale entries
  for (auto it = was_inside_.begin(); it != was_inside_.end();) {
    if (tracker_.GetTrack(it->first) == nullptr) {
      it = was_inside_.erase(it);
    } else {
      ++it;
    }
  }

  return events;
}

void RegionIntrusionEvaluator::Reset() {
  tracker_.Reset();
  was_inside_.clear();
}

bool RegionIntrusionEvaluator::PointInPolygon(
    const Point2D& point, const std::vector<Point2D>& polygon) {
  // Ray-casting algorithm: cast a horizontal ray from the point to the right.
  // Count how many polygon edges it crosses. Odd = inside, even = outside.
  if (polygon.size() < 3) return false;

  bool inside = false;
  size_t n = polygon.size();

  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    double yi = polygon[i].y;
    double yj = polygon[j].y;
    double xi = polygon[i].x;
    double xj = polygon[j].x;

    // Check if the ray crosses this edge
    if (((yi > point.y) != (yj > point.y)) &&
        (point.x < (xj - xi) * (point.y - yi) / (yj - yi) + xi)) {
      inside = !inside;
    }
  }

  return inside;
}

std::vector<Detection> RegionIntrusionEvaluator::FilterDetections(
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
