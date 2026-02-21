// Copyright 2026 Loong AI NVR Project

#include "rules/rule_store/rule_store.h"

#include "spdlog/spdlog.h"

#include <chrono>

#include "nlohmann/json.hpp"

namespace loong::rules {

RuleStore::RuleStore() = default;

RuleStore::~RuleStore() { Close(); }

bool RuleStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ != nullptr) return true;

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("RuleStore: failed to open '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // Enable WAL mode for concurrent reads
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  if (!CreateTables()) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  spdlog::info("RuleStore: opened '{}'", db_path);
  return true;
}

void RuleStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool RuleStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS rules (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      name            TEXT    NOT NULL,
      type            TEXT    NOT NULL,
      channel_id      INTEGER NOT NULL,
      enabled         INTEGER NOT NULL DEFAULT 1,
      min_confidence  REAL    NOT NULL DEFAULT 0.5,
      loiter_time_sec INTEGER NOT NULL DEFAULT 30,
      cooldown_sec    INTEGER NOT NULL DEFAULT 60,
      params_json     TEXT    NOT NULL DEFAULT '{}',
      created_at      INTEGER NOT NULL,
      updated_at      INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_rules_channel ON rules(channel_id);
    CREATE INDEX IF NOT EXISTS idx_rules_enabled ON rules(channel_id, enabled);

    CREATE TABLE IF NOT EXISTS rule_events (
      id              INTEGER PRIMARY KEY AUTOINCREMENT,
      rule_id         INTEGER NOT NULL,
      rule_name       TEXT    NOT NULL,
      rule_type       TEXT    NOT NULL,
      channel_id      INTEGER NOT NULL,
      timestamp_ms    INTEGER NOT NULL,
      severity        TEXT    NOT NULL DEFAULT 'alarm',
      trigger_json    TEXT    NOT NULL DEFAULT '{}',
      count_value     INTEGER NOT NULL DEFAULT 0,
      dwell_time_sec  INTEGER NOT NULL DEFAULT 0,
      direction       TEXT    NOT NULL DEFAULT '',
      metadata_json   TEXT    NOT NULL DEFAULT '{}'
    );

    CREATE INDEX IF NOT EXISTS idx_rule_events_channel_time
        ON rule_events(channel_id, timestamp_ms);
    CREATE INDEX IF NOT EXISTS idx_rule_events_rule
        ON rule_events(rule_id, timestamp_ms);
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    spdlog::error("RuleStore: CreateTables failed: {}", err_msg);
    sqlite3_free(err_msg);
    return false;
  }
  return true;
}

int64_t RuleStore::CreateRule(const AnalysisRule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return -1;

  const char* sql = R"(
    INSERT INTO rules (name, type, channel_id, enabled, min_confidence,
                       loiter_time_sec, cooldown_sec, params_json,
                       created_at, updated_at)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("RuleStore: CreateRule prepare failed: {}",
                  sqlite3_errmsg(db_));
    return -1;
  }

  auto now = Now();
  auto params_json = SerializeParams(rule);

  sqlite3_bind_text(stmt, 1, rule.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, RuleTypeToString(rule.type), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, rule.channel_id);
  sqlite3_bind_int(stmt, 4, rule.enabled ? 1 : 0);
  sqlite3_bind_double(stmt, 5, static_cast<double>(rule.min_confidence));
  sqlite3_bind_int(stmt, 6, rule.loiter_time_sec);
  sqlite3_bind_int(stmt, 7, rule.cooldown_sec);
  sqlite3_bind_text(stmt, 8, params_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 9, now);
  sqlite3_bind_int64(stmt, 10, now);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    spdlog::error("RuleStore: CreateRule step failed: {}", sqlite3_errmsg(db_));
    return -1;
  }

  int64_t new_id = sqlite3_last_insert_rowid(db_);
  spdlog::info("RuleStore: created rule id={} name='{}' type={} ch={}", new_id,
               rule.name, RuleTypeToString(rule.type), rule.channel_id);
  return new_id;
}

bool RuleStore::UpdateRule(const AnalysisRule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return false;

  const char* sql = R"(
    UPDATE rules SET name=?, type=?, channel_id=?, enabled=?,
           min_confidence=?, loiter_time_sec=?, cooldown_sec=?,
           params_json=?, updated_at=?
    WHERE id=?
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  auto params_json = SerializeParams(rule);

  sqlite3_bind_text(stmt, 1, rule.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, RuleTypeToString(rule.type), -1, SQLITE_STATIC);
  sqlite3_bind_int(stmt, 3, rule.channel_id);
  sqlite3_bind_int(stmt, 4, rule.enabled ? 1 : 0);
  sqlite3_bind_double(stmt, 5, static_cast<double>(rule.min_confidence));
  sqlite3_bind_int(stmt, 6, rule.loiter_time_sec);
  sqlite3_bind_int(stmt, 7, rule.cooldown_sec);
  sqlite3_bind_text(stmt, 8, params_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 9, Now());
  sqlite3_bind_int64(stmt, 10, rule.id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

bool RuleStore::DeleteRule(int64_t rule_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return false;

  const char* sql = "DELETE FROM rules WHERE id=?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, rule_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    spdlog::info("RuleStore: deleted rule id={}", rule_id);
    return true;
  }
  return false;
}

bool RuleStore::GetRule(int64_t rule_id, AnalysisRule& out) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return false;

  const char* sql = R"(
    SELECT id, name, type, channel_id, enabled, min_confidence,
           loiter_time_sec, cooldown_sec, params_json, created_at, updated_at
    FROM rules WHERE id=?
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, rule_id);

  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    out.id = sqlite3_column_int64(stmt, 0);
    out.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    out.type = ParseRuleType(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    out.channel_id = sqlite3_column_int(stmt, 3);
    out.enabled = sqlite3_column_int(stmt, 4) != 0;
    out.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    out.loiter_time_sec = sqlite3_column_int(stmt, 6);
    out.cooldown_sec = sqlite3_column_int(stmt, 7);

    auto params_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    if (params_str != nullptr) {
      DeserializeParams(params_str, out);
    }

    out.created_at = sqlite3_column_int64(stmt, 9);
    out.updated_at = sqlite3_column_int64(stmt, 10);
    found = true;
  }

  sqlite3_finalize(stmt);
  return found;
}

std::vector<AnalysisRule> RuleStore::ListRules() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<AnalysisRule> result;
  if (db_ == nullptr) return result;

  const char* sql = R"(
    SELECT id, name, type, channel_id, enabled, min_confidence,
           loiter_time_sec, cooldown_sec, params_json, created_at, updated_at
    FROM rules ORDER BY id
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AnalysisRule rule;
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    rule.type = ParseRuleType(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    rule.channel_id = sqlite3_column_int(stmt, 3);
    rule.enabled = sqlite3_column_int(stmt, 4) != 0;
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    rule.loiter_time_sec = sqlite3_column_int(stmt, 6);
    rule.cooldown_sec = sqlite3_column_int(stmt, 7);

    auto params_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    if (params_str != nullptr) {
      DeserializeParams(params_str, rule);
    }

    rule.created_at = sqlite3_column_int64(stmt, 9);
    rule.updated_at = sqlite3_column_int64(stmt, 10);
    result.push_back(std::move(rule));
  }

  sqlite3_finalize(stmt);
  return result;
}

std::vector<AnalysisRule> RuleStore::ListRulesByChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<AnalysisRule> result;
  if (db_ == nullptr) return result;

  const char* sql = R"(
    SELECT id, name, type, channel_id, enabled, min_confidence,
           loiter_time_sec, cooldown_sec, params_json, created_at, updated_at
    FROM rules WHERE channel_id=? ORDER BY id
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }
  sqlite3_bind_int(stmt, 1, channel_id);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AnalysisRule rule;
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    rule.type = ParseRuleType(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    rule.channel_id = sqlite3_column_int(stmt, 3);
    rule.enabled = sqlite3_column_int(stmt, 4) != 0;
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    rule.loiter_time_sec = sqlite3_column_int(stmt, 6);
    rule.cooldown_sec = sqlite3_column_int(stmt, 7);

    auto params_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    if (params_str != nullptr) {
      DeserializeParams(params_str, rule);
    }

    rule.created_at = sqlite3_column_int64(stmt, 9);
    rule.updated_at = sqlite3_column_int64(stmt, 10);
    result.push_back(std::move(rule));
  }

  sqlite3_finalize(stmt);
  return result;
}

std::vector<AnalysisRule> RuleStore::ListEnabledRulesByChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<AnalysisRule> result;
  if (db_ == nullptr) return result;

  const char* sql = R"(
    SELECT id, name, type, channel_id, enabled, min_confidence,
           loiter_time_sec, cooldown_sec, params_json, created_at, updated_at
    FROM rules WHERE channel_id=? AND enabled=1 ORDER BY id
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }
  sqlite3_bind_int(stmt, 1, channel_id);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AnalysisRule rule;
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    rule.type = ParseRuleType(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    rule.channel_id = sqlite3_column_int(stmt, 3);
    rule.enabled = sqlite3_column_int(stmt, 4) != 0;
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 5));
    rule.loiter_time_sec = sqlite3_column_int(stmt, 6);
    rule.cooldown_sec = sqlite3_column_int(stmt, 7);

    auto params_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
    if (params_str != nullptr) {
      DeserializeParams(params_str, rule);
    }

    rule.created_at = sqlite3_column_int64(stmt, 9);
    rule.updated_at = sqlite3_column_int64(stmt, 10);
    result.push_back(std::move(rule));
  }

  sqlite3_finalize(stmt);
  return result;
}

int64_t RuleStore::LogEvent(const RuleEvent& event) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_ == nullptr) return -1;

  const char* sql = R"(
    INSERT INTO rule_events (rule_id, rule_name, rule_type, channel_id,
                             timestamp_ms, severity, trigger_json,
                             count_value, dwell_time_sec, direction,
                             metadata_json)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  nlohmann::json trigger_j = {
      {"x1", event.trigger_detection.x1},
      {"y1", event.trigger_detection.y1},
      {"x2", event.trigger_detection.x2},
      {"y2", event.trigger_detection.y2},
      {"confidence", event.trigger_detection.confidence},
      {"class_id", event.trigger_detection.class_id},
      {"class_name", event.trigger_detection.class_name},
  };
  auto trigger_str = trigger_j.dump();

  sqlite3_bind_int64(stmt, 1, event.rule_id);
  sqlite3_bind_text(stmt, 2, event.rule_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, RuleTypeToString(event.rule_type), -1,
                    SQLITE_STATIC);
  sqlite3_bind_int(stmt, 4, event.channel_id);
  sqlite3_bind_int64(stmt, 5, event.timestamp);
  sqlite3_bind_text(stmt, 6, SeverityToString(event.severity), -1,
                    SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, trigger_str.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, event.count_value);
  sqlite3_bind_int(stmt, 9, event.dwell_time_sec);
  sqlite3_bind_text(stmt, 10, event.direction.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11, event.metadata_json.c_str(), -1,
                    SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) return -1;
  return sqlite3_last_insert_rowid(db_);
}

std::vector<RuleEvent> RuleStore::QueryEvents(int channel_id, int64_t start_ms,
                                              int64_t end_ms, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<RuleEvent> result;
  if (db_ == nullptr) return result;

  const char* sql = R"(
    SELECT rule_id, rule_name, rule_type, channel_id, timestamp_ms,
           severity, trigger_json, count_value, dwell_time_sec,
           direction, metadata_json
    FROM rule_events
    WHERE channel_id=? AND timestamp_ms BETWEEN ? AND ?
    ORDER BY timestamp_ms DESC
    LIMIT ?
  )";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return result;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int64(stmt, 2, start_ms);
  sqlite3_bind_int64(stmt, 3, end_ms);
  sqlite3_bind_int(stmt, 4, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    RuleEvent ev;
    ev.rule_id = sqlite3_column_int64(stmt, 0);
    ev.rule_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    ev.rule_type = ParseRuleType(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    ev.channel_id = sqlite3_column_int(stmt, 3);
    ev.timestamp = sqlite3_column_int64(stmt, 4);

    auto sev_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
    if (std::string(sev_str) == "warning") {
      ev.severity = RuleEventSeverity::kWarning;
    } else if (std::string(sev_str) == "info") {
      ev.severity = RuleEventSeverity::kInfo;
    } else {
      ev.severity = RuleEventSeverity::kAlarm;
    }

    auto trigger_str =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    if (trigger_str != nullptr) {
      try {
        auto tj = nlohmann::json::parse(trigger_str);
        ev.trigger_detection.x1 = tj.value("x1", 0.0F);
        ev.trigger_detection.y1 = tj.value("y1", 0.0F);
        ev.trigger_detection.x2 = tj.value("x2", 0.0F);
        ev.trigger_detection.y2 = tj.value("y2", 0.0F);
        ev.trigger_detection.confidence = tj.value("confidence", 0.0F);
        ev.trigger_detection.class_id = tj.value("class_id", 0);
        ev.trigger_detection.class_name = tj.value("class_name", "");
      } catch (...) {
      }
    }

    ev.count_value = sqlite3_column_int(stmt, 7);
    ev.dwell_time_sec = sqlite3_column_int(stmt, 8);
    auto dir = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    if (dir != nullptr) ev.direction = dir;
    auto meta = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    if (meta != nullptr) ev.metadata_json = meta;

    result.push_back(std::move(ev));
  }

  sqlite3_finalize(stmt);
  return result;
}

// ── JSON serialization helpers ──

std::string RuleStore::SerializeParams(const AnalysisRule& rule) {
  nlohmann::json j;

  // Virtual line
  j["line"] = {
      {"start", {{"x", rule.line.start.x}, {"y", rule.line.start.y}}},
      {"end", {{"x", rule.line.end.x}, {"y", rule.line.end.y}}},
      {"bidirectional", rule.line.bidirectional},
  };

  // Region vertices
  nlohmann::json verts = nlohmann::json::array();
  for (const auto& v : rule.region.vertices) {
    verts.push_back({{"x", v.x}, {"y", v.y}});
  }
  j["region"] = {{"vertices", verts}};

  // Target classes
  j["target_classes"] = rule.target_classes;

  // Schedule
  j["schedule"] = {
      {"always_active", rule.schedule.always_active},
      {"start_time", rule.schedule.start_time},
      {"end_time", rule.schedule.end_time},
      {"weekdays", rule.schedule.weekdays},
  };

  return j.dump();
}

void RuleStore::DeserializeParams(const std::string& json, AnalysisRule& rule) {
  try {
    auto j = nlohmann::json::parse(json);

    if (j.contains("line")) {
      const auto& ln = j["line"];
      if (ln.contains("start")) {
        rule.line.start.x = ln["start"].value("x", 0.0);
        rule.line.start.y = ln["start"].value("y", 0.0);
      }
      if (ln.contains("end")) {
        rule.line.end.x = ln["end"].value("x", 0.0);
        rule.line.end.y = ln["end"].value("y", 0.0);
      }
      rule.line.bidirectional = ln.value("bidirectional", true);
    }

    if (j.contains("region") && j["region"].contains("vertices")) {
      rule.region.vertices.clear();
      for (const auto& v : j["region"]["vertices"]) {
        rule.region.vertices.push_back({v.value("x", 0.0), v.value("y", 0.0)});
      }
    }

    if (j.contains("target_classes")) {
      rule.target_classes = j["target_classes"].get<std::vector<std::string>>();
    }

    if (j.contains("schedule")) {
      const auto& s = j["schedule"];
      rule.schedule.always_active = s.value("always_active", true);
      rule.schedule.start_time = s.value("start_time", "");
      rule.schedule.end_time = s.value("end_time", "");
      if (s.contains("weekdays")) {
        rule.schedule.weekdays = s["weekdays"].get<std::vector<int>>();
      }
    }
  } catch (const std::exception& e) {
    spdlog::warn("RuleStore: DeserializeParams failed: {}", e.what());
  }
}

int64_t RuleStore::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace loong::rules
