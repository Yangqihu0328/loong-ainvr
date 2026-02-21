// Copyright 2026 Loong AI NVR Project

#include "core/thread_pool/thread_pool.h"

#include "spdlog/spdlog.h"

#include <algorithm>

namespace loong::core {

ThreadPool& ThreadPool::Instance() {
  static ThreadPool instance;
  return instance;
}

ThreadPool::ThreadPool(size_t num_threads) {
  if (num_threads == 0) {
    num_threads = std::max(static_cast<unsigned int>(2),
                           std::thread::hardware_concurrency());
  }

  for (size_t i = 0; i < num_threads; ++i) {
    workers_.emplace_back([this] {
      for (;;) {
        std::function<void()> task;
        {
          std::unique_lock<std::mutex> lock(this->queue_mutex_);
          this->condition_.wait(
              lock, [this] { return this->stop_ || !this->tasks_.empty(); });
          if (this->stop_ && this->tasks_.empty()) {
            return;
          }
          task = std::move(this->tasks_.front());
          this->tasks_.pop();
          ++active_tasks_;
        }
        task();
        {
          std::unique_lock<std::mutex> lock(queue_mutex_);
          --active_tasks_;
          if (tasks_.empty() && active_tasks_ == 0) {
            completion_condition_.notify_all();
          }
        }
      }
    });
  }

  spdlog::info("ThreadPool created with {} workers", num_threads);
}

ThreadPool::~ThreadPool() { Shutdown(); }

size_t ThreadPool::WorkerCount() const { return workers_.size(); }

void ThreadPool::WaitForCompletion() {
  std::unique_lock<std::mutex> lock(queue_mutex_);
  completion_condition_.wait(
      lock, [this] { return tasks_.empty() && active_tasks_ == 0; });
}

void ThreadPool::Shutdown() {
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stop_) return;
    stop_ = true;
  }
  condition_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  spdlog::info("ThreadPool shutdown complete");
}

}  // namespace loong::core
