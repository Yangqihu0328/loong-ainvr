// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_MEMORY_POOL_FRAME_BUFFER_POOL_H_
#define LOONG_CORE_MEMORY_POOL_FRAME_BUFFER_POOL_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace loong::core {

/// A pre-allocated frame buffer for zero-copy video frame passing.
class FrameBuffer {
 public:
  FrameBuffer(uint8_t* data, size_t size);

  uint8_t* Data();
  const uint8_t* Data() const;
  size_t Size() const;

  void SetActualSize(size_t size);
  size_t ActualSize() const;

 private:
  uint8_t* data_;
  size_t capacity_;
  size_t actual_size_ = 0;
};

/// Pre-allocated pool of FrameBuffers for efficient memory reuse.
/// Uses shared_ptr with custom deleter to auto-return buffers to pool.
class FrameBufferPool {
 public:
  /// Create a pool with `count` buffers, each of `buffer_size` bytes.
  FrameBufferPool(size_t count, size_t buffer_size);
  ~FrameBufferPool();

  /// Acquire a buffer from the pool. Returns nullptr if pool is exhausted.
  std::shared_ptr<FrameBuffer> Acquire();

  /// Get pool statistics.
  size_t TotalCount() const;
  size_t AvailableCount() const;

  // Non-copyable
  FrameBufferPool(const FrameBufferPool&) = delete;
  FrameBufferPool& operator=(const FrameBufferPool&) = delete;

 private:
  void Release(FrameBuffer* buffer);

  std::vector<uint8_t> memory_block_;
  std::queue<FrameBuffer*> available_;
  std::vector<std::unique_ptr<FrameBuffer>> all_buffers_;
  mutable std::mutex mutex_;
  size_t buffer_size_;
  size_t total_count_;
};

}  // namespace loong::core

#endif  // LOONG_CORE_MEMORY_POOL_FRAME_BUFFER_POOL_H_
