// Copyright 2026 Loong AI NVR Project

#include "storage/indexer/indexer.h"

#include "spdlog/spdlog.h"

#include <chrono>

namespace loong::storage {

Indexer::Indexer(std::shared_ptr<RecordIndex> index,
                 const IndexerConfig& config)
    : index_(std::move(index)), config_(config) {}

Indexer::~Indexer() { Stop(); }

void Indexer::Start() {
  if (running_) return;
  running_ = true;
  flush_thread_ = std::thread(&Indexer::FlushLoop, this);
  spdlog::info("Indexer: started (batch_size={}, flush_interval={}ms)",
               config_.batch_size, config_.flush_interval_ms);
}

void Indexer::Stop() {
  if (!running_) return;
  running_ = false;
  cv_.notify_all();
  if (flush_thread_.joinable()) {
    flush_thread_.join();
  }
  // Flush any remaining operations
  FlushPending();
  spdlog::info("Indexer: stopped");
}

void Indexer::QueueSegmentInsert(const SegmentInfo& info) {
  std::lock_guard<std::mutex> lock(mutex_);
  PendingOp op;
  op.type = OpType::kInsertSegment;
  op.segment = info;
  pending_ops_.push_back(std::move(op));

  if (static_cast<int>(pending_ops_.size()) >= config_.batch_size) {
    cv_.notify_one();
  }
}

void Indexer::QueueSegmentUpdate(int64_t segment_id, int64_t end_time,
                                 int64_t file_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  PendingOp op;
  op.type = OpType::kUpdateSegment;
  op.segment_id = segment_id;
  op.end_time = end_time;
  op.file_size = file_size;
  pending_ops_.push_back(std::move(op));

  if (static_cast<int>(pending_ops_.size()) >= config_.batch_size) {
    cv_.notify_one();
  }
}

void Indexer::QueueEventInsert(const EventInfo& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  PendingOp op;
  op.type = OpType::kInsertEvent;
  op.event = event;
  pending_ops_.push_back(std::move(op));

  if (static_cast<int>(pending_ops_.size()) >= config_.batch_size) {
    cv_.notify_one();
  }
}

void Indexer::Flush() { FlushPending(); }

size_t Indexer::PendingCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return pending_ops_.size();
}

void Indexer::FlushLoop() {
  while (running_) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait_for(
          lock, std::chrono::milliseconds(config_.flush_interval_ms), [this] {
            return !running_ ||
                   static_cast<int>(pending_ops_.size()) >= config_.batch_size;
          });
    }

    if (!pending_ops_.empty()) {
      FlushPending();
    }
  }
}

void Indexer::FlushPending() {
  std::vector<PendingOp> ops;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ops.swap(pending_ops_);
  }

  if (ops.empty()) return;

  spdlog::debug("Indexer: flushing {} pending operations", ops.size());

  int insert_count = 0;
  int update_count = 0;
  int event_count = 0;

  for (const auto& op : ops) {
    switch (op.type) {
      case OpType::kInsertSegment: {
        int64_t id = index_->InsertSegment(op.segment);
        if (id < 0) {
          spdlog::warn("Indexer: failed to insert segment for ch{}",
                       op.segment.channel_id);
        }
        ++insert_count;
        break;
      }
      case OpType::kUpdateSegment: {
        if (!index_->UpdateSegmentEnd(op.segment_id, op.end_time,
                                      op.file_size)) {
          spdlog::warn("Indexer: failed to update segment {}", op.segment_id);
        }
        ++update_count;
        break;
      }
      case OpType::kInsertEvent: {
        int64_t id = index_->InsertEvent(op.event);
        if (id < 0) {
          spdlog::warn("Indexer: failed to insert event for ch{}",
                       op.event.channel_id);
        }
        ++event_count;
        break;
      }
    }
  }

  spdlog::debug("Indexer: flushed {} inserts, {} updates, {} events",
                insert_count, update_count, event_count);
}

}  // namespace loong::storage
