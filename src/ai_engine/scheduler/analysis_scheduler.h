// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_SCHEDULER_ANALYSIS_SCHEDULER_H_
#define LOONG_AI_ENGINE_SCHEDULER_ANALYSIS_SCHEDULER_H_

#include "ai_engine/inference/inference_engine.h"
#include "core/common/types.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace loong::ai_engine {

/// Scheduling strategy for analysis tasks.
enum class SchedulingStrategy {
  kRealtime,  // Analyze every frame (highest resource usage)
  kPolling,   // Round-robin across channels at configured FPS
  kOnDemand,  // Only analyze when explicitly requested
};

/// Configuration for a single channel's analysis task.
struct AnalysisTask {
  int channel_id = -1;
  std::string model_name;
  float confidence_threshold = 0.5F;
  int target_fps = 5;  // Target analysis FPS
  SchedulingStrategy strategy = SchedulingStrategy::kPolling;
  bool enabled = true;
};

/// Runtime statistics for a channel's analysis task.
struct AnalysisTaskStats {
  int channel_id = -1;
  int actual_fps = 0;
  int target_fps = 0;
  int64_t total_frames_analyzed = 0;
  int64_t total_inferences = 0;
  int64_t avg_inference_time_us = 0;
  double gpu_time_share = 0.0;  // Fraction of GPU budget consumed
  bool is_throttled = false;    // Whether the task is being throttled
};

/// Manages the scheduling and resource allocation of AI analysis tasks
/// across up to 64 concurrent video channels.
///
/// Responsibilities:
/// - **Task Management**: Register/unregister analysis tasks per channel.
/// - **Scheduling**: Decide which channels get analyzed in each cycle,
///   using configurable strategies (realtime, polling, on-demand).
/// - **GPU Budget**: Enforce a total GPU time budget to prevent
///   oversubscription. When the budget is exceeded, lower-priority
///   channels are throttled (analysis FPS reduced).
/// - **Priority Scheduling**: Channels with FramePriority::kRealtime
///   are always served first; kAnalysis channels can be degraded.
/// - **Statistics**: Track per-channel and aggregate scheduling metrics,
///   published via EventBus as "ai.scheduler.metrics".
class AnalysisScheduler {
 public:
  static constexpr int kMaxChannels = 64;

  AnalysisScheduler();
  ~AnalysisScheduler();

  /// Start the scheduling loop.
  void Start();

  /// Stop the scheduling loop.
  void Stop();

  /// Register an analysis task for a channel.
  bool AddTask(const AnalysisTask& task);

  /// Remove an analysis task.
  bool RemoveTask(int channel_id);

  /// Update an existing task's configuration.
  bool UpdateTask(int channel_id, const AnalysisTask& task);

  /// Enable or disable analysis for a channel.
  bool SetEnabled(int channel_id, bool enabled);

  /// Check if a channel should be analyzed right now.
  /// Called by the pipeline's AiStage to decide whether to skip a frame.
  /// Returns true if the frame should be analyzed.
  bool ShouldAnalyze(int channel_id);

  /// Report that an inference was completed for a channel.
  /// @param inference_time_us  The time taken for inference in microseconds.
  void ReportInference(int channel_id, int64_t inference_time_us);

  /// Get statistics for a specific channel.
  AnalysisTaskStats GetTaskStats(int channel_id) const;

  /// Get statistics for all tasks.
  std::vector<AnalysisTaskStats> GetAllStats() const;

  /// Set the total GPU time budget per scheduling cycle (in microseconds).
  /// When the sum of inference times exceeds this budget, lower-priority
  /// channels are throttled.
  void SetGpuBudget(int64_t budget_us);

  /// Get current GPU utilization as a fraction (0.0 - 1.0).
  double GetGpuUtilization() const;

  /// Get the number of active (enabled) tasks.
  int ActiveTaskCount() const;

  // Non-copyable
  AnalysisScheduler(const AnalysisScheduler&) = delete;
  AnalysisScheduler& operator=(const AnalysisScheduler&) = delete;

 private:
  /// Internal per-channel scheduling state.
  struct TaskState {
    AnalysisTask config;

    // Timing
    std::chrono::steady_clock::time_point last_analysis_time;
    std::chrono::microseconds frame_interval{200000};  // 1/target_fps

    // Statistics
    int64_t total_frames_analyzed = 0;
    int64_t total_inferences = 0;
    int64_t total_inference_time_us = 0;
    int64_t recent_inference_time_us = 0;

    // Throttling
    bool is_throttled = false;
    int effective_fps = 0;
  };

  /// Background loop that recalculates scheduling decisions.
  void SchedulerLoop();

  /// Rebalance GPU budget across all active tasks.
  void RebalanceBudget();

  /// Compute the scheduling order for the next cycle.
  std::vector<int> ComputeSchedulingOrder() const;

  mutable std::mutex mutex_;
  std::unordered_map<int, TaskState> tasks_;

  std::atomic<bool> running_{false};
  std::thread worker_;

  // GPU budget management
  int64_t gpu_budget_us_ = 100000;  // 100ms per cycle (default)
  int64_t gpu_used_us_ = 0;
  double gpu_utilization_ = 0.0;

  // Scheduling cycle interval (how often to rebalance).
  static constexpr int kRebalanceIntervalMs = 1000;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_SCHEDULER_ANALYSIS_SCHEDULER_H_
