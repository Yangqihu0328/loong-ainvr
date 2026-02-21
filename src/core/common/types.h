// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_COMMON_TYPES_H_
#define LOONG_CORE_COMMON_TYPES_H_

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace loong {

/// Frame priority for scheduling decisions.
enum class FramePriority {
  kRealtime = 0,   // Live preview — highest priority
  kRecord = 1,     // Recording — high priority
  kAnalysis = 2,   // AI analysis — can be degraded
};

/// Video codec type.
enum class CodecType {
  kH264,
  kH265,
  kUnknown,
};

/// Frame type.
enum class FrameType {
  kRaw,       // Decoded raw frame (YUV/RGB)
  kEncoded,   // Encoded frame (H.264/H.265 NAL units)
};

/// Detection result from AI analysis.
struct Detection {
  float x1, y1, x2, y2;  // Bounding box (absolute coordinates)
  float confidence;
  int class_id;
  std::string class_name;
};

/// Result from a secondary (cascade) model applied to a primary detection's ROI.
struct SecondaryResult {
  int parent_index = -1;                      // Index in primary detections[]
  std::string model_name;                     // Model that produced this result
  std::vector<Detection> detections;          // Sub-detections within the ROI
  std::map<std::string, std::string> attributes;  // Classification attributes
};

/// Analysis result attached to a frame after AI processing.
struct AnalysisResult {
  bool has_result = false;
  std::vector<Detection> detections;
  int64_t inference_time_us = 0;  // Inference time in microseconds

  // Multi-model cascade results (FEAT-2.1)
  std::vector<SecondaryResult> secondary_results;
};

/// Visual overlay for a virtual line (cross-line / counting rules).
struct RuleLineOverlay {
  double x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // Normalized 0.0~1.0
  std::string label;
  int count_a_to_b = 0;
  int count_b_to_a = 0;
  bool show_counts = false;
};

/// Visual overlay for a polygon region (intrusion / loitering rules).
struct RuleRegionOverlay {
  std::vector<std::pair<double, double>> vertices;  // Normalized 0.0~1.0
  std::string label;
  bool alarm_active = false;
};

/// Highlight for a target under rule evaluation (e.g. loitering).
struct RuleTargetHighlight {
  float x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // Absolute pixel coordinates
  std::string label;
  bool alarm = false;
};

/// Aggregated rule visualization data attached to a frame.
struct RuleOverlayData {
  std::vector<RuleLineOverlay> lines;
  std::vector<RuleRegionOverlay> regions;
  std::vector<RuleTargetHighlight> highlights;
};

/// Video frame metadata.
struct FrameInfo {
  int width = 0;
  int height = 0;
  int stride = 0;       // Row stride in bytes
  int pixel_format = 0; // AVPixelFormat or custom enum
  bool is_keyframe = false;
  CodecType codec = CodecType::kUnknown;
};

/// A video frame flowing through the pipeline.
struct Frame {
  int channel_id = -1;
  int64_t pts = 0;                   // Presentation timestamp (microseconds)
  int64_t dts = 0;                   // Decode timestamp
  FrameType type = FrameType::kRaw;
  FrameInfo info;
  FramePriority priority = FramePriority::kAnalysis;

  // Raw frame data (decoded)
  std::shared_ptr<uint8_t[]> data;
  size_t data_size = 0;

  // Encoded packet data
  std::shared_ptr<uint8_t[]> packet_data;
  size_t packet_size = 0;

  // AI analysis result (populated by AI stage)
  AnalysisResult analysis;

  // Rule visualization overlays (populated by RuleStage)
  RuleOverlayData rule_overlay;
};

/// Channel state machine states.
enum class ChannelState {
  kCreated,
  kConfigured,
  kRunning,
  kStopping,
  kStopped,
  kError,
  kDestroyed,
};

/// Convert ChannelState to string.
inline const char* ChannelStateToString(ChannelState state) {
  switch (state) {
    case ChannelState::kCreated:    return "Created";
    case ChannelState::kConfigured: return "Configured";
    case ChannelState::kRunning:    return "Running";
    case ChannelState::kStopping:   return "Stopping";
    case ChannelState::kStopped:    return "Stopped";
    case ChannelState::kError:      return "Error";
    case ChannelState::kDestroyed:  return "Destroyed";
  }
  return "Unknown";
}

/// Configuration for a pipeline stage.
struct StageConfig {
  std::string name;
  int thread_count = 1;
  size_t queue_capacity = 128;
  // Stage-specific parameters stored as JSON string
  std::string params;
};

/// Overlay rendering configuration per channel.
struct OverlayConfig {
  bool enabled = true;             // Master switch for overlay rendering
  int line_thickness = 2;          // Bounding box line thickness
  double font_scale = 0.5;         // Font scale for labels
  bool show_labels = true;         // Show class name labels
  bool show_confidence = true;     // Show confidence percentage
  bool show_timestamp = true;      // Show current date/time on frame
  bool show_channel_name = true;   // Show channel name/ID on frame
  bool show_trajectory = false;    // Show object tracking trajectory lines
  int trajectory_max_points = 30;  // Max history points per tracked object
  int timestamp_position = 0;      // 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right
  double fill_opacity = 0.08;      // Semi-transparent fill opacity for bounding boxes
  std::string timestamp_format;    // Custom format, empty = default "%Y-%m-%d %H:%M:%S"
};

/// Configuration for a channel.
struct ChannelConfig {
  int id = -1;
  std::string name;
  std::string rtsp_url;
  CodecType codec = CodecType::kUnknown;  // Auto-detect from stream
  int width = 0;                           // Auto-detect from stream
  int height = 0;                          // Auto-detect from stream
  int framerate = 0;                       // Auto-detect from stream
  int bitrate_kbps = 4000;

  // AI settings
  std::string ai_model_name;     // e.g., "yolov8n"
  std::string ai_backend;        // e.g., "onnxruntime"
  float confidence_threshold = 0.5F;
  int analysis_fps = 0;

  // Overlay settings
  OverlayConfig overlay;

  // Recording settings
  bool record_enabled = true;
  std::string record_path;

  // GIS location (WGS84)
  double latitude = 0.0;
  double longitude = 0.0;
};

/// Runtime status of a channel.
struct ChannelStatus {
  int id = -1;
  std::string name;
  ChannelState state = ChannelState::kCreated;
  int fps_in = 0;      // Input FPS
  int fps_decode = 0;   // Decode FPS
  int fps_ai = 0;       // AI analysis FPS
  int64_t frames_processed = 0;
  int64_t frames_dropped = 0;
  std::string error_message;

  // GIS location (propagated from config)
  double latitude = 0.0;
  double longitude = 0.0;
};

}  // namespace loong

#endif  // LOONG_CORE_COMMON_TYPES_H_
