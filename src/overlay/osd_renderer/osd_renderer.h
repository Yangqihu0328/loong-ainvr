// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_OVERLAY_OSD_RENDERER_OSD_RENDERER_H_
#define LOONG_OVERLAY_OSD_RENDERER_OSD_RENDERER_H_

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/common/types.h"
#include "overlay/ft_text_renderer/ft_text_renderer.h"

namespace loong::overlay {

/// Standalone OSD (On-Screen Display) rendering engine.
///
/// Responsible for drawing all graphical overlays on BGR24 video frames
/// using OpenCV. Designed as a reusable library component, decoupled
/// from the pipeline framework.
///
/// Capabilities:
///   - Detection bounding boxes with anti-aliased lines
///   - Class-colored boxes from a 20-color palette
///   - Text labels with confidence percentages
///   - Timestamp OSD with configurable position and format
///   - Channel name/ID OSD
///   - Object trajectory trail lines
///   - Per-frame configurable via OverlayConfig
class OsdRenderer {
 public:
  OsdRenderer();
  ~OsdRenderer() = default;

  /// Apply the given overlay configuration. Can be called at any time
  /// to update rendering behaviour.
  void Configure(const OverlayConfig& config);

  /// Load a CJK-capable font for text rendering.
  /// If not loaded, falls back to OpenCV's built-in ASCII font.
  bool LoadFont(const std::string& font_path, int pixel_size = 20);

  /// Set channel identification info (displayed by channel name OSD).
  void SetChannelInfo(int channel_id, const std::string& channel_name);

  /// Render all enabled OSD elements onto a BGR24 frame.
  /// @param data    Pointer to BGR24 pixel data.
  /// @param width   Frame width in pixels.
  /// @param height  Frame height in pixels.
  /// @param stride  Row stride in bytes (>= width * 3).
  /// @param detections  AI detection results (may be empty).
  /// @param rule_overlay  Rule visualization data (may be empty).
  void Render(uint8_t* data, int width, int height, int stride,
              const std::vector<Detection>& detections,
              const RuleOverlayData& rule_overlay = {});

  /// Clear accumulated trajectory history.
  void ResetTrajectories();

  /// Get current configuration.
  const OverlayConfig& GetConfig() const { return config_; }

 private:
  struct Color {
    uint8_t b, g, r;
  };

  /// Draw detection bounding boxes, labels, and confidence scores.
  void DrawDetections(uint8_t* data, int width, int height, int stride,
                      const std::vector<Detection>& detections) const;

  /// Draw timestamp OSD on the frame.
  void DrawTimestamp(uint8_t* data, int width, int height, int stride) const;

  /// Draw channel name/ID OSD on the frame.
  void DrawChannelName(uint8_t* data, int width, int height, int stride);

  /// Draw object trajectory lines on the frame.
  void DrawTrajectories(uint8_t* data, int width, int height, int stride);

  /// Update trajectory tracking history with current detections.
  void UpdateTrajectoryHistory(const std::vector<Detection>& detections);

  /// Draw virtual lines (cross-line / counting rules) with optional counts.
  void DrawRuleLines(uint8_t* data, int width, int height, int stride,
                     const std::vector<RuleLineOverlay>& lines) const;

  /// Draw polygon regions (intrusion / loitering rules).
  void DrawRuleRegions(uint8_t* data, int width, int height, int stride,
                       const std::vector<RuleRegionOverlay>& regions) const;

  /// Draw highlights on loitering / alarming targets.
  void DrawRuleHighlights(uint8_t* data, int width, int height, int stride,
                          const std::vector<RuleTargetHighlight>& hl) const;

  /// Get a color for a given class ID (cycles through palette).
  static Color GetClassColor(int class_id);

  /// Draw text using FreeType if available, cv::putText otherwise.
  void DrawText(uint8_t* data, int width, int height, int stride,
                const std::string& text, int x, int y,
                double font_scale, uint8_t r, uint8_t g, uint8_t b) const;

  OverlayConfig config_;
  std::string channel_name_;
  int channel_id_ = -1;
  FtTextRenderer ft_renderer_;

  /// Trajectory tracking: maps class_id to deque of center points.
  struct TrajectoryPoint {
    int x, y;
  };
  std::unordered_map<int, std::deque<TrajectoryPoint>> trajectories_;

  /// 20-color palette for class visualization.
  static const std::array<Color, 20> kColorPalette;
};

}  // namespace loong::overlay

#endif  // LOONG_OVERLAY_OSD_RENDERER_OSD_RENDERER_H_
