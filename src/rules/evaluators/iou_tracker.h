// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_EVALUATORS_IOU_TRACKER_H_
#define LOONG_RULES_EVALUATORS_IOU_TRACKER_H_

#include "core/common/types.h"
#include "rules/rule_types.h"

#include <vector>

namespace loong::rules {

/// Lightweight IOU-based multi-object tracker.
///
/// Algorithm:
///  1. Compute IOU between every existing track and new detection
///  2. Greedy match (highest IOU first, above threshold)
///  3. Matched tracks: update position and append to trajectory
///  4. Unmatched detections: create new tracks
///  5. Unmatched tracks: increment frames_missing; deactivate if too old
///
/// This tracker is stateful and per-evaluator (one instance per rule).
/// It does not handle re-identification across camera views.
class IouTracker {
 public:
  struct Config {
    float iou_threshold = 0.25F;
    int max_frames_missing = 10;
    int max_trajectory_points = 60;
  };

  IouTracker();
  explicit IouTracker(const Config& config);

  /// Update tracks with a new set of detections for this frame.
  /// Returns references to all currently active tracks.
  const std::vector<TrackedObject>& Update(
      const std::vector<Detection>& detections);

  /// Get all active tracks.
  const std::vector<TrackedObject>& GetTracks() const;

  /// Get a specific track by ID. Returns nullptr if not found.
  const TrackedObject* GetTrack(int track_id) const;

  /// Reset all tracks.
  void Reset();

  /// Number of active tracks.
  size_t ActiveCount() const;

  /// Compute IOU between two bounding boxes.
  static float ComputeIou(const Detection& a, const Detection& b);

  /// Get the center point of a detection (in absolute pixel coordinates).
  static Point2D DetectionCenter(const Detection& det);

  /// Normalize a pixel-coordinate point to 0.0~1.0 range.
  static Point2D NormalizePoint(const Point2D& pt, int width, int height);

 private:
  Config config_;
  std::vector<TrackedObject> tracks_;
  int next_track_id_ = 1;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_EVALUATORS_IOU_TRACKER_H_
