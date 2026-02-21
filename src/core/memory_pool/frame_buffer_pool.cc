// Copyright 2026 Loong AI NVR Project

#include "core/memory_pool/frame_buffer_pool.h"

#include "spdlog/spdlog.h"

namespace loong::core {

// FrameBuffer implementation
FrameBuffer::FrameBuffer(uint8_t* data, size_t size)
    : data_(data), capacity_(size) {}

uint8_t* FrameBuffer::Data() { return data_; }
const uint8_t* FrameBuffer::Data() const { return data_; }
size_t FrameBuffer::Size() const { return capacity_; }

void FrameBuffer::SetActualSize(size_t size) { actual_size_ = size; }
size_t FrameBuffer::ActualSize() const { return actual_size_; }

// FrameBufferPool implementation
FrameBufferPool::FrameBufferPool(size_t count, size_t buffer_size)
    : buffer_size_(buffer_size), total_count_(count) {
  memory_block_.resize(count * buffer_size);

  for (size_t i = 0; i < count; ++i) {
    uint8_t* ptr = memory_block_.data() + i * buffer_size;
    auto buf = std::make_unique<FrameBuffer>(ptr, buffer_size);
    available_.push(buf.get());
    all_buffers_.push_back(std::move(buf));
  }

  spdlog::info("FrameBufferPool created: {} buffers x {} bytes = {} MB", count,
               buffer_size,
               (static_cast<size_t>(count) * buffer_size) / (1024 * 1024));
}

FrameBufferPool::~FrameBufferPool() = default;

std::shared_ptr<FrameBuffer> FrameBufferPool::Acquire() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (available_.empty()) {
    spdlog::warn("FrameBufferPool exhausted!");
    return nullptr;
  }

  FrameBuffer* raw = available_.front();
  available_.pop();

  return {raw, [this](FrameBuffer* buf) { this->Release(buf); }};
}

void FrameBufferPool::Release(FrameBuffer* buffer) {
  std::lock_guard<std::mutex> lock(mutex_);
  buffer->SetActualSize(0);
  available_.push(buffer);
}

size_t FrameBufferPool::TotalCount() const { return total_count_; }

size_t FrameBufferPool::AvailableCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return available_.size();
}

}  // namespace loong::core
