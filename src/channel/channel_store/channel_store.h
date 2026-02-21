// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CHANNEL_CHANNEL_STORE_CHANNEL_STORE_H_
#define LOONG_CHANNEL_CHANNEL_STORE_CHANNEL_STORE_H_

#include <mutex>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "core/common/types.h"

namespace loong::channel {

/// SQLite-based persistent storage for channel configurations.
/// Thread-safe — all public methods are mutex-protected.
class ChannelStore {
 public:
  ChannelStore();
  ~ChannelStore();

  /// Open or create the channels database at the given path.
  bool Open(const std::string& db_path);

  /// Close the database.
  void Close();

  /// Save a channel configuration (insert or replace).
  bool Save(const ChannelConfig& config);

  /// Delete a channel by ID.
  bool Delete(int channel_id);

  /// Load all channel configurations.
  std::vector<ChannelConfig> LoadAll();

  /// Get the highest channel ID currently stored (for ID generation).
  int MaxChannelId();

  // Non-copyable
  ChannelStore(const ChannelStore&) = delete;
  ChannelStore& operator=(const ChannelStore&) = delete;

 private:
  bool CreateTables();
  static std::string SerializeOverlay(const OverlayConfig& overlay);
  static OverlayConfig DeserializeOverlay(const std::string& json);

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::channel

#endif  // LOONG_CHANNEL_CHANNEL_STORE_CHANNEL_STORE_H_
