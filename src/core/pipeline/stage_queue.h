// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_PIPELINE_STAGE_QUEUE_H_
#define LOONG_CORE_PIPELINE_STAGE_QUEUE_H_

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

#include "core/common/types.h"

namespace loong::core {

/// Thread-safe queue for passing frames between pipeline stages.
/// Supports backpressure (capacity limit with oldest-frame dropping)
/// and batch consumption for AI inference.
class StageQueue {
 public:
  explicit StageQueue(size_t capacity = 128);

  /// Push a frame into the queue. If queue is full, drops the oldest
  /// frame of the same channel to make room.
  /// Returns true if frame was added, false if dropped.
  bool Push(std::shared_ptr<Frame> frame);

  /// Pop a single frame (blocking). Returns nullopt if shutdown.
  std::optional<std::shared_ptr<Frame>> Pop(
      std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

  /// Pop a batch of frames (for AI batch inference).
  /// Waits until batch_size frames are available or timeout expires.
  std::vector<std::shared_ptr<Frame>> PopBatch(
      size_t max_batch,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(10));

  /// Current queue size.
  size_t Size() const;

  /// Check if queue is empty.
  bool Empty() const;

  /// Set maximum capacity.
  void SetCapacity(size_t capacity);

  /// Signal shutdown — unblock all waiting consumers.
  void Shutdown();

  /// Check if shutdown has been signaled.
  bool IsShutdown() const;

  /// Get number of dropped frames.
  int64_t DroppedCount() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable not_empty_;
  std::deque<std::shared_ptr<Frame>> queue_;
  size_t capacity_;
  bool shutdown_ = false;
  int64_t dropped_count_ = 0;
};

}  // namespace loong::core

#endif  // LOONG_CORE_PIPELINE_STAGE_QUEUE_H_
