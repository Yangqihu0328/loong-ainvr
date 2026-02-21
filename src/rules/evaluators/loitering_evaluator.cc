// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/loitering_evaluator.h"

#include <algorithm>

#include "spdlog/spdlog.h"

namespace loong::rules {

bool LoiteringEvaluator::Configure(const AnalysisRule& rule) {
  rule_ = rule;

  if (rule_.region.vertices.size() < 3) {
    spdlog::warn("LoiteringEvaluator: rule '{}' has < 3 vertices, disabled",
                 rule_.name);
    return false;
  }

  IouTracker::Config tracker_cfg;
  tracker_cfg.iou_threshold = 0.25F;
  tracker_cfg.max_frames_missing = 10;
  tracker_cfg.max_trajectory_points = 30;
  tracker_ = IouTracker(tracker_cfg);

  track_states_.clear();

  spdlog::debug("LoiteringEvaluator: configured rule '{}' threshold={}s "
                "cooldown={}s vertices={} ch={}",
                rule_.name, rule_.loiter_time_sec, rule_.cooldown_sec,
                rule_.region.vertices.size(), rule_.channel_id);
  return true;
}

std::vector<RuleEvent> LoiteringEvaluator::Evaluate(
    const std::vector<Detection>& detections,
    const FrameContext& context) {
  std::vector<RuleEvent> events;

  if (rule_.region.vertices.size() < 3) return events;

  auto filtered = FilterDetections(detections);
  tracker_.Update(filtered);

  const int64_t threshold_ms =
      static_cast<int64_t>(rule_.loiter_time_sec) * 1000;
  const int64_t cooldown_ms =
      static_cast<int64_t>(rule_.cooldown_sec) * 1000;

  for (const auto& track : tracker_.GetTracks()) {
    if (!track.active || track.trajectory.empty()) continue;

    auto center = IouTracker::NormalizePoint(
        track.trajectory.back(), context.frame_width, context.frame_height);

    bool inside = RegionIntrusionEvaluator::PointInPolygon(
        center, rule_.region.vertices);

    auto& state = track_states_[track.track_id];

    if (inside) {
      if (!state.inside) {
        // Just entered the region
        state.inside = true;
        state.enter_timestamp_ms = context.timestamp_ms;
        state.alerted = false;
      }

      int64_t dwell_ms = context.timestamp_ms - state.enter_timestamp_ms;
      int dwell_sec = static_cast<int>(dwell_ms / 1000);

      if (dwell_ms >= threshold_ms) {
        bool should_alert = false;

        if (!state.alerted) {
          should_alert = true;
        } else if (cooldown_ms > 0 &&
                   (context.timestamp_ms - state.last_alert_ms) >= cooldown_ms) {
          should_alert = true;
        }

        if (should_alert) {
          RuleEvent ev;
          ev.rule_id = rule_.id;
          ev.rule_name = rule_.name;
          ev.rule_type = RuleType::kLoitering;
          ev.channel_id = context.channel_id;
          ev.timestamp = context.timestamp_ms;
          ev.severity = RuleEventSeverity::kAlarm;
          ev.trigger_detection = track.last_detection;
          ev.dwell_time_sec = dwell_sec;

          events.push_back(std::move(ev));

          state.alerted = true;
          state.last_alert_ms = context.timestamp_ms;

          spdlog::debug("Loitering: track {} in '{}' for {}s (ch={})",
                        track.track_id, rule_.name, dwell_sec,
                        context.channel_id);
        }
      }
    } else {
      // Object left the region — reset dwell timer
      if (state.inside) {
        state.inside = false;
        state.alerted = false;
        state.enter_timestamp_ms = 0;
      }
    }
  }

  // Clean up stale track states
  for (auto it = track_states_.begin(); it != track_states_.end();) {
    if (tracker_.GetTrack(it->first) == nullptr) {
      it = track_states_.erase(it);
    } else {
      ++it;
    }
  }

  return events;
}

void LoiteringEvaluator::Reset() {
  tracker_.Reset();
  track_states_.clear();
}

std::vector<Detection> LoiteringEvaluator::FilterDetections(
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
