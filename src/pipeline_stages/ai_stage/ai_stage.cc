// Copyright 2026 Loong AI NVR Project

#include "pipeline_stages/ai_stage/ai_stage.h"

#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

#include <chrono>
#include <nlohmann/json.hpp>

namespace loong::pipeline_stages {

bool AiStage::Initialize(const StageConfig& config) {
  try {
    auto params = nlohmann::json::parse(config.params);
    confidence_threshold_ = params.value("confidence_threshold", 0.5F);
    batch_size_ = params.value("batch_size", 1);
    if (batch_size_ < 1) batch_size_ = 1;
    skip_frames_ = params.value("skip_frames", 0);
    if (skip_frames_ < 0) skip_frames_ = 0;
  } catch (const std::exception& e) {
    spdlog::debug("AiStage: failed to parse config, using defaults: {}",
                  e.what());
  }

  initialized_ = true;
  spdlog::info(
      "AiStage: initialized (threshold={}, batch_size={}, skip_frames={})",
      confidence_threshold_, batch_size_, skip_frames_);
  return true;
}

// --- Async inference worker thread ---

void AiStage::InferWorker() {
  while (!shutdown_infer_.load()) {
    std::shared_ptr<Frame> frame;
    {
      std::unique_lock<std::mutex> lk(infer_mutex_);
      infer_cv_.wait(lk, [this] {
        return pending_frame_ != nullptr || shutdown_infer_.load();
      });
      if (shutdown_infer_.load()) break;
      frame = std::move(pending_frame_);
    }

    if (!frame || !engine_) continue;

    auto t0 = std::chrono::steady_clock::now();
    std::vector<Detection> dets;
    bool ok = engine_->Infer(frame->data.get(), frame->info.width,
                             frame->info.height, confidence_threshold_, dets);
    auto t1 = std::chrono::steady_clock::now();
    auto us =
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (ok && !dets.empty()) {
      AnalysisResult result;
      result.has_result = true;
      result.detections = std::move(dets);
      result.inference_time_us = us;
      {
        std::lock_guard<std::mutex> rlk(result_mutex_);
        last_result_ = std::move(result);
      }

      try {
        std::lock_guard<std::mutex> rlk(result_mutex_);
        float max_conf = 0.0F;
        std::string top_class;
        for (const auto& d : last_result_.detections) {
          if (d.confidence > max_conf) {
            max_conf = d.confidence;
            top_class = d.class_name;
          }
        }
        nlohmann::json det_event;
        det_event["channel_id"] = frame->channel_id;
        det_event["detection_count"] =
            static_cast<int>(last_result_.detections.size());
        det_event["event_type"] =
            top_class.empty() ? "object_detected" : top_class;
        det_event["max_confidence"] = max_conf;
        loong::core::EventBus::Instance().Publish("ai.detection",
                                                  det_event.dump());
      } catch (...) {
      }
    }

    ++infer_count_;
    total_infer_us_ += us;
    infer_running_.store(false);
  }
}

// --- ProcessFrame (single-frame pipeline path) ---

bool AiStage::ProcessFrame(std::shared_ptr<Frame> frame) {
  if (!frame || frame->type != FrameType::kRaw) {
    return PassToNext(std::move(frame));
  }

  if (!engine_ && !(cascade_ && !cascade_->Empty())) {
    return PassToNext(std::move(frame));
  }

  if (cascade_ && !cascade_->Empty()) {
    return InferCascade(frame);
  }

  // Start inference worker on first call.
  if (!infer_thread_.joinable()) {
    infer_thread_ = std::thread(&AiStage::InferWorker, this);
  }

  // Apply latest async result to all frames.
  {
    std::lock_guard<std::mutex> rlk(result_mutex_);
    if (last_result_.has_result) {
      frame->analysis = last_result_;
    }
  }

  // Submit for async inference if it's time and worker is idle.
  ++frame_counter_;
  if (skip_frames_ <= 0 || frame_counter_ % (skip_frames_ + 1) == 0) {
    if (!infer_running_.exchange(true)) {
      {
        std::lock_guard<std::mutex> lk(infer_mutex_);
        pending_frame_ = frame;
      }
      infer_cv_.notify_one();
    }
  }

  // Periodic stats.
  ++pass_count_;
  auto now = std::chrono::steady_clock::now();
  auto sec =
      std::chrono::duration_cast<std::chrono::seconds>(now - stats_start_)
          .count();
  if (sec >= 10) {
    int64_t ic = infer_count_.exchange(0);
    int64_t ius = total_infer_us_.exchange(0);
    if (ic > 0) {
      double ifps = static_cast<double>(ic) / static_cast<double>(sec);
      double avg_ms =
          static_cast<double>(ius) / static_cast<double>(ic) / 1000.0;
      double pfps = static_cast<double>(pass_count_) / static_cast<double>(sec);
      spdlog::info(
          "AiStage: infer {:.1f} fps, avg {:.1f} ms/frame, "
          "throughput {:.1f} fps",
          ifps, avg_ms, pfps);
    }
    pass_count_ = 0;
    stats_start_ = now;
  }

  return PassToNext(std::move(frame));
}

// --- ProcessBatch (batch pipeline path) ---

bool AiStage::ProcessBatch(std::vector<std::shared_ptr<Frame>>& frames) {
  if (frames.empty()) return true;

  if (!engine_) {
    for (auto& f : frames) {
      PassToNext(std::move(f));
    }
    return true;
  }

  // Start inference worker on first call.
  if (!infer_thread_.joinable()) {
    infer_thread_ = std::thread(&AiStage::InferWorker, this);
  }

  // Find a frame to submit for async inference.
  for (size_t i = 0; i < frames.size(); ++i) {
    if (frames[i] && frames[i]->type == FrameType::kRaw && frames[i]->data) {
      ++frame_counter_;
      if (skip_frames_ <= 0 || frame_counter_ % (skip_frames_ + 1) == 0) {
        if (!infer_running_.exchange(true)) {
          std::lock_guard<std::mutex> lk(infer_mutex_);
          pending_frame_ = frames[i];
          infer_cv_.notify_one();
        }
      }
    }
  }

  // Apply latest result to all frames and pass through immediately.
  AnalysisResult current;
  {
    std::lock_guard<std::mutex> rlk(result_mutex_);
    current = last_result_;
  }

  pass_count_ += static_cast<int64_t>(frames.size());

  // Periodic stats.
  auto now = std::chrono::steady_clock::now();
  auto sec =
      std::chrono::duration_cast<std::chrono::seconds>(now - stats_start_)
          .count();
  if (sec >= 10) {
    int64_t ic = infer_count_.exchange(0);
    int64_t ius = total_infer_us_.exchange(0);
    if (ic > 0) {
      double infer_fps = static_cast<double>(ic) / static_cast<double>(sec);
      double avg_ms =
          static_cast<double>(ius) / static_cast<double>(ic) / 1000.0;
      double pass_fps =
          static_cast<double>(pass_count_) / static_cast<double>(sec);
      spdlog::info(
          "AiStage: infer {:.1f} fps, avg {:.1f} ms/frame, "
          "throughput {:.1f} fps",
          infer_fps, avg_ms, pass_fps);
    }
    pass_count_ = 0;
    stats_start_ = now;
  }

  for (auto& f : frames) {
    if (f && current.has_result) {
      f->analysis = current;
    }
    PassToNext(std::move(f));
  }
  return true;
}

// --- Single-frame sync inference (used by ProcessFrame fallback) ---

bool AiStage::InferSingle(std::shared_ptr<Frame>& frame) {
  auto start = std::chrono::steady_clock::now();

  std::vector<Detection> detections;
  bool ok =
      engine_->Infer(frame->data.get(), frame->info.width, frame->info.height,
                     confidence_threshold_, detections);

  auto end = std::chrono::steady_clock::now();
  auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start)
          .count();

  if (ok && !detections.empty()) {
    frame->analysis.has_result = true;
    frame->analysis.detections = std::move(detections);
    frame->analysis.inference_time_us = elapsed_us;
  } else if (ok) {
    frame->analysis.has_result = true;
    frame->analysis.inference_time_us = elapsed_us;
  }

  return PassToNext(std::move(frame));
}

// --- Cascade inference ---

bool AiStage::InferCascade(std::shared_ptr<Frame>& frame) {
  AnalysisResult result;
  bool ok = cascade_->Run(frame->data.get(), frame->info.width,
                          frame->info.height, result);
  if (ok) {
    frame->analysis = std::move(result);

    if (!frame->analysis.detections.empty()) {
      try {
        float max_conf = 0.0F;
        std::string top_class;
        for (const auto& d : frame->analysis.detections) {
          if (d.confidence > max_conf) {
            max_conf = d.confidence;
            top_class = d.class_name;
          }
        }
        nlohmann::json det_event;
        det_event["channel_id"] = frame->channel_id;
        det_event["detection_count"] =
            static_cast<int>(frame->analysis.detections.size());
        det_event["event_type"] =
            top_class.empty() ? "object_detected" : top_class;
        det_event["max_confidence"] = max_conf;

        if (!frame->analysis.secondary_results.empty()) {
          nlohmann::json secondaries = nlohmann::json::array();
          for (const auto& sr : frame->analysis.secondary_results) {
            nlohmann::json s;
            s["model_name"] = sr.model_name;
            s["parent_index"] = sr.parent_index;
            for (const auto& [k, v] : sr.attributes) {
              s[k] = v;
            }
            for (const auto& d : sr.detections) {
              s["class_name"] = d.class_name;
              s["confidence"] = d.confidence;
            }
            secondaries.push_back(s);
          }
          det_event["secondary_results"] = secondaries;
        }

        loong::core::EventBus::Instance().Publish("ai.detection",
                                                  det_event.dump());
      } catch (const std::exception& e) {
        spdlog::debug("AiStage: cascade event publish failed: {}", e.what());
      }
    }
  }
  return PassToNext(std::move(frame));
}

// --- Lifecycle ---

void AiStage::Shutdown() {
  shutdown_infer_.store(true);
  infer_cv_.notify_one();
  if (infer_thread_.joinable()) {
    infer_thread_.join();
  }
  engine_.reset();
  cascade_.reset();
  initialized_ = false;
}

void AiStage::SetInferenceEngine(
    std::shared_ptr<ai_engine::InferenceEngine> engine) {
  engine_ = std::move(engine);
}

void AiStage::SetModelCascade(
    std::shared_ptr<ai_engine::ModelCascade> cascade) {
  cascade_ = std::move(cascade);
}

}  // namespace loong::pipeline_stages
