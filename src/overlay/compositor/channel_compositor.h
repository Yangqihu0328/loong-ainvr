// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_OVERLAY_COMPOSITOR_CHANNEL_COMPOSITOR_H_
#define LOONG_OVERLAY_COMPOSITOR_CHANNEL_COMPOSITOR_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loong::overlay {

/// Grid layout presets for multi-channel composition.
enum class GridLayout {
  k1x1 = 1,    // Single view
  k2x2 = 4,    // 4-channel grid
  k3x3 = 9,    // 9-channel grid
  k4x4 = 16,   // 16-channel grid
  k8x8 = 64,   // 64-channel grid (maximum)
};

/// Information about a single channel's frame for compositing.
struct CompositorInput {
  int channel_id = -1;
  uint8_t* data = nullptr;   // BGR24 frame data
  int width = 0;
  int height = 0;
  int stride = 0;
};

/// Multi-channel compositor that combines multiple video channel frames
/// into a single composite output frame with grid layout.
///
/// Supports:
///   - Configurable grid layouts (1x1, 2x2, 3x3, 4x4, 8x8)
///   - Dynamic channel count — empty cells show "no signal" placeholder
///   - Automatic channel-to-cell assignment
///   - Channel labels on each cell
///   - Thread-safe frame updates
///
/// The compositor maintains an internal frame cache that is updated as
/// individual channel frames arrive. Call Composite() to produce
/// the final output frame.
class ChannelCompositor {
 public:
  /// Create a compositor with the given output dimensions.
  ChannelCompositor(int output_width, int output_height);
  ~ChannelCompositor() = default;

  /// Set the grid layout.
  void SetLayout(GridLayout layout);

  /// Get the current grid layout.
  GridLayout GetLayout() const { return layout_; }

  /// Set output resolution.
  void SetOutputSize(int width, int height);

  /// Update a channel's latest frame. Thread-safe.
  /// The compositor copies the frame data internally.
  void UpdateChannel(const CompositorInput& input);

  /// Remove a channel from the composition. Thread-safe.
  void RemoveChannel(int channel_id);

  /// Assign a channel to a specific grid cell position (0-based).
  void AssignCell(int channel_id, int cell_index);

  /// Clear all cell assignments (channels auto-assign by ID order).
  void ClearAssignments();

  /// Produce the composite output frame. Thread-safe.
  /// @param output     Pre-allocated BGR24 buffer.
  /// @param out_stride Row stride of the output buffer.
  /// @return true on success.
  bool Composite(uint8_t* output, int out_stride);

  /// Get the number of cells in the current layout.
  int CellCount() const;

  /// Get grid dimensions.
  int GridCols() const;
  int GridRows() const;

  /// Get single cell dimensions.
  int CellWidth() const;
  int CellHeight() const;

  // Non-copyable
  ChannelCompositor(const ChannelCompositor&) = delete;
  ChannelCompositor& operator=(const ChannelCompositor&) = delete;

 private:
  struct ChannelFrame {
    int channel_id = -1;
    std::vector<uint8_t> data;
    int width = 0;
    int height = 0;
    int stride = 0;
    bool valid = false;
  };

  void DrawNoSignal(uint8_t* output, int out_stride,
                    int cell_x, int cell_y, int cell_w, int cell_h) const;
  void DrawChannelCell(uint8_t* output, int out_stride,
                       int cell_x, int cell_y, int cell_w, int cell_h,
                       const ChannelFrame& frame);
  void DrawCellLabel(uint8_t* output, int out_stride,
                     int cell_x, int cell_y, int cell_w, int cell_h,
                     int channel_id) const;
  std::vector<int> GetCellChannelOrder() const;

  mutable std::mutex mutex_;
  int output_width_;
  int output_height_;
  GridLayout layout_ = GridLayout::k2x2;

  std::unordered_map<int, ChannelFrame> channel_frames_;
  std::unordered_map<int, int> cell_assignments_;  // cell_index -> channel_id
};

}  // namespace loong::overlay

#endif  // LOONG_OVERLAY_COMPOSITOR_CHANNEL_COMPOSITOR_H_
