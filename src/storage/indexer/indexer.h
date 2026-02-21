// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_INDEXER_INDEXER_H_
#define LOONG_STORAGE_INDEXER_INDEXER_H_

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "storage/record_index/record_index.h"

namespace loong::storage {

/// Configuration for the async indexer.
struct IndexerConfig {
  int batch_size = 100;          // Flush when batch reaches this size
  int flush_interval_ms = 5000;  // Flush every N milliseconds
};

/// Async batch indexing layer over RecordIndex.
/// Accumulates segment and event inserts, flushing them in batches
/// for better SQLite write performance (reduced transaction overhead).
class Indexer {
 public:
  explicit Indexer(std::shared_ptr<RecordIndex> index,
                   const IndexerConfig& config = {});
  ~Indexer();

  /// Start the background flush thread.
  void Start();

  /// Stop the background flush thread and flush remaining entries.
  void Stop();

  /// Queue a segment insert. Returns immediately.
  /// The actual insert happens on the next flush.
  /// segment_id is set asynchronously; use the callback to retrieve it.
  void QueueSegmentInsert(const SegmentInfo& info);

  /// Queue a segment end update.
  void QueueSegmentUpdate(int64_t segment_id, int64_t end_time,
                          int64_t file_size);

  /// Queue an event insert.
  void QueueEventInsert(const EventInfo& event);

  /// Force an immediate flush of all pending operations.
  void Flush();

  /// Get the number of pending operations.
  size_t PendingCount() const;

  // Non-copyable
  Indexer(const Indexer&) = delete;
  Indexer& operator=(const Indexer&) = delete;

 private:
  enum class OpType {
    kInsertSegment,
    kUpdateSegment,
    kInsertEvent,
  };

  struct PendingOp {
    OpType type;
    SegmentInfo segment;
    EventInfo event;
    int64_t segment_id = 0;
    int64_t end_time = 0;
    int64_t file_size = 0;
  };

  void FlushLoop();
  void FlushPending();

  std::shared_ptr<RecordIndex> index_;
  IndexerConfig config_;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::vector<PendingOp> pending_ops_;

  std::atomic<bool> running_{false};
  std::thread flush_thread_;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_INDEXER_INDEXER_H_
