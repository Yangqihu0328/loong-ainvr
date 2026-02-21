// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/iou_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace loong::rules {

IouTracker::IouTracker() = default;

IouTracker::IouTracker(const Config& config) : config_(config) {}

const std::vector<TrackedObject>& IouTracker::Update(
    const std::vector<Detection>& detections) {
  const auto num_tracks = tracks_.size();
  const auto num_dets = detections.size();

  // Mark which tracks and detections are matched
  std::vector<bool> track_matched(num_tracks, false);
  std::vector<bool> det_matched(num_dets, false);

  if (num_tracks > 0 && num_dets > 0) {
    // Build IOU matrix (track × detection)
    struct IouPair {
      size_t track_idx;
      size_t det_idx;
      float iou;
    };

    std::vector<IouPair> pairs;
    pairs.reserve(num_tracks * num_dets);

    for (size_t t = 0; t < num_tracks; ++t) {
      if (!tracks_[t].active) continue;
      for (size_t d = 0; d < num_dets; ++d) {
        float iou = ComputeIou(tracks_[t].last_detection, detections[d]);
        if (iou >= config_.iou_threshold) {
          pairs.push_back({t, d, iou});
        }
      }
    }

    // Greedy matching: sort by IOU descending, assign greedily
    std::sort(pairs.begin(), pairs.end(),
              [](const IouPair& a, const IouPair& b) { return a.iou > b.iou; });

    for (const auto& pair : pairs) {
      if (track_matched[pair.track_idx] || det_matched[pair.det_idx]) continue;

      track_matched[pair.track_idx] = true;
      det_matched[pair.det_idx] = true;

      auto& track = tracks_[pair.track_idx];
      track.last_detection = detections[pair.det_idx];
      track.frames_missing = 0;
      track.age++;

      auto center = DetectionCenter(detections[pair.det_idx]);
      track.trajectory.push_back(center);
      if (static_cast<int>(track.trajectory.size()) >
          config_.max_trajectory_points) {
        track.trajectory.erase(track.trajectory.begin());
      }
    }
  }

  // Unmatched tracks: increment missing counter
  for (size_t t = 0; t < num_tracks; ++t) {
    if (!tracks_[t].active) continue;
    if (!track_matched[t]) {
      tracks_[t].frames_missing++;
      if (tracks_[t].frames_missing > config_.max_frames_missing) {
        tracks_[t].active = false;
      }
    }
  }

  // Unmatched detections: create new tracks
  for (size_t d = 0; d < num_dets; ++d) {
    if (det_matched[d]) continue;

    TrackedObject new_track;
    new_track.track_id = next_track_id_++;
    new_track.last_detection = detections[d];
    new_track.age = 1;
    new_track.frames_missing = 0;
    new_track.active = true;
    new_track.trajectory.push_back(DetectionCenter(detections[d]));
    tracks_.push_back(std::move(new_track));
  }

  // Remove long-dead tracks to prevent unbounded growth
  tracks_.erase(std::remove_if(tracks_.begin(), tracks_.end(),
                               [this](const TrackedObject& t) {
                                 return !t.active &&
                                        t.frames_missing >
                                            config_.max_frames_missing * 2;
                               }),
                tracks_.end());

  return tracks_;
}

const std::vector<TrackedObject>& IouTracker::GetTracks() const {
  return tracks_;
}

const TrackedObject* IouTracker::GetTrack(int track_id) const {
  for (const auto& t : tracks_) {
    if (t.track_id == track_id) return &t;
  }
  return nullptr;
}

void IouTracker::Reset() {
  tracks_.clear();
  next_track_id_ = 1;
}

size_t IouTracker::ActiveCount() const {
  return static_cast<size_t>(
      std::count_if(tracks_.begin(), tracks_.end(),
                    [](const TrackedObject& t) { return t.active; }));
}

float IouTracker::ComputeIou(const Detection& a, const Detection& b) {
  float x1 = std::max(a.x1, b.x1);
  float y1 = std::max(a.y1, b.y1);
  float x2 = std::min(a.x2, b.x2);
  float y2 = std::min(a.y2, b.y2);

  float inter_w = std::max(0.0F, x2 - x1);
  float inter_h = std::max(0.0F, y2 - y1);
  float inter_area = inter_w * inter_h;

  float area_a = (a.x2 - a.x1) * (a.y2 - a.y1);
  float area_b = (b.x2 - b.x1) * (b.y2 - b.y1);
  float union_area = area_a + area_b - inter_area;

  if (union_area <= 0.0F) return 0.0F;
  return inter_area / union_area;
}

Point2D IouTracker::DetectionCenter(const Detection& det) {
  return {static_cast<double>((det.x1 + det.x2) * 0.5F),
          static_cast<double>((det.y1 + det.y2) * 0.5F)};
}

Point2D IouTracker::NormalizePoint(const Point2D& pt, int width, int height) {
  if (width <= 0 || height <= 0) return pt;
  return {pt.x / static_cast<double>(width),
          pt.y / static_cast<double>(height)};
}

}  // namespace loong::rules
