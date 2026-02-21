// Copyright 2026 Loong AI NVR Project

#include "rules/rule_stage/rule_stage.h"

#include "spdlog/spdlog.h"

#include <chrono>

#include "nlohmann/json.hpp"

namespace loong::rules {

bool RuleStage::Initialize(const StageConfig& config) {
  if (initialized_) return true;

  // Parse channel_id from stage params JSON
  if (!config.params.empty()) {
    try {
      auto j = nlohmann::json::parse(config.params);
      channel_id_ = j.value("channel_id", -1);
    } catch (...) {
      spdlog::warn("RuleStage: failed to parse config params");
    }
  }

  initialized_ = true;
  spdlog::debug("RuleStage: initialized for channel {}", channel_id_);
  return true;
}

bool RuleStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  if (!frame) return PassToNext(nullptr);

  if (engine_) {
    // Evaluate rules if the frame has AI results
    if (frame->analysis.has_result && !frame->analysis.detections.empty()) {
      FrameContext ctx;
      ctx.channel_id = frame->channel_id;
      ctx.frame_width = frame->info.width;
      ctx.frame_height = frame->info.height;

      ctx.timestamp_ms = frame->pts / 1000;
      if (ctx.timestamp_ms <= 0) {
        ctx.timestamp_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count();
      }

      auto events =
          engine_->Evaluate(frame->channel_id, frame->analysis.detections, ctx);

      // Add loitering/alarm target highlights from triggered events
      for (const auto& ev : events) {
        if (ev.rule_type == RuleType::kLoitering ||
            ev.severity == RuleEventSeverity::kAlarm) {
          RuleTargetHighlight hl;
          hl.x1 = ev.trigger_detection.x1;
          hl.y1 = ev.trigger_detection.y1;
          hl.x2 = ev.trigger_detection.x2;
          hl.y2 = ev.trigger_detection.y2;
          hl.alarm = (ev.severity == RuleEventSeverity::kAlarm);
          if (ev.rule_type == RuleType::kLoitering) {
            hl.label =
                ev.rule_name + " " + std::to_string(ev.dwell_time_sec) + "s";
          } else {
            hl.label = ev.rule_name;
          }
          frame->rule_overlay.highlights.push_back(std::move(hl));
        }
      }
    }

    // Always populate rule geometry overlay (lines, regions) for visualization
    auto overlay = engine_->BuildOverlayData(frame->channel_id);
    frame->rule_overlay.lines = std::move(overlay.lines);
    frame->rule_overlay.regions = std::move(overlay.regions);
  }

  return PassToNext(std::move(frame));
}

void RuleStage::Shutdown() {
  initialized_ = false;
  spdlog::debug("RuleStage: shutdown (channel {})", channel_id_);
}

void RuleStage::SetRuleEngine(std::shared_ptr<RuleEngine> engine) {
  engine_ = std::move(engine);
}

}  // namespace loong::rules
