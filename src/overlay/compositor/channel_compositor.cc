// Copyright 2026 Loong AI NVR Project

#include "overlay/compositor/channel_compositor.h"

#include "spdlog/spdlog.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <set>
#include <string>

namespace loong::overlay {

ChannelCompositor::ChannelCompositor(int output_width, int output_height)
    : output_width_(output_width), output_height_(output_height) {}

void ChannelCompositor::SetLayout(GridLayout layout) {
  std::lock_guard<std::mutex> lock(mutex_);
  layout_ = layout;
}

void ChannelCompositor::SetOutputSize(int width, int height) {
  std::lock_guard<std::mutex> lock(mutex_);
  output_width_ = width;
  output_height_ = height;
}

int ChannelCompositor::CellCount() const { return static_cast<int>(layout_); }

int ChannelCompositor::GridCols() const {
  return static_cast<int>(
      std::ceil(std::sqrt(static_cast<double>(CellCount()))));
}

int ChannelCompositor::GridRows() const {
  int cols = GridCols();
  return (CellCount() + cols - 1) / cols;
}

int ChannelCompositor::CellWidth() const { return output_width_ / GridCols(); }

int ChannelCompositor::CellHeight() const {
  return output_height_ / GridRows();
}

void ChannelCompositor::UpdateChannel(const CompositorInput& input) {
  if (input.channel_id < 0 || !input.data || input.width <= 0 ||
      input.height <= 0) {
    return;
  }

  std::lock_guard<std::mutex> lock(mutex_);
  auto& cached = channel_frames_[input.channel_id];
  cached.channel_id = input.channel_id;
  cached.width = input.width;
  cached.height = input.height;
  cached.stride = input.stride > 0 ? input.stride : input.width * 3;

  size_t data_size =
      static_cast<size_t>(cached.stride) * static_cast<size_t>(input.height);
  cached.data.resize(data_size);
  std::memcpy(cached.data.data(), input.data, data_size);
  cached.valid = true;
}

void ChannelCompositor::RemoveChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  channel_frames_.erase(channel_id);

  for (auto it = cell_assignments_.begin(); it != cell_assignments_.end();) {
    if (it->second == channel_id) {
      it = cell_assignments_.erase(it);
    } else {
      ++it;
    }
  }
}

void ChannelCompositor::AssignCell(int channel_id, int cell_index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cell_index < 0 || cell_index >= CellCount()) return;
  cell_assignments_[cell_index] = channel_id;
}

void ChannelCompositor::ClearAssignments() {
  std::lock_guard<std::mutex> lock(mutex_);
  cell_assignments_.clear();
}

std::vector<int> ChannelCompositor::GetCellChannelOrder() const {
  int count = CellCount();
  std::vector<int> result(static_cast<size_t>(count), -1);

  for (const auto& [cell_idx, ch_id] : cell_assignments_) {
    if (cell_idx >= 0 && cell_idx < count) {
      result[static_cast<size_t>(cell_idx)] = ch_id;
    }
  }

  std::set<int> assigned;
  for (int ch : result) {
    if (ch >= 0) assigned.insert(ch);
  }

  std::vector<int> unassigned;
  for (const auto& [ch_id, frame] : channel_frames_) {
    if (frame.valid && assigned.find(ch_id) == assigned.end()) {
      unassigned.push_back(ch_id);
    }
  }
  std::sort(unassigned.begin(), unassigned.end());

  size_t ua_idx = 0;
  for (int i = 0; i < count && ua_idx < unassigned.size(); ++i) {
    if (result[static_cast<size_t>(i)] < 0) {
      result[static_cast<size_t>(i)] = unassigned[ua_idx++];
    }
  }

  return result;
}

bool ChannelCompositor::Composite(uint8_t* output, int out_stride) {
  if (!output || output_width_ <= 0 || output_height_ <= 0) return false;

  std::lock_guard<std::mutex> lock(mutex_);

  cv::Mat canvas(output_height_, output_width_, CV_8UC3, output,
                 static_cast<size_t>(out_stride));
  canvas.setTo(cv::Scalar(30, 30, 30));

  int cols = GridCols();
  int rows = GridRows();
  int cell_w = CellWidth();
  int cell_h = CellHeight();

  auto channel_order = GetCellChannelOrder();

  for (int i = 0; i < CellCount(); ++i) {
    int row = i / cols;
    int col = i % cols;
    int cx = col * cell_w;
    int cy = row * cell_h;

    int ch_id = channel_order[static_cast<size_t>(i)];

    if (ch_id >= 0) {
      auto it = channel_frames_.find(ch_id);
      if (it != channel_frames_.end() && it->second.valid) {
        DrawChannelCell(output, out_stride, cx, cy, cell_w, cell_h, it->second);
        DrawCellLabel(output, out_stride, cx, cy, cell_w, cell_h, ch_id);
        continue;
      }
    }

    DrawNoSignal(output, out_stride, cx, cy, cell_w, cell_h);
  }

  // Grid lines
  for (int r = 1; r < rows; ++r) {
    int y = r * cell_h;
    cv::line(canvas, cv::Point(0, y), cv::Point(output_width_ - 1, y),
             cv::Scalar(60, 60, 60), 1);
  }
  for (int c = 1; c < cols; ++c) {
    int x = c * cell_w;
    cv::line(canvas, cv::Point(x, 0), cv::Point(x, output_height_ - 1),
             cv::Scalar(60, 60, 60), 1);
  }

  return true;
}

void ChannelCompositor::DrawNoSignal(uint8_t* output, int out_stride,
                                     int cell_x, int cell_y, int cell_w,
                                     int cell_h) const {
  cv::Mat canvas(output_height_, output_width_, CV_8UC3, output,
                 static_cast<size_t>(out_stride));

  int x2 = std::min(cell_x + cell_w, output_width_);
  int y2 = std::min(cell_y + cell_h, output_height_);
  if (x2 <= cell_x || y2 <= cell_y) return;

  cv::Rect cell_rect(cell_x, cell_y, x2 - cell_x, y2 - cell_y);
  cv::Mat cell = canvas(cell_rect);
  cell.setTo(cv::Scalar(20, 20, 20));

  std::string text = "No Signal";
  double font_scale =
      std::max(0.3, std::min(1.0, static_cast<double>(cell_w) / 300.0));
  int baseline = 0;
  cv::Size text_size =
      cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, font_scale, 1, &baseline);

  int tx = std::max(0, (cell_rect.width - text_size.width) / 2);
  int ty =
      std::max(text_size.height, (cell_rect.height + text_size.height) / 2);

  cv::putText(cell, text, cv::Point(tx, ty), cv::FONT_HERSHEY_SIMPLEX,
              font_scale, cv::Scalar(100, 100, 100), 1, cv::LINE_AA);
}

void ChannelCompositor::DrawChannelCell(uint8_t* output, int out_stride,
                                        int cell_x, int cell_y, int cell_w,
                                        int cell_h, const ChannelFrame& frame) {
  cv::Mat canvas(output_height_, output_width_, CV_8UC3, output,
                 static_cast<size_t>(out_stride));

  int x2 = std::min(cell_x + cell_w, output_width_);
  int y2 = std::min(cell_y + cell_h, output_height_);
  if (x2 <= cell_x || y2 <= cell_y) return;

  cv::Rect cell_rect(cell_x, cell_y, x2 - cell_x, y2 - cell_y);
  cv::Mat cell = canvas(cell_rect);

  cv::Mat src(frame.height, frame.width, CV_8UC3,
              const_cast<uint8_t*>(frame.data.data()),
              static_cast<size_t>(frame.stride));

  // Resize maintaining aspect ratio
  double src_aspect = static_cast<double>(frame.width) / frame.height;
  double cell_aspect = static_cast<double>(cell_rect.width) / cell_rect.height;

  int dst_w = 0;
  int dst_h = 0;
  if (src_aspect > cell_aspect) {
    dst_w = cell_rect.width;
    dst_h = static_cast<int>(cell_rect.width / src_aspect);
  } else {
    dst_h = cell_rect.height;
    dst_w = static_cast<int>(cell_rect.height * src_aspect);
  }
  dst_w = std::max(1, dst_w);
  dst_h = std::max(1, dst_h);

  cv::Mat resized;
  cv::resize(src, resized, cv::Size(dst_w, dst_h), 0, 0, cv::INTER_LINEAR);

  // Center with letterboxing
  int offset_x = (cell_rect.width - dst_w) / 2;
  int offset_y = (cell_rect.height - dst_h) / 2;

  cell.setTo(cv::Scalar(0, 0, 0));

  cv::Rect dst_rect(offset_x, offset_y, dst_w, dst_h);
  dst_rect &= cv::Rect(0, 0, cell_rect.width, cell_rect.height);
  if (dst_rect.width > 0 && dst_rect.height > 0) {
    cv::Mat dst_roi = cell(dst_rect);
    cv::Mat src_cropped =
        resized(cv::Rect(0, 0, dst_rect.width, dst_rect.height));
    src_cropped.copyTo(dst_roi);
  }
}

void ChannelCompositor::DrawCellLabel(uint8_t* output, int out_stride,
                                      int cell_x, int cell_y, int cell_w,
                                      int /*cell_h*/, int channel_id) const {
  cv::Mat canvas(output_height_, output_width_, CV_8UC3, output,
                 static_cast<size_t>(out_stride));

  std::string label = "CH" + std::to_string(channel_id);
  double font_scale =
      std::max(0.3, std::min(0.6, static_cast<double>(cell_w) / 500.0));
  int baseline = 0;
  cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX,
                                       font_scale, 1, &baseline);

  int pad = 3;
  int lx = cell_x + pad;
  int ly = cell_y + pad;
  int lw = text_size.width + pad * 2;
  int lh = text_size.height + baseline + pad * 2;

  cv::Rect bg_rect(lx, ly, lw, lh);
  bg_rect &= cv::Rect(0, 0, output_width_, output_height_);
  if (bg_rect.width > 0 && bg_rect.height > 0) {
    cv::Mat roi = canvas(bg_rect);
    cv::Mat overlay_mat;
    roi.copyTo(overlay_mat);
    overlay_mat.setTo(cv::Scalar(0, 0, 0));
    cv::addWeighted(overlay_mat, 0.5, roi, 0.5, 0, roi);
  }

  cv::putText(canvas, label, cv::Point(lx + pad, ly + text_size.height + pad),
              cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(200, 200, 200),
              1, cv::LINE_AA);
}

}  // namespace loong::overlay
