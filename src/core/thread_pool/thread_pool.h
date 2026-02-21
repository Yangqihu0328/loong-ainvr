// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_THREAD_POOL_THREAD_POOL_H_
#define LOONG_CORE_THREAD_POOL_THREAD_POOL_H_

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace loong::core {

/// A general-purpose thread pool for task scheduling.
class ThreadPool {
 public:
  static ThreadPool& Instance();

  /// Submit a task to the thread pool. Returns a future for the result.
  template <typename F, typename... Args>
  auto Submit(F&& f, Args&&... args)
      -> std::future<typename std::invoke_result<F, Args...>::type>;

  /// Get the number of worker threads.
  size_t WorkerCount() const;

  /// Wait for all pending tasks to complete.
  void WaitForCompletion();

  /// Shutdown the thread pool.
  void Shutdown();

  ~ThreadPool();

  // Non-copyable
  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

 private:
  explicit ThreadPool(size_t num_threads = 0);

  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;

  std::mutex queue_mutex_;
  std::condition_variable condition_;
  std::condition_variable completion_condition_;
  bool stop_ = false;
  size_t active_tasks_ = 0;
};

// Template implementation
template <typename F, typename... Args>
auto ThreadPool::Submit(F&& f, Args&&... args)
    -> std::future<typename std::invoke_result<F, Args...>::type> {
  using ReturnType = typename std::invoke_result<F, Args...>::type;

  auto task = std::make_shared<std::packaged_task<ReturnType()>>(
      std::bind(std::forward<F>(f), std::forward<Args>(args)...));

  std::future<ReturnType> result = task->get_future();
  {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stop_) {
      throw std::runtime_error("Submit on stopped ThreadPool");
    }
    tasks_.emplace([task]() { (*task)(); });
  }
  condition_.notify_one();
  return result;
}

}  // namespace loong::core

#endif  // LOONG_CORE_THREAD_POOL_THREAD_POOL_H_
