// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_AI_ENGINE_FACE_FACE_STORE_H_
#define LOONG_AI_ENGINE_FACE_FACE_STORE_H_

#include <cstdint>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loong::ai_engine {

/// A stored face record with attributes and embedding.
struct FaceRecord {
  int64_t id = 0;
  int channel_id = -1;
  int64_t timestamp = 0;  // Unix timestamp (ms)
  float confidence = 0.0F;
  int age = -1;
  std::string gender;            // "male" / "female" / "unknown"
  std::vector<float> embedding;  // 512-d feature vector
  std::string snapshot_path;     // Path to face snapshot image
};

/// Query parameters for searching face records.
struct FaceQuery {
  int channel_id = -1;     // -1 = all channels
  int64_t start_time = 0;  // 0 = no lower bound
  int64_t end_time = 0;    // 0 = no upper bound
  std::string gender;      // "" = any
  int age_min = -1;        // -1 = no lower bound
  int age_max = -1;        // -1 = no upper bound
  int limit = 100;
  int offset = 0;
};

/// SQLite-based persistent storage for face records.
/// Thread-safe — all public methods are mutex-protected.
///
/// Embeddings are stored as BLOBs for efficient retrieval.
/// Similarity search is done in-memory after loading candidates.
class FaceStore {
 public:
  FaceStore();
  ~FaceStore();

  bool Open(const std::string& db_path);
  void Close();

  /// Insert a new face record. Returns the record ID, or -1 on failure.
  int64_t Insert(const FaceRecord& record);

  /// Query face records with attribute filters.
  std::vector<FaceRecord> Query(const FaceQuery& query);

  /// Get a single record by ID.
  FaceRecord GetById(int64_t id);

  /// Find faces similar to a given embedding (cosine similarity >= threshold).
  /// Returns matches sorted by similarity descending, up to `limit` results.
  std::vector<std::pair<FaceRecord, float>> SearchByEmbedding(
      const std::vector<float>& query_embedding, float threshold = 0.5F,
      int limit = 10);

  /// Delete records older than the given timestamp.
  int PurgeOlderThan(int64_t timestamp_ms);

  /// Get total record count.
  int64_t Count();

  FaceStore(const FaceStore&) = delete;
  FaceStore& operator=(const FaceStore&) = delete;

 private:
  void CreateTables();

  static std::vector<float> BlobToEmbedding(const void* data, int bytes);
  static float CosineSimilarity(const std::vector<float>& a,
                                const std::vector<float>& b);

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::ai_engine

#endif  // LOONG_AI_ENGINE_FACE_FACE_STORE_H_
