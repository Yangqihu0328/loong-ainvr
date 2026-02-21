// Copyright 2026 Loong AI NVR Project

#include "core/pipeline/stage_queue.h"

#include "spdlog/spdlog.h"

namespace loong::core {

StageQueue::StageQueue(size_t capacity) : capacity_(capacity) {}

bool StageQueue::Push(std::shared_ptr<Frame> frame) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (shutdown_) return false;

  if (queue_.size() >= capacity_) {
    // Backpressure: drop oldest frame of same channel, or just oldest
    int target_channel = frame->channel_id;
    bool found = false;
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
      if ((*it)->channel_id == target_channel &&
          !(*it)->info.is_keyframe) {
        queue_.erase(it);
        ++dropped_count_;
        found = true;
        break;
      }
    }
    if (!found && !queue_.empty()) {
      // Drop overall oldest non-keyframe
      for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (!(*it)->info.is_keyframe) {
          queue_.erase(it);
          ++dropped_count_;
          found = true;
          break;
        }
      }
    }
    if (!found) {
      // All keyframes and still full — drop incoming frame
      ++dropped_count_;
      return false;
    }
  }

  queue_.push_back(std::move(frame));
  not_empty_.notify_one();
  return true;
}

std::optional<std::shared_ptr<Frame>> StageQueue::Pop(
    std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  if (!not_empty_.wait_for(lock, timeout, [this] {
        return !queue_.empty() || shutdown_;
      })) {
    return std::nullopt;  // Timeout
  }

  if (shutdown_ && queue_.empty()) {
    return std::nullopt;
  }

  auto frame = std::move(queue_.front());
  queue_.pop_front();
  return frame;
}

std::vector<std::shared_ptr<Frame>> StageQueue::PopBatch(
    size_t max_batch, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(mutex_);
  not_empty_.wait_for(lock, timeout, [this] {
    return !queue_.empty() || shutdown_;
  });

  std::vector<std::shared_ptr<Frame>> batch;
  size_t count = std::min(max_batch, queue_.size());
  batch.reserve(count);

  for (size_t i = 0; i < count; ++i) {
    batch.push_back(std::move(queue_.front()));
    queue_.pop_front();
  }

  return batch;
}

size_t StageQueue::Size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}

bool StageQueue::Empty() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.empty();
}

void StageQueue::SetCapacity(size_t capacity) {
  std::lock_guard<std::mutex> lock(mutex_);
  capacity_ = capacity;
}

void StageQueue::Shutdown() {
  std::lock_guard<std::mutex> lock(mutex_);
  shutdown_ = true;
  not_empty_.notify_all();
}

bool StageQueue::IsShutdown() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return shutdown_;
}

int64_t StageQueue::DroppedCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return dropped_count_;
}

}  // namespace loong::core
