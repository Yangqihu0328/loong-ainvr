// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_ALARM_ALARM_MANAGER_H_
#define LOONG_SYSTEM_ALARM_ALARM_MANAGER_H_

#include "core/common/types.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace loong::system {

/// Severity level for alarms.
enum class AlarmSeverity {
  kInfo = 0,
  kWarning = 1,
  kCritical = 2,
};

/// Convert AlarmSeverity to string.
inline const char* AlarmSeverityToString(AlarmSeverity s) {
  switch (s) {
    case AlarmSeverity::kInfo:
      return "info";
    case AlarmSeverity::kWarning:
      return "warning";
    case AlarmSeverity::kCritical:
      return "critical";
  }
  return "unknown";
}

/// Status of an alarm record.
enum class AlarmStatus {
  kActive = 0,
  kAcknowledged = 1,
  kDismissed = 2,
};

/// A rule that triggers an alarm based on AI detection events.
struct AlarmRule {
  int64_t id = 0;
  int channel_id = -1;  // -1 means all channels
  std::string
      event_type;  // e.g. "person_detected", "vehicle_detected", "" = any
  float min_confidence = 0.5F;
  AlarmSeverity severity = AlarmSeverity::kWarning;
  bool enabled = true;
  std::string name;  // Human-readable rule name
  std::string description;
  int cooldown_sec = 30;  // Minimum seconds between repeated alarms
};

/// A recorded alarm instance.
struct AlarmRecord {
  int64_t id = 0;
  int64_t rule_id = 0;
  int channel_id = 0;
  std::string event_type;
  float confidence = 0.0F;
  AlarmSeverity severity = AlarmSeverity::kWarning;
  AlarmStatus status = AlarmStatus::kActive;
  int64_t triggered_at = 0;  // Unix timestamp (ms)
  int64_t acknowledged_at = 0;
  std::string acknowledged_by;
  std::string metadata;  // JSON
};

/// Callback invoked when an alarm is triggered.
using AlarmCallback = std::function<void(const AlarmRecord& alarm)>;

/// Alarm manager for AI event-based alarm processing.
///
/// Listens to AI detection events via EventBus, evaluates alarm rules,
/// stores alarm history in SQLite, and publishes "alarm.triggered" events
/// for WebSocket notification.
class AlarmManager {
 public:
  AlarmManager();
  ~AlarmManager();

  /// Open the alarm database.
  bool Open(const std::string& db_path);

  /// Close the database.
  void Close();

  /// Start listening to EventBus for AI detection events.
  void Start();

  /// Stop listening.
  void Stop();

  // ---- Rule management ----

  /// Add a new alarm rule. Returns rule ID.
  int64_t AddRule(const AlarmRule& rule);

  /// Update an existing alarm rule.
  bool UpdateRule(const AlarmRule& rule);

  /// Delete an alarm rule.
  bool DeleteRule(int64_t rule_id);

  /// Get a rule by ID.
  AlarmRule GetRule(int64_t rule_id);

  /// List all alarm rules.
  std::vector<AlarmRule> ListRules();

  /// List rules for a specific channel.
  std::vector<AlarmRule> ListRulesForChannel(int channel_id);

  // ---- Alarm history ----

  /// Query alarm records by time range.
  std::vector<AlarmRecord> QueryAlarms(int64_t start_time, int64_t end_time,
                                       int limit = 100);

  /// Query alarm records by channel and time range.
  std::vector<AlarmRecord> QueryAlarmsByChannel(int channel_id,
                                                int64_t start_time,
                                                int64_t end_time,
                                                int limit = 100);

  /// Get count of active (unacknowledged) alarms.
  int GetActiveAlarmCount();

  /// Acknowledge an alarm.
  bool AcknowledgeAlarm(int64_t alarm_id, const std::string& user);

  /// Dismiss an alarm.
  bool DismissAlarm(int64_t alarm_id);

  /// Acknowledge all active alarms.
  int AcknowledgeAll(const std::string& user);

  // ---- Event processing ----

  /// Manually evaluate a detection against alarm rules.
  /// Used by the pipeline or external callers.
  void ProcessDetection(int channel_id, const std::string& event_type,
                        float confidence, const std::string& metadata = "");

  /// Register a callback for alarm events (in addition to EventBus).
  void SetAlarmCallback(AlarmCallback callback);

  // Non-copyable
  AlarmManager(const AlarmManager&) = delete;
  AlarmManager& operator=(const AlarmManager&) = delete;

 private:
  bool CreateTables();
  int64_t InsertAlarm(const AlarmRecord& record);
  bool CheckCooldown(int64_t rule_id);
  int64_t NowMs() const;

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
  std::atomic<bool> running_{false};
  uint64_t event_sub_id_ = 0;

  // Cooldown tracking: rule_id -> last trigger time (ms)
  std::unordered_map<int64_t, int64_t> last_trigger_times_;

  AlarmCallback alarm_callback_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_ALARM_ALARM_MANAGER_H_
