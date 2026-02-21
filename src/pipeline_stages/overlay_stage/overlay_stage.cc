// Copyright 2026 Loong AI NVR Project

#include "pipeline_stages/overlay_stage/overlay_stage.h"

#include "spdlog/spdlog.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace loong::pipeline_stages {

bool OverlayStage::Initialize(const StageConfig& config) {
  OverlayConfig ov_config;
  std::string channel_name;
  int channel_id = -1;

  try {
    auto params = nlohmann::json::parse(config.params);

    ov_config.enabled = params.value("enabled", true);
    ov_config.line_thickness = params.value("line_thickness", 2);
    ov_config.font_scale = params.value("font_scale", 0.5);
    ov_config.show_labels = params.value("show_labels", true);
    ov_config.show_confidence = params.value("show_confidence", true);
    ov_config.fill_opacity = params.value("fill_opacity", 0.08);
    ov_config.show_timestamp = params.value("show_timestamp", true);
    ov_config.show_channel_name = params.value("show_channel_name", true);
    ov_config.timestamp_position = params.value("timestamp_position", 0);
    ov_config.timestamp_format = params.value("timestamp_format", "");
    ov_config.show_trajectory = params.value("show_trajectory", false);
    ov_config.trajectory_max_points = params.value("trajectory_max_points", 30);

    channel_name = params.value("channel_name", "");
    channel_id = params.value("channel_id", -1);
  } catch (...) {
    // Use defaults
  }

  renderer_.Configure(ov_config);
  renderer_.SetChannelInfo(channel_id, channel_name);

  // Load CJK-capable font for OSD text rendering.
  std::string font_path;
  try {
    auto params = nlohmann::json::parse(config.params);
    font_path = params.value("font_path", "");
  } catch (...) {
  }

  if (font_path.empty()) {
    // Search common font locations
    static const char* kFontSearchPaths[] = {
        "resources/fonts/simhei.ttf",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const auto* p : kFontSearchPaths) {
      if (std::ifstream(p).good()) {
        font_path = p;
        break;
      }
    }
  }

  if (!font_path.empty()) {
    int px_size = std::max(14, static_cast<int>(ov_config.font_scale * 28.0));
    renderer_.LoadFont(font_path, px_size);
  }

  spdlog::info(
      "OverlayStage: initialized (ch={}, name='{}', enabled={}, "
      "thickness={}, timestamp={}, trajectory={})",
      channel_id, channel_name, ov_config.enabled, ov_config.line_thickness,
      ov_config.show_timestamp, ov_config.show_trajectory);
  return true;
}

bool OverlayStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  if (!frame || frame->type != FrameType::kRaw) {
    return PassToNext(std::move(frame));
  }

  renderer_.Render(frame->data.get(), frame->info.width, frame->info.height,
                   frame->info.stride, frame->analysis.detections,
                   frame->rule_overlay);

  return PassToNext(std::move(frame));
}

void OverlayStage::Shutdown() { renderer_.ResetTrajectories(); }

}  // namespace loong::pipeline_stages
