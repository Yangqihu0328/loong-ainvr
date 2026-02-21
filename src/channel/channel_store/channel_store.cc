// Copyright 2026 Loong AI NVR Project

#include "channel/channel_store/channel_store.h"

#include "spdlog/spdlog.h"

#include "nlohmann/json.hpp"

namespace loong::channel {

ChannelStore::ChannelStore() = default;

ChannelStore::~ChannelStore() { Close(); }

bool ChannelStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ != nullptr) return true;

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("ChannelStore: failed to open '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  if (!CreateTables()) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  spdlog::info("ChannelStore: opened '{}'", db_path);
  return true;
}

void ChannelStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool ChannelStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS channels (
      id              INTEGER PRIMARY KEY,
      name            TEXT    NOT NULL DEFAULT '',
      rtsp_url        TEXT    NOT NULL DEFAULT '',
      codec           TEXT    NOT NULL DEFAULT 'h264',
      width           INTEGER NOT NULL DEFAULT 1920,
      height          INTEGER NOT NULL DEFAULT 1080,
      framerate       INTEGER NOT NULL DEFAULT 30,
      bitrate_kbps    INTEGER NOT NULL DEFAULT 4000,
      ai_model_name   TEXT    NOT NULL DEFAULT '',
      ai_backend      TEXT    NOT NULL DEFAULT '',
      confidence      REAL    NOT NULL DEFAULT 0.5,
      analysis_fps    INTEGER NOT NULL DEFAULT 5,
      overlay_json    TEXT    NOT NULL DEFAULT '{}',
      record_enabled  INTEGER NOT NULL DEFAULT 1,
      record_path     TEXT    NOT NULL DEFAULT '',
      latitude        REAL    NOT NULL DEFAULT 0.0,
      longitude       REAL    NOT NULL DEFAULT 0.0,
      enabled         INTEGER NOT NULL DEFAULT 1
    );
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    spdlog::error("ChannelStore: CreateTables failed: {}", err_msg);
    sqlite3_free(err_msg);
    return false;
  }
  return true;
}

bool ChannelStore::Save(const ChannelConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return false;

  const char* sql = R"(
    INSERT OR REPLACE INTO channels
      (id, name, rtsp_url, codec, width, height, framerate, bitrate_kbps,
       ai_model_name, ai_backend, confidence, analysis_fps,
       overlay_json, record_enabled, record_path, latitude, longitude, enabled)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("ChannelStore: Save prepare failed: {}", sqlite3_errmsg(db_));
    return false;
  }

  const char* codec_str = (config.codec == CodecType::kH265) ? "h265" : "h264";
  auto overlay_json = SerializeOverlay(config.overlay);

  sqlite3_bind_int(stmt, 1, config.id);
  sqlite3_bind_text(stmt, 2, config.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, config.rtsp_url.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, codec_str, -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 5, config.width);
  sqlite3_bind_int(stmt, 6, config.height);
  sqlite3_bind_int(stmt, 7, config.framerate);
  sqlite3_bind_int(stmt, 8, config.bitrate_kbps);
  sqlite3_bind_text(stmt, 9, config.ai_model_name.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, config.ai_backend.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 11,
                      static_cast<double>(config.confidence_threshold));
  sqlite3_bind_int(stmt, 12, config.analysis_fps);
  sqlite3_bind_text(stmt, 13, overlay_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 14, config.record_enabled ? 1 : 0);
  sqlite3_bind_text(stmt, 15, config.record_path.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 16, config.latitude);
  sqlite3_bind_double(stmt, 17, config.longitude);
  sqlite3_bind_int(stmt, 18, 1);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    spdlog::error("ChannelStore: Save failed for ch {}: {}", config.id,
                  sqlite3_errmsg(db_));
    return false;
  }

  spdlog::debug("ChannelStore: saved ch {} '{}'", config.id, config.name);
  return true;
}

bool ChannelStore::Delete(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return false;

  const char* sql = "DELETE FROM channels WHERE id=?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    spdlog::debug("ChannelStore: deleted ch {}", channel_id);
    return true;
  }
  return false;
}

std::vector<ChannelConfig> ChannelStore::LoadAll() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelConfig> result;
  if (db_ == nullptr) return result;

  const char* sql = R"(
    SELECT id, name, rtsp_url, codec, width, height, framerate, bitrate_kbps,
           ai_model_name, ai_backend, confidence, analysis_fps,
           overlay_json, record_enabled, record_path, latitude, longitude
    FROM channels WHERE enabled=1 ORDER BY id
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    ChannelConfig cfg;
    cfg.id = sqlite3_column_int(stmt, 0);

    auto text = [&](int col) -> std::string {
      auto p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
      return p ? p : "";
    };

    cfg.name = text(1);
    cfg.rtsp_url = text(2);

    std::string codec_str = text(3);
    cfg.codec = (codec_str == "h265" || codec_str == "hevc") ? CodecType::kH265
                                                             : CodecType::kH264;

    cfg.width = sqlite3_column_int(stmt, 4);
    cfg.height = sqlite3_column_int(stmt, 5);
    cfg.framerate = sqlite3_column_int(stmt, 6);
    cfg.bitrate_kbps = sqlite3_column_int(stmt, 7);
    cfg.ai_model_name = text(8);
    cfg.ai_backend = text(9);
    cfg.confidence_threshold =
        static_cast<float>(sqlite3_column_double(stmt, 10));
    cfg.analysis_fps = sqlite3_column_int(stmt, 11);
    cfg.overlay = DeserializeOverlay(text(12));
    cfg.record_enabled = sqlite3_column_int(stmt, 13) != 0;
    cfg.record_path = text(14);
    cfg.latitude = sqlite3_column_double(stmt, 15);
    cfg.longitude = sqlite3_column_double(stmt, 16);

    result.push_back(std::move(cfg));
  }

  sqlite3_finalize(stmt);
  spdlog::info("ChannelStore: loaded {} channels", result.size());
  return result;
}

int ChannelStore::MaxChannelId() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return 0;

  const char* sql = "SELECT COALESCE(MAX(id), 0) FROM channels";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  int max_id = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    max_id = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return max_id;
}

std::string ChannelStore::SerializeOverlay(const OverlayConfig& o) {
  nlohmann::json j;
  j["enabled"] = o.enabled;
  j["line_thickness"] = o.line_thickness;
  j["font_scale"] = o.font_scale;
  j["show_labels"] = o.show_labels;
  j["show_confidence"] = o.show_confidence;
  j["show_timestamp"] = o.show_timestamp;
  j["show_channel_name"] = o.show_channel_name;
  j["show_trajectory"] = o.show_trajectory;
  j["trajectory_max_points"] = o.trajectory_max_points;
  j["timestamp_position"] = o.timestamp_position;
  j["fill_opacity"] = o.fill_opacity;
  j["timestamp_format"] = o.timestamp_format;
  return j.dump();
}

OverlayConfig ChannelStore::DeserializeOverlay(const std::string& json) {
  OverlayConfig o;
  try {
    auto j = nlohmann::json::parse(json);
    o.enabled = j.value("enabled", true);
    o.line_thickness = j.value("line_thickness", 2);
    o.font_scale = j.value("font_scale", 0.5);
    o.show_labels = j.value("show_labels", true);
    o.show_confidence = j.value("show_confidence", true);
    o.show_timestamp = j.value("show_timestamp", true);
    o.show_channel_name = j.value("show_channel_name", true);
    o.show_trajectory = j.value("show_trajectory", false);
    o.trajectory_max_points = j.value("trajectory_max_points", 30);
    o.timestamp_position = j.value("timestamp_position", 0);
    o.fill_opacity = j.value("fill_opacity", 0.08);
    o.timestamp_format = j.value("timestamp_format", "");
  } catch (...) {
  }
  return o;
}

}  // namespace loong::channel
