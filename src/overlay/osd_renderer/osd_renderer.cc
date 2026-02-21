// Copyright 2026 Loong AI NVR Project

#include "overlay/osd_renderer/osd_renderer.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include "spdlog/spdlog.h"

namespace loong::overlay {

// 20-color palette: distinct, high-contrast colors for class visualization.
// BGR format matching OpenCV convention.
const std::array<OsdRenderer::Color, 20> OsdRenderer::kColorPalette = {{
    {0, 255, 0},       // Green
    {255, 0, 0},       // Blue
    {0, 0, 255},       // Red
    {0, 255, 255},     // Yellow
    {255, 255, 0},     // Cyan
    {255, 0, 255},     // Magenta
    {0, 165, 255},     // Orange
    {203, 192, 255},   // Pink
    {0, 128, 0},       // Dark Green
    {128, 0, 0},       // Navy
    {0, 0, 128},       // Maroon
    {128, 128, 0},     // Teal
    {0, 128, 128},     // Olive
    {128, 0, 128},     // Purple
    {180, 105, 255},   // Hot Pink
    {50, 205, 50},     // Lime Green
    {255, 191, 0},     // Deep Sky Blue
    {0, 215, 255},     // Gold
    {147, 20, 255},    // Deep Pink
    {255, 144, 30},    // Dodger Blue
}};

OsdRenderer::OsdRenderer() = default;

void OsdRenderer::Configure(const OverlayConfig& config) {
  config_ = config;
}

bool OsdRenderer::LoadFont(const std::string& font_path, int pixel_size) {
  return ft_renderer_.LoadFont(font_path, pixel_size);
}

void OsdRenderer::DrawText(uint8_t* data, int width, int height, int stride,
                            const std::string& text, int x, int y,
                            double font_scale,
                            uint8_t r, uint8_t g, uint8_t b) const {
  if (ft_renderer_.IsReady()) {
    int px_size = std::max(12, static_cast<int>(font_scale * 28.0));
    const_cast<FtTextRenderer&>(ft_renderer_).SetPixelSize(px_size);
    ft_renderer_.RenderText(data, width, height, stride, text, x, y, r, g, b);
  } else {
    cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));
    cv::putText(frame, text, cv::Point(x, y + static_cast<int>(font_scale * 28)),
                cv::FONT_HERSHEY_SIMPLEX, font_scale,
                cv::Scalar(b, g, r), 1, cv::LINE_AA);
  }
}

void OsdRenderer::SetChannelInfo(int channel_id,
                                  const std::string& channel_name) {
  channel_id_ = channel_id;
  channel_name_ = channel_name;
}

OsdRenderer::Color OsdRenderer::GetClassColor(int class_id) {
  auto index = static_cast<size_t>(class_id % 20);
  if (class_id < 0) index = 0;
  return kColorPalette[index];
}

void OsdRenderer::ResetTrajectories() {
  trajectories_.clear();
}

// ============================================================
// Main Render Entry Point
// ============================================================

void OsdRenderer::Render(uint8_t* data, int width, int height, int stride,
                          const std::vector<Detection>& detections,
                          const RuleOverlayData& rule_overlay) {
  if (!data || width <= 0 || height <= 0 || !config_.enabled) return;

  // Draw rule geometry first (lowest layer)
  if (!rule_overlay.regions.empty()) {
    DrawRuleRegions(data, width, height, stride, rule_overlay.regions);
  }
  if (!rule_overlay.lines.empty()) {
    DrawRuleLines(data, width, height, stride, rule_overlay.lines);
  }

  // Draw OSD elements
  if (config_.show_timestamp) {
    DrawTimestamp(data, width, height, stride);
  }

  if (config_.show_channel_name) {
    DrawChannelName(data, width, height, stride);
  }

  // Draw trajectories and detections
  if (!detections.empty()) {
    if (config_.show_trajectory) {
      UpdateTrajectoryHistory(detections);
      DrawTrajectories(data, width, height, stride);
    }
    DrawDetections(data, width, height, stride, detections);
  }

  // Draw rule target highlights (top layer)
  if (!rule_overlay.highlights.empty()) {
    DrawRuleHighlights(data, width, height, stride, rule_overlay.highlights);
  }
}

// ============================================================
// Timestamp OSD
// ============================================================

void OsdRenderer::DrawTimestamp(uint8_t* data, int width, int height,
                                 int stride) const {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm tm_buf{};
  localtime_r(&time_t_now, &tm_buf);

  std::ostringstream oss;
  if (config_.timestamp_format.empty()) {
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
  } else {
    oss << std::put_time(&tm_buf, config_.timestamp_format.c_str());
  }
  std::string timestamp = oss.str();

  double ts_font_scale = config_.font_scale * 1.2;
  int text_w = 0, text_h = 0;
  if (ft_renderer_.IsReady()) {
    int px = std::max(12, static_cast<int>(ts_font_scale * 28.0));
    const_cast<FtTextRenderer&>(ft_renderer_).SetPixelSize(px);
    ft_renderer_.MeasureText(timestamp, text_w, text_h);
  } else {
    int baseline = 0;
    cv::Size sz = cv::getTextSize(timestamp, cv::FONT_HERSHEY_SIMPLEX,
                                  ts_font_scale, 1, &baseline);
    text_w = sz.width;
    text_h = sz.height + baseline;
  }

  int pad = 6;
  int box_h = text_h + pad * 2;
  int box_w = text_w + pad * 2;

  int ox = 0;
  int oy = 0;
  switch (config_.timestamp_position) {
    case 0: ox = pad; oy = pad; break;
    case 1: ox = width - box_w - pad; oy = pad; break;
    case 2: ox = pad; oy = height - box_h - pad; break;
    case 3: ox = width - box_w - pad; oy = height - box_h - pad; break;
    default: ox = pad; oy = pad; break;
  }

  ox = std::max(0, std::min(ox, width - box_w));
  oy = std::max(0, std::min(oy, height - box_h));

  cv::Rect bg_rect(ox, oy, box_w, box_h);
  bg_rect &= cv::Rect(0, 0, width, height);
  if (bg_rect.width > 0 && bg_rect.height > 0) {
    cv::Mat roi = frame(bg_rect);
    cv::Mat overlay_mat;
    roi.copyTo(overlay_mat);
    overlay_mat.setTo(cv::Scalar(0, 0, 0));
    cv::addWeighted(overlay_mat, 0.5, roi, 0.5, 0, roi);
  }

  DrawText(data, width, height, stride, timestamp,
           ox + pad, oy + pad, ts_font_scale, 255, 255, 255);
}

// ============================================================
// Channel Name OSD
// ============================================================

void OsdRenderer::DrawChannelName(uint8_t* data, int width, int height,
                                   int stride) {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  std::string label;
  if (!channel_name_.empty()) {
    label = channel_name_;
    if (channel_id_ >= 0) {
      label += " (CH" + std::to_string(channel_id_) + ")";
    }
  } else if (channel_id_ >= 0) {
    label = "CH" + std::to_string(channel_id_);
  } else {
    return;
  }

  double ch_font_scale = config_.font_scale * 1.1;
  int text_w = 0, text_h = 0;
  if (ft_renderer_.IsReady()) {
    int px = std::max(12, static_cast<int>(ch_font_scale * 28.0));
    const_cast<FtTextRenderer&>(ft_renderer_).SetPixelSize(px);
    ft_renderer_.MeasureText(label, text_w, text_h);
  } else {
    int baseline = 0;
    cv::Size sz = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                  ch_font_scale, 1, &baseline);
    text_w = sz.width;
    text_h = sz.height + baseline;
  }

  int pad = 6;
  int box_h = text_h + pad * 2;
  int box_w = text_w + pad * 2;

  int ox = width - box_w - pad;
  int oy = pad;
  if (config_.show_timestamp && config_.timestamp_position == 1) {
    ox = pad;
  }

  ox = std::max(0, std::min(ox, width - box_w));
  oy = std::max(0, std::min(oy, height - box_h));

  cv::Rect bg_rect(ox, oy, box_w, box_h);
  bg_rect &= cv::Rect(0, 0, width, height);
  if (bg_rect.width > 0 && bg_rect.height > 0) {
    cv::Mat roi = frame(bg_rect);
    cv::Mat overlay_mat;
    roi.copyTo(overlay_mat);
    overlay_mat.setTo(cv::Scalar(0, 0, 0));
    cv::addWeighted(overlay_mat, 0.45, roi, 0.55, 0, roi);
  }

  DrawText(data, width, height, stride, label,
           ox + pad, oy + pad, ch_font_scale, 255, 255, 0);
}

// ============================================================
// Trajectory Drawing
// ============================================================

void OsdRenderer::UpdateTrajectoryHistory(
    const std::vector<Detection>& detections) {
  for (const auto& det : detections) {
    int cx = static_cast<int>((det.x1 + det.x2) / 2.0F);
    int cy = static_cast<int>((det.y1 + det.y2) / 2.0F);

    auto& trail = trajectories_[det.class_id];
    trail.push_back({cx, cy});

    while (static_cast<int>(trail.size()) > config_.trajectory_max_points) {
      trail.pop_front();
    }
  }
}

void OsdRenderer::DrawTrajectories(uint8_t* data, int width, int height,
                                    int stride) {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  for (const auto& [class_id, trail] : trajectories_) {
    if (trail.size() < 2) continue;

    auto color = GetClassColor(class_id);
    cv::Scalar cv_color(color.b, color.g, color.r);

    for (size_t i = 1; i < trail.size(); ++i) {
      double alpha = static_cast<double>(i) /
                     static_cast<double>(trail.size());
      int thickness = std::max(1, static_cast<int>(alpha *
                                                   config_.line_thickness));

      cv::Point pt1(std::clamp(trail[i - 1].x, 0, width - 1),
                    std::clamp(trail[i - 1].y, 0, height - 1));
      cv::Point pt2(std::clamp(trail[i].x, 0, width - 1),
                    std::clamp(trail[i].y, 0, height - 1));

      cv::line(frame, pt1, pt2, cv_color, thickness, cv::LINE_AA);
    }
  }
}

// ============================================================
// Detection Bounding Boxes
// ============================================================

void OsdRenderer::DrawDetections(uint8_t* data, int width, int height,
                                  int stride,
                                  const std::vector<Detection>& detections) const {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  for (const auto& det : detections) {
    int x1 = std::max(0, std::min(static_cast<int>(det.x1), width - 1));
    int y1 = std::max(0, std::min(static_cast<int>(det.y1), height - 1));
    int x2 = std::max(0, std::min(static_cast<int>(det.x2), width - 1));
    int y2 = std::max(0, std::min(static_cast<int>(det.y2), height - 1));

    if (x2 <= x1 || y2 <= y1) continue;

    auto color = GetClassColor(det.class_id);
    cv::Scalar cv_color(color.b, color.g, color.r);

    // Bounding box
    cv::rectangle(frame, cv::Point(x1, y1), cv::Point(x2, y2), cv_color,
                  config_.line_thickness, cv::LINE_AA);

    // Semi-transparent fill
    if (config_.fill_opacity > 0.0) {
      cv::Mat roi = frame(cv::Rect(x1, y1, x2 - x1, y2 - y1));
      cv::Mat overlay_mat;
      roi.copyTo(overlay_mat);
      cv::rectangle(overlay_mat, cv::Point(0, 0),
                    cv::Point(x2 - x1, y2 - y1), cv_color, cv::FILLED);
      cv::addWeighted(overlay_mat, config_.fill_opacity, roi,
                      1.0 - config_.fill_opacity, 0, roi);
    }

    if (!config_.show_labels) continue;

    // Label text
    std::string label = det.class_name;
    if (label.empty()) {
      label = "class_" + std::to_string(det.class_id);
    }
    if (config_.show_confidence) {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision(0) << (det.confidence * 100.0F)
          << "%";
      label += " " + oss.str();
    }

    int baseline = 0;
    cv::Size text_size = cv::getTextSize(
        label, cv::FONT_HERSHEY_SIMPLEX, config_.font_scale, 1, &baseline);

    int label_h = text_size.height + baseline + 8;
    int label_w = text_size.width + 8;

    int label_y = y1 - label_h;
    if (label_y < 0) label_y = y1;

    cv::rectangle(frame, cv::Point(x1, label_y),
                  cv::Point(x1 + label_w, label_y + label_h), cv_color,
                  cv::FILLED);

    double brightness =
        0.299 * color.r + 0.587 * color.g + 0.114 * color.b;
    cv::Scalar text_color = brightness > 128 ? cv::Scalar(0, 0, 0)
                                              : cv::Scalar(255, 255, 255);

    DrawText(data, width, height, stride, label,
             x1 + 4, label_y + 4, config_.font_scale,
             static_cast<uint8_t>(text_color[2]),
             static_cast<uint8_t>(text_color[1]),
             static_cast<uint8_t>(text_color[0]));
  }
}

// ============================================================
// Rule Overlay: Virtual Lines
// ============================================================

void OsdRenderer::DrawRuleLines(
    uint8_t* data, int width, int height, int stride,
    const std::vector<RuleLineOverlay>& lines) const {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  for (const auto& ln : lines) {
    int px1 = std::clamp(static_cast<int>(ln.x1 * width), 0, width - 1);
    int py1 = std::clamp(static_cast<int>(ln.y1 * height), 0, height - 1);
    int px2 = std::clamp(static_cast<int>(ln.x2 * width), 0, width - 1);
    int py2 = std::clamp(static_cast<int>(ln.y2 * height), 0, height - 1);

    cv::Scalar line_color(0, 255, 255);  // Yellow (BGR)
    cv::line(frame, cv::Point(px1, py1), cv::Point(px2, py2),
             line_color, 2, cv::LINE_AA);

    // Direction arrow at midpoint
    int mx = (px1 + px2) / 2;
    int my = (py1 + py2) / 2;
    double dx = static_cast<double>(px2 - px1);
    double dy = static_cast<double>(py2 - py1);
    double len = std::sqrt(dx * dx + dy * dy);
    if (len > 0) {
      double nx = -dy / len * 12.0;
      double ny = dx / len * 12.0;
      cv::arrowedLine(frame,
                      cv::Point(mx - static_cast<int>(nx),
                                my - static_cast<int>(ny)),
                      cv::Point(mx + static_cast<int>(nx),
                                my + static_cast<int>(ny)),
                      line_color, 2, cv::LINE_AA, 0, 0.3);
    }

    if (!ln.label.empty()) {
      int tw = 0, th = 0;
      if (ft_renderer_.IsReady()) {
        int px = std::max(12, static_cast<int>(config_.font_scale * 28.0));
        const_cast<FtTextRenderer&>(ft_renderer_).SetPixelSize(px);
        ft_renderer_.MeasureText(ln.label, tw, th);
      } else {
        int baseline = 0;
        cv::Size ts = cv::getTextSize(ln.label, cv::FONT_HERSHEY_SIMPLEX,
                                      config_.font_scale, 1, &baseline);
        tw = ts.width;
      }
      DrawText(data, width, height, stride, ln.label,
               mx - tw / 2, my - 10 - th, config_.font_scale, 0, 255, 255);
    }

    if (ln.show_counts) {
      std::string count_text = "A>" + std::to_string(ln.count_a_to_b) +
                               " B>" + std::to_string(ln.count_b_to_a);
      DrawText(data, width, height, stride, count_text,
               mx - 30, my + 20, config_.font_scale * 0.9, 255, 255, 255);
    }
  }
}

// ============================================================
// Rule Overlay: Polygon Regions
// ============================================================

void OsdRenderer::DrawRuleRegions(
    uint8_t* data, int width, int height, int stride,
    const std::vector<RuleRegionOverlay>& regions) const {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  for (const auto& reg : regions) {
    if (reg.vertices.size() < 3) continue;

    std::vector<cv::Point> pts;
    pts.reserve(reg.vertices.size());
    for (const auto& [vx, vy] : reg.vertices) {
      pts.emplace_back(
          std::clamp(static_cast<int>(vx * width), 0, width - 1),
          std::clamp(static_cast<int>(vy * height), 0, height - 1));
    }

    // Semi-transparent fill
    cv::Scalar fill_color = reg.alarm_active
                                ? cv::Scalar(0, 0, 200)    // Red tint
                                : cv::Scalar(200, 200, 0);  // Cyan tint
    double alpha = reg.alarm_active ? 0.25 : 0.12;

    cv::Rect bounding = cv::boundingRect(pts);
    bounding &= cv::Rect(0, 0, width, height);
    if (bounding.width > 0 && bounding.height > 0) {
      cv::Mat roi = frame(bounding);
      cv::Mat overlay_mat;
      roi.copyTo(overlay_mat);

      std::vector<cv::Point> shifted;
      shifted.reserve(pts.size());
      for (const auto& p : pts) {
        shifted.emplace_back(p.x - bounding.x, p.y - bounding.y);
      }
      const cv::Point* ppt[] = {shifted.data()};
      int npt[] = {static_cast<int>(shifted.size())};
      cv::fillPoly(overlay_mat, ppt, npt, 1, fill_color);
      cv::addWeighted(overlay_mat, alpha, roi, 1.0 - alpha, 0, roi);
    }

    // Polygon border
    cv::Scalar border_color = reg.alarm_active
                                  ? cv::Scalar(0, 0, 255)
                                  : cv::Scalar(255, 255, 0);
    const cv::Point* ppt[] = {pts.data()};
    int npt[] = {static_cast<int>(pts.size())};
    cv::polylines(frame, ppt, npt, 1, true, border_color, 2, cv::LINE_AA);

    if (!reg.label.empty() && !pts.empty()) {
      int th = 0, tw = 0;
      if (ft_renderer_.IsReady()) {
        ft_renderer_.MeasureText(reg.label, tw, th);
      }
      DrawText(data, width, height, stride, reg.label,
               pts[0].x + 4, pts[0].y - 8 - th, config_.font_scale,
               static_cast<uint8_t>(border_color[2]),
               static_cast<uint8_t>(border_color[1]),
               static_cast<uint8_t>(border_color[0]));
    }
  }
}

// ============================================================
// Rule Overlay: Target Highlights
// ============================================================

void OsdRenderer::DrawRuleHighlights(
    uint8_t* data, int width, int height, int stride,
    const std::vector<RuleTargetHighlight>& hl) const {
  cv::Mat frame(height, width, CV_8UC3, data, static_cast<size_t>(stride));

  for (const auto& h : hl) {
    int hx1 = std::clamp(static_cast<int>(h.x1), 0, width - 1);
    int hy1 = std::clamp(static_cast<int>(h.y1), 0, height - 1);
    int hx2 = std::clamp(static_cast<int>(h.x2), 0, width - 1);
    int hy2 = std::clamp(static_cast<int>(h.y2), 0, height - 1);
    if (hx2 <= hx1 || hy2 <= hy1) continue;

    cv::Scalar color = h.alarm ? cv::Scalar(0, 0, 255)    // Red
                               : cv::Scalar(0, 200, 255);  // Orange

    // Thicker dashed-style border (draw with thicker line)
    cv::rectangle(frame, cv::Point(hx1, hy1), cv::Point(hx2, hy2),
                  color, 3, cv::LINE_AA);

    // Pulsing corners (corner brackets)
    int corner_len = std::min(20, std::min(hx2 - hx1, hy2 - hy1) / 3);
    cv::line(frame, cv::Point(hx1, hy1),
             cv::Point(hx1 + corner_len, hy1), color, 3);
    cv::line(frame, cv::Point(hx1, hy1),
             cv::Point(hx1, hy1 + corner_len), color, 3);
    cv::line(frame, cv::Point(hx2, hy1),
             cv::Point(hx2 - corner_len, hy1), color, 3);
    cv::line(frame, cv::Point(hx2, hy1),
             cv::Point(hx2, hy1 + corner_len), color, 3);
    cv::line(frame, cv::Point(hx1, hy2),
             cv::Point(hx1 + corner_len, hy2), color, 3);
    cv::line(frame, cv::Point(hx1, hy2),
             cv::Point(hx1, hy2 - corner_len), color, 3);
    cv::line(frame, cv::Point(hx2, hy2),
             cv::Point(hx2 - corner_len, hy2), color, 3);
    cv::line(frame, cv::Point(hx2, hy2),
             cv::Point(hx2, hy2 - corner_len), color, 3);

    // Label below box
    if (!h.label.empty()) {
      int baseline = 0;
      cv::Size ts = cv::getTextSize(h.label, cv::FONT_HERSHEY_SIMPLEX,
                                    config_.font_scale, 1, &baseline);
      int label_y = hy2 + ts.height + 6;
      if (label_y >= height) label_y = hy1 - 6;

      // Background for readability
      cv::Rect bg(hx1, label_y - ts.height - 2,
                  ts.width + 8, ts.height + baseline + 4);
      bg &= cv::Rect(0, 0, width, height);
      if (bg.width > 0 && bg.height > 0) {
        cv::Mat roi = frame(bg);
        cv::Mat ov;
        roi.copyTo(ov);
        ov.setTo(cv::Scalar(0, 0, 0));
        cv::addWeighted(ov, 0.55, roi, 0.45, 0, roi);
      }

      DrawText(data, width, height, stride, h.label,
               hx1 + 4, label_y - static_cast<int>(config_.font_scale * 28),
               config_.font_scale, 255, 255, 255);
    }
  }
}

}  // namespace loong::overlay
