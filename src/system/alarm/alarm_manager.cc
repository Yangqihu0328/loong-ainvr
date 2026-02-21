// Copyright 2026 Loong AI NVR Project

#include "system/alarm/alarm_manager.h"

#include <chrono>

#include <nlohmann/json.hpp>

#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

namespace loong::system {

AlarmManager::AlarmManager() = default;

AlarmManager::~AlarmManager() {
  Stop();
  Close();
}

bool AlarmManager::Open(const std::string& db_path) {
  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("AlarmManager: failed to open db '{}': {}", db_path,
                   sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "PRAGMA synchronous=NORMAL;", nullptr, nullptr, nullptr);

  if (!CreateTables()) {
    Close();
    return false;
  }

  spdlog::info("AlarmManager: opened '{}'", db_path);
  return true;
}

void AlarmManager::Close() {
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void AlarmManager::Start() {
  if (running_) return;
  running_ = true;

  auto& bus = core::EventBus::Instance();
  event_sub_id_ = bus.Subscribe("ai.detection", [this](const std::any& data) {
    if (!running_) return;
    try {
      auto json_str = std::any_cast<std::string>(data);
      auto j = nlohmann::json::parse(json_str);
      int channel_id = j.value("channel_id", -1);
      std::string event_type = j.value("event_type", "object_detected");
      float confidence = j.value("max_confidence", 0.0F);
      std::string metadata = j.dump();
      ProcessDetection(channel_id, event_type, confidence, metadata);
    } catch (const std::exception& e) {
      spdlog::debug("AlarmManager: failed to parse ai.detection event: {}",
                     e.what());
    }
  });

  spdlog::info("AlarmManager: started, listening for ai.detection events");
}

void AlarmManager::Stop() {
  if (!running_) return;
  running_ = false;

  if (event_sub_id_ != 0) {
    core::EventBus::Instance().Unsubscribe(event_sub_id_);
    event_sub_id_ = 0;
  }

  spdlog::info("AlarmManager: stopped");
}

// ============================================================
// Rule Management
// ============================================================

int64_t AlarmManager::AddRule(const AlarmRule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "INSERT INTO alarm_rules "
      "(channel_id, event_type, min_confidence, severity, enabled, "
      "name, description, cooldown_sec) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    spdlog::error("AlarmManager: prepare AddRule failed");
    return -1;
  }

  sqlite3_bind_int(stmt, 1, rule.channel_id);
  sqlite3_bind_text(stmt, 2, rule.event_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 3, static_cast<double>(rule.min_confidence));
  sqlite3_bind_int(stmt, 4, static_cast<int>(rule.severity));
  sqlite3_bind_int(stmt, 5, rule.enabled ? 1 : 0);
  sqlite3_bind_text(stmt, 6, rule.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, rule.description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, rule.cooldown_sec);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    spdlog::error("AlarmManager: AddRule failed");
    return -1;
  }

  int64_t id = sqlite3_last_insert_rowid(db_);
  spdlog::info("AlarmManager: added rule '{}' (id={})", rule.name, id);
  return id;
}

bool AlarmManager::UpdateRule(const AlarmRule& rule) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "UPDATE alarm_rules SET channel_id=?, event_type=?, min_confidence=?, "
      "severity=?, enabled=?, name=?, description=?, cooldown_sec=? "
      "WHERE id=?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int(stmt, 1, rule.channel_id);
  sqlite3_bind_text(stmt, 2, rule.event_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 3, static_cast<double>(rule.min_confidence));
  sqlite3_bind_int(stmt, 4, static_cast<int>(rule.severity));
  sqlite3_bind_int(stmt, 5, rule.enabled ? 1 : 0);
  sqlite3_bind_text(stmt, 6, rule.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, rule.description.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 8, rule.cooldown_sec);
  sqlite3_bind_int64(stmt, 9, rule.id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE;
}

bool AlarmManager::DeleteRule(int64_t rule_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql = "DELETE FROM alarm_rules WHERE id=?;";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, rule_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE) {
    last_trigger_times_.erase(rule_id);
    spdlog::info("AlarmManager: deleted rule {}", rule_id);
    return true;
  }
  return false;
}

AlarmRule AlarmManager::GetRule(int64_t rule_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT id, channel_id, event_type, min_confidence, severity, enabled, "
      "name, description, cooldown_sec FROM alarm_rules WHERE id=?;";

  sqlite3_stmt* stmt = nullptr;
  AlarmRule rule;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return rule;
  }

  sqlite3_bind_int64(stmt, 1, rule_id);
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.channel_id = sqlite3_column_int(stmt, 1);
    const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    rule.event_type = et ? et : "";
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    rule.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 4));
    rule.enabled = sqlite3_column_int(stmt, 5) != 0;
    const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    rule.name = nm ? nm : "";
    const auto* ds = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    rule.description = ds ? ds : "";
    rule.cooldown_sec = sqlite3_column_int(stmt, 8);
  }
  sqlite3_finalize(stmt);
  return rule;
}

std::vector<AlarmRule> AlarmManager::ListRules() {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT id, channel_id, event_type, min_confidence, severity, enabled, "
      "name, description, cooldown_sec FROM alarm_rules ORDER BY id;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<AlarmRule> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AlarmRule rule;
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.channel_id = sqlite3_column_int(stmt, 1);
    const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    rule.event_type = et ? et : "";
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    rule.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 4));
    rule.enabled = sqlite3_column_int(stmt, 5) != 0;
    const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    rule.name = nm ? nm : "";
    const auto* ds = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    rule.description = ds ? ds : "";
    rule.cooldown_sec = sqlite3_column_int(stmt, 8);
    results.push_back(rule);
  }

  sqlite3_finalize(stmt);
  return results;
}

std::vector<AlarmRule> AlarmManager::ListRulesForChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT id, channel_id, event_type, min_confidence, severity, enabled, "
      "name, description, cooldown_sec FROM alarm_rules "
      "WHERE channel_id=? OR channel_id=-1 ORDER BY id;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<AlarmRule> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, channel_id);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AlarmRule rule;
    rule.id = sqlite3_column_int64(stmt, 0);
    rule.channel_id = sqlite3_column_int(stmt, 1);
    const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    rule.event_type = et ? et : "";
    rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
    rule.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 4));
    rule.enabled = sqlite3_column_int(stmt, 5) != 0;
    const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
    rule.name = nm ? nm : "";
    const auto* ds = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    rule.description = ds ? ds : "";
    rule.cooldown_sec = sqlite3_column_int(stmt, 8);
    results.push_back(rule);
  }

  sqlite3_finalize(stmt);
  return results;
}

// ============================================================
// Alarm History
// ============================================================

std::vector<AlarmRecord> AlarmManager::QueryAlarms(int64_t start_time,
                                                    int64_t end_time,
                                                    int limit) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT id, rule_id, channel_id, event_type, confidence, severity, "
      "status, triggered_at, acknowledged_at, acknowledged_by, metadata "
      "FROM alarm_history "
      "WHERE triggered_at>=? AND triggered_at<=? "
      "ORDER BY triggered_at DESC LIMIT ?;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<AlarmRecord> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int64(stmt, 1, start_time);
  sqlite3_bind_int64(stmt, 2, end_time);
  sqlite3_bind_int(stmt, 3, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AlarmRecord rec;
    rec.id = sqlite3_column_int64(stmt, 0);
    rec.rule_id = sqlite3_column_int64(stmt, 1);
    rec.channel_id = sqlite3_column_int(stmt, 2);
    const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    rec.event_type = et ? et : "";
    rec.confidence = static_cast<float>(sqlite3_column_double(stmt, 4));
    rec.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 5));
    rec.status = static_cast<AlarmStatus>(sqlite3_column_int(stmt, 6));
    rec.triggered_at = sqlite3_column_int64(stmt, 7);
    rec.acknowledged_at = sqlite3_column_int64(stmt, 8);
    const auto* ab = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    rec.acknowledged_by = ab ? ab : "";
    const auto* md = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    rec.metadata = md ? md : "";
    results.push_back(rec);
  }

  sqlite3_finalize(stmt);
  return results;
}

std::vector<AlarmRecord> AlarmManager::QueryAlarmsByChannel(
    int channel_id, int64_t start_time, int64_t end_time, int limit) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT id, rule_id, channel_id, event_type, confidence, severity, "
      "status, triggered_at, acknowledged_at, acknowledged_by, metadata "
      "FROM alarm_history "
      "WHERE channel_id=? AND triggered_at>=? AND triggered_at<=? "
      "ORDER BY triggered_at DESC LIMIT ?;";

  sqlite3_stmt* stmt = nullptr;
  std::vector<AlarmRecord> results;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return results;
  }

  sqlite3_bind_int(stmt, 1, channel_id);
  sqlite3_bind_int64(stmt, 2, start_time);
  sqlite3_bind_int64(stmt, 3, end_time);
  sqlite3_bind_int(stmt, 4, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    AlarmRecord rec;
    rec.id = sqlite3_column_int64(stmt, 0);
    rec.rule_id = sqlite3_column_int64(stmt, 1);
    rec.channel_id = sqlite3_column_int(stmt, 2);
    const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    rec.event_type = et ? et : "";
    rec.confidence = static_cast<float>(sqlite3_column_double(stmt, 4));
    rec.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 5));
    rec.status = static_cast<AlarmStatus>(sqlite3_column_int(stmt, 6));
    rec.triggered_at = sqlite3_column_int64(stmt, 7);
    rec.acknowledged_at = sqlite3_column_int64(stmt, 8);
    const auto* ab = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
    rec.acknowledged_by = ab ? ab : "";
    const auto* md = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    rec.metadata = md ? md : "";
    results.push_back(rec);
  }

  sqlite3_finalize(stmt);
  return results;
}

int AlarmManager::GetActiveAlarmCount() {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "SELECT COUNT(*) FROM alarm_history WHERE status=0;";

  sqlite3_stmt* stmt = nullptr;
  int count = 0;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }
  return count;
}

bool AlarmManager::AcknowledgeAlarm(int64_t alarm_id,
                                     const std::string& user) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "UPDATE alarm_history SET status=1, acknowledged_at=?, acknowledged_by=? "
      "WHERE id=? AND status=0;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, NowMs());
  sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, alarm_id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc == SQLITE_DONE && sqlite3_changes(db_) > 0) {
    spdlog::info("AlarmManager: alarm {} acknowledged by {}", alarm_id, user);
    return true;
  }
  return false;
}

bool AlarmManager::DismissAlarm(int64_t alarm_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "UPDATE alarm_history SET status=2 WHERE id=?;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return false;
  }

  sqlite3_bind_int64(stmt, 1, alarm_id);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

int AlarmManager::AcknowledgeAll(const std::string& user) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "UPDATE alarm_history SET status=1, acknowledged_at=?, acknowledged_by=? "
      "WHERE status=0;";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return 0;
  }

  sqlite3_bind_int64(stmt, 1, NowMs());
  sqlite3_bind_text(stmt, 2, user.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  int count = (rc == SQLITE_DONE) ? sqlite3_changes(db_) : 0;
  if (count > 0) {
    spdlog::info("AlarmManager: acknowledged {} alarms by {}", count, user);
  }
  return count;
}

// ============================================================
// Event Processing
// ============================================================

void AlarmManager::ProcessDetection(int channel_id,
                                     const std::string& event_type,
                                     float confidence,
                                     const std::string& metadata) {
  // Fetch matching rules (needs lock for DB access but release before publish)
  std::vector<AlarmRule> matching_rules;
  {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql =
        "SELECT id, channel_id, event_type, min_confidence, severity, enabled, "
        "name, description, cooldown_sec FROM alarm_rules "
        "WHERE enabled=1 AND (channel_id=? OR channel_id=-1) "
        "AND (event_type='' OR event_type=?) "
        "AND min_confidence<=?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
      return;
    }

    sqlite3_bind_int(stmt, 1, channel_id);
    sqlite3_bind_text(stmt, 2, event_type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, static_cast<double>(confidence));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
      AlarmRule rule;
      rule.id = sqlite3_column_int64(stmt, 0);
      rule.channel_id = sqlite3_column_int(stmt, 1);
      const auto* et = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      rule.event_type = et ? et : "";
      rule.min_confidence = static_cast<float>(sqlite3_column_double(stmt, 3));
      rule.severity = static_cast<AlarmSeverity>(sqlite3_column_int(stmt, 4));
      rule.enabled = sqlite3_column_int(stmt, 5) != 0;
      const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
      rule.name = nm ? nm : "";
      const auto* ds = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
      rule.description = ds ? ds : "";
      rule.cooldown_sec = sqlite3_column_int(stmt, 8);
      matching_rules.push_back(rule);
    }
    sqlite3_finalize(stmt);
  }

  // Process each matching rule
  for (const auto& rule : matching_rules) {
    if (!CheckCooldown(rule.id)) {
      continue;
    }

    // Create alarm record
    AlarmRecord record;
    record.rule_id = rule.id;
    record.channel_id = channel_id;
    record.event_type = event_type;
    record.confidence = confidence;
    record.severity = rule.severity;
    record.status = AlarmStatus::kActive;
    record.triggered_at = NowMs();
    record.metadata = metadata;

    int64_t alarm_id = InsertAlarm(record);
    if (alarm_id < 0) continue;

    record.id = alarm_id;

    // Update cooldown
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_trigger_times_[rule.id] = record.triggered_at;
    }

    spdlog::info(
        "AlarmManager: ALARM triggered — rule='{}' ch{} type='{}' "
        "conf={:.2f} severity={}",
        rule.name, channel_id, event_type, confidence,
        AlarmSeverityToString(rule.severity));

    // Publish to EventBus
    core::EventBus::Instance().Publish("alarm.triggered",
                                        std::any(record));

    // Invoke callback if registered
    if (alarm_callback_) {
      alarm_callback_(record);
    }
  }
}

void AlarmManager::SetAlarmCallback(AlarmCallback callback) {
  alarm_callback_ = std::move(callback);
}

// ============================================================
// Private Helpers
// ============================================================

bool AlarmManager::CreateTables() {
  const char* schema = R"(
    CREATE TABLE IF NOT EXISTS alarm_rules (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      channel_id INTEGER DEFAULT -1,
      event_type TEXT DEFAULT '',
      min_confidence REAL DEFAULT 0.5,
      severity INTEGER DEFAULT 1,
      enabled INTEGER DEFAULT 1,
      name TEXT NOT NULL,
      description TEXT DEFAULT '',
      cooldown_sec INTEGER DEFAULT 30
    );

    CREATE TABLE IF NOT EXISTS alarm_history (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      rule_id INTEGER REFERENCES alarm_rules(id),
      channel_id INTEGER NOT NULL,
      event_type TEXT NOT NULL,
      confidence REAL,
      severity INTEGER DEFAULT 1,
      status INTEGER DEFAULT 0,
      triggered_at INTEGER NOT NULL,
      acknowledged_at INTEGER DEFAULT 0,
      acknowledged_by TEXT DEFAULT '',
      metadata TEXT DEFAULT ''
    );

    CREATE INDEX IF NOT EXISTS idx_alarm_time
      ON alarm_history(triggered_at);
    CREATE INDEX IF NOT EXISTS idx_alarm_channel_time
      ON alarm_history(channel_id, triggered_at);
    CREATE INDEX IF NOT EXISTS idx_alarm_status
      ON alarm_history(status);
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    spdlog::error("AlarmManager: create tables failed: {}",
                   err_msg ? err_msg : "unknown");
    sqlite3_free(err_msg);
    return false;
  }
  return true;
}

int64_t AlarmManager::InsertAlarm(const AlarmRecord& record) {
  std::lock_guard<std::mutex> lock(mutex_);

  const char* sql =
      "INSERT INTO alarm_history "
      "(rule_id, channel_id, event_type, confidence, severity, status, "
      "triggered_at, metadata) "
      "VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_int64(stmt, 1, record.rule_id);
  sqlite3_bind_int(stmt, 2, record.channel_id);
  sqlite3_bind_text(stmt, 3, record.event_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 4, static_cast<double>(record.confidence));
  sqlite3_bind_int(stmt, 5, static_cast<int>(record.severity));
  sqlite3_bind_int(stmt, 6, static_cast<int>(record.status));
  sqlite3_bind_int64(stmt, 7, record.triggered_at);
  sqlite3_bind_text(stmt, 8, record.metadata.c_str(), -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return (rc == SQLITE_DONE) ? sqlite3_last_insert_rowid(db_) : -1;
}

bool AlarmManager::CheckCooldown(int64_t rule_id) {
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = last_trigger_times_.find(rule_id);
  if (it == last_trigger_times_.end()) return true;

  // Look up cooldown from the rules (we need to query, but for performance
  // we use a default if the rule's cooldown_sec isn't cached).
  // For simplicity, query the rule's cooldown.
  const char* sql = "SELECT cooldown_sec FROM alarm_rules WHERE id=?;";
  sqlite3_stmt* stmt = nullptr;
  int cooldown_sec = 30;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
    sqlite3_bind_int64(stmt, 1, rule_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      cooldown_sec = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
  }

  int64_t elapsed_ms = NowMs() - it->second;
  return elapsed_ms >= static_cast<int64_t>(cooldown_sec) * 1000;
}

int64_t AlarmManager::NowMs() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace loong::system
