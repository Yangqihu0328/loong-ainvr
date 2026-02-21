// Copyright 2026 Loong AI NVR Project

#include "ai_engine/scheduler/analysis_scheduler.h"

#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <numeric>

namespace loong::ai_engine {

AnalysisScheduler::AnalysisScheduler() = default;

AnalysisScheduler::~AnalysisScheduler() { Stop(); }

// ========== Lifecycle ==========

void AnalysisScheduler::Start() {
  if (running_) return;
  running_ = true;
  worker_ = std::thread(&AnalysisScheduler::SchedulerLoop, this);
  spdlog::info("AnalysisScheduler: started");
}

void AnalysisScheduler::Stop() {
  if (!running_) return;
  running_ = false;
  if (worker_.joinable()) {
    worker_.join();
  }
  spdlog::info("AnalysisScheduler: stopped");
}

// ========== Task Management ==========

bool AnalysisScheduler::AddTask(const AnalysisTask& task) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (tasks_.size() >= static_cast<size_t>(kMaxChannels)) {
    spdlog::error("AnalysisScheduler: max tasks ({}) reached", kMaxChannels);
    return false;
  }

  if (tasks_.count(task.channel_id)) {
    spdlog::warn("AnalysisScheduler: task for ch {} already exists",
                 task.channel_id);
    return false;
  }

  TaskState state;
  state.config = task;
  state.last_analysis_time = std::chrono::steady_clock::now();

  // Compute frame interval from target FPS.
  if (task.target_fps > 0) {
    state.frame_interval = std::chrono::microseconds(1000000 / task.target_fps);
  }
  state.effective_fps = task.target_fps;

  tasks_[task.channel_id] = state;
  spdlog::info(
      "AnalysisScheduler: added task for ch {} "
      "(strategy={}, fps={})",
      task.channel_id, static_cast<int>(task.strategy), task.target_fps);
  return true;
}

bool AnalysisScheduler::RemoveTask(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) {
    return false;
  }
  tasks_.erase(it);
  spdlog::info("AnalysisScheduler: removed task for ch {}", channel_id);
  return true;
}

bool AnalysisScheduler::UpdateTask(int channel_id, const AnalysisTask& task) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) {
    return false;
  }

  auto& state = it->second;
  state.config = task;
  state.config.channel_id = channel_id;

  if (task.target_fps > 0) {
    state.frame_interval = std::chrono::microseconds(1000000 / task.target_fps);
  }

  spdlog::info("AnalysisScheduler: updated task for ch {} (fps={})", channel_id,
               task.target_fps);
  return true;
}

bool AnalysisScheduler::SetEnabled(int channel_id, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) return false;
  it->second.config.enabled = enabled;
  return true;
}

// ========== Scheduling Decision ==========

bool AnalysisScheduler::ShouldAnalyze(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) return false;

  auto& state = it->second;
  if (!state.config.enabled) return false;

  switch (state.config.strategy) {
    case SchedulingStrategy::kRealtime:
      // Always analyze.
      return true;

    case SchedulingStrategy::kPolling: {
      // Rate-limited analysis based on target FPS.
      auto now = std::chrono::steady_clock::now();
      auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          now - state.last_analysis_time);

      // Use effective interval (may be stretched by throttling).
      auto effective_interval =
          state.is_throttled
              ? state.frame_interval * 2  // Double interval when throttled
              : state.frame_interval;

      if (elapsed >= effective_interval) {
        state.last_analysis_time = now;
        return true;
      }
      return false;
    }

    case SchedulingStrategy::kOnDemand:
      // On-demand: only if explicitly triggered.
      // The caller should set a flag; here we always return false
      // as the default — actual triggering is external.
      return false;
  }

  return false;
}

// ========== Inference Reporting ==========

void AnalysisScheduler::ReportInference(int channel_id,
                                        int64_t inference_time_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) return;

  auto& state = it->second;
  state.total_inferences++;
  state.total_frames_analyzed++;
  state.total_inference_time_us += inference_time_us;
  state.recent_inference_time_us = inference_time_us;

  gpu_used_us_ += inference_time_us;
}

// ========== Statistics ==========

AnalysisTaskStats AnalysisScheduler::GetTaskStats(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = tasks_.find(channel_id);
  if (it == tasks_.end()) {
    AnalysisTaskStats empty;
    empty.channel_id = -1;
    return empty;
  }

  const auto& state = it->second;
  AnalysisTaskStats stats;
  stats.channel_id = channel_id;
  stats.target_fps = state.config.target_fps;
  stats.actual_fps = state.effective_fps;
  stats.total_frames_analyzed = state.total_frames_analyzed;
  stats.total_inferences = state.total_inferences;
  stats.is_throttled = state.is_throttled;

  if (state.total_inferences > 0) {
    stats.avg_inference_time_us =
        state.total_inference_time_us / state.total_inferences;
  }

  if (gpu_budget_us_ > 0) {
    stats.gpu_time_share = static_cast<double>(state.recent_inference_time_us *
                                               state.effective_fps) /
                           static_cast<double>(gpu_budget_us_);
  }

  return stats;
}

std::vector<AnalysisTaskStats> AnalysisScheduler::GetAllStats() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<AnalysisTaskStats> result;
  result.reserve(tasks_.size());

  for (const auto& [id, state] : tasks_) {
    AnalysisTaskStats stats;
    stats.channel_id = id;
    stats.target_fps = state.config.target_fps;
    stats.actual_fps = state.effective_fps;
    stats.total_frames_analyzed = state.total_frames_analyzed;
    stats.total_inferences = state.total_inferences;
    stats.is_throttled = state.is_throttled;

    if (state.total_inferences > 0) {
      stats.avg_inference_time_us =
          state.total_inference_time_us / state.total_inferences;
    }
    result.push_back(stats);
  }

  return result;
}

void AnalysisScheduler::SetGpuBudget(int64_t budget_us) {
  std::lock_guard<std::mutex> lock(mutex_);
  gpu_budget_us_ = budget_us;
  spdlog::info("AnalysisScheduler: GPU budget set to {} us", budget_us);
}

double AnalysisScheduler::GetGpuUtilization() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return gpu_utilization_;
}

int AnalysisScheduler::ActiveTaskCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  int count = 0;
  for (const auto& [id, state] : tasks_) {
    if (state.config.enabled) ++count;
  }
  return count;
}

// ========== Scheduling Loop ==========

void AnalysisScheduler::SchedulerLoop() {
  while (running_) {
    RebalanceBudget();

    // Publish metrics event.
    core::EventBus::Instance().Publish("ai.scheduler.metrics");

    // Sleep in small increments for responsive shutdown.
    for (int i = 0; i < kRebalanceIntervalMs / 100 && running_; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }
}

void AnalysisScheduler::RebalanceBudget() {
  std::lock_guard<std::mutex> lock(mutex_);

  if (tasks_.empty()) {
    gpu_utilization_ = 0.0;
    gpu_used_us_ = 0;
    return;
  }

  // Compute total estimated GPU time per second from all active tasks.
  int64_t estimated_total_us = 0;
  for (const auto& [id, state] : tasks_) {
    if (!state.config.enabled) continue;
    // Estimated cost = avg inference time × target FPS.
    int64_t avg_time =
        (state.total_inferences > 0)
            ? (state.total_inference_time_us / state.total_inferences)
            : 10000;  // Default 10ms if unknown
    estimated_total_us += avg_time * state.config.target_fps;
  }

  // GPU utilization: estimated vs budget.
  gpu_utilization_ = (gpu_budget_us_ > 0)
                         ? static_cast<double>(estimated_total_us) /
                               static_cast<double>(gpu_budget_us_)
                         : 0.0;

  // Determine which tasks need throttling.
  if (estimated_total_us > gpu_budget_us_) {
    // Oversubscribed — need to throttle.
    auto order = ComputeSchedulingOrder();

    // Give full budget to high-priority channels first, then distribute.
    int64_t remaining_budget = gpu_budget_us_;

    for (int id : order) {
      auto it = tasks_.find(id);
      if (it == tasks_.end() || !it->second.config.enabled) continue;

      auto& state = it->second;
      int64_t avg_time =
          (state.total_inferences > 0)
              ? (state.total_inference_time_us / state.total_inferences)
              : 10000;

      int64_t needed = avg_time * state.config.target_fps;

      if (remaining_budget >= needed) {
        // Full allocation — no throttling.
        state.is_throttled = false;
        state.effective_fps = state.config.target_fps;
        remaining_budget -= needed;
      } else if (remaining_budget > 0 && avg_time > 0) {
        // Partial allocation — reduce FPS.
        int achievable_fps = static_cast<int>(remaining_budget / avg_time);
        if (achievable_fps < 1) achievable_fps = 1;
        state.is_throttled = true;
        state.effective_fps = achievable_fps;
        remaining_budget = 0;

        spdlog::debug(
            "AnalysisScheduler: ch {} throttled to {} fps "
            "(target={})",
            id, achievable_fps, state.config.target_fps);
      } else {
        // No budget left — heavily throttle.
        state.is_throttled = true;
        state.effective_fps = 1;
      }

      // Update frame interval for the effective FPS.
      if (state.effective_fps > 0) {
        state.frame_interval =
            std::chrono::microseconds(1000000 / state.effective_fps);
      }
    }
  } else {
    // Under budget — remove all throttling.
    for (auto& [id, state] : tasks_) {
      state.is_throttled = false;
      state.effective_fps = state.config.target_fps;
      if (state.config.target_fps > 0) {
        state.frame_interval =
            std::chrono::microseconds(1000000 / state.config.target_fps);
      }
    }
  }

  // Reset cycle counter.
  gpu_used_us_ = 0;
}

std::vector<int> AnalysisScheduler::ComputeSchedulingOrder() const {
  // Sort channels by priority: realtime first, then polling, then on-demand.
  // Within the same strategy, sort by channel_id for determinism.
  std::vector<std::pair<int, int>> order_pairs;  // (priority_score, ch_id)

  for (const auto& [id, state] : tasks_) {
    if (!state.config.enabled) continue;

    int priority = 0;
    switch (state.config.strategy) {
      case SchedulingStrategy::kRealtime:
        priority = 0;  // Highest priority
        break;
      case SchedulingStrategy::kPolling:
        priority = 1;
        break;
      case SchedulingStrategy::kOnDemand:
        priority = 2;  // Lowest priority
        break;
    }
    order_pairs.emplace_back(priority, id);
  }

  std::sort(order_pairs.begin(), order_pairs.end());

  std::vector<int> result;
  result.reserve(order_pairs.size());
  for (const auto& [priority, id] : order_pairs) {
    result.push_back(id);
  }
  return result;
}

}  // namespace loong::ai_engine
