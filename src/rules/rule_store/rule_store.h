// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_RULE_STORE_RULE_STORE_H_
#define LOONG_RULES_RULE_STORE_RULE_STORE_H_

#include "rules/rule_types.h"

#include <cstdint>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loong::rules {

/// SQLite-based persistent storage for analysis rules.
/// Thread-safe — all public methods are mutex-protected.
class RuleStore {
 public:
  RuleStore();
  ~RuleStore();

  /// Open or create the rules database at the given path.
  bool Open(const std::string& db_path);

  /// Close the database.
  void Close();

  /// Create a new rule. Returns the rule ID, or -1 on failure.
  int64_t CreateRule(const AnalysisRule& rule);

  /// Update an existing rule by ID.
  bool UpdateRule(const AnalysisRule& rule);

  /// Delete a rule by ID.
  bool DeleteRule(int64_t rule_id);

  /// Get a rule by ID. Returns false if not found.
  bool GetRule(int64_t rule_id, AnalysisRule& out);

  /// List all rules.
  std::vector<AnalysisRule> ListRules();

  /// List rules for a specific channel.
  std::vector<AnalysisRule> ListRulesByChannel(int channel_id);

  /// List enabled rules for a specific channel.
  std::vector<AnalysisRule> ListEnabledRulesByChannel(int channel_id);

  /// Insert a rule event into the event log. Returns event row ID.
  int64_t LogEvent(const RuleEvent& event);

  /// Query rule events by channel and time range.
  std::vector<RuleEvent> QueryEvents(int channel_id, int64_t start_ms,
                                     int64_t end_ms, int limit = 100);

  // Non-copyable
  RuleStore(const RuleStore&) = delete;
  RuleStore& operator=(const RuleStore&) = delete;

 private:
  bool CreateTables();

  /// Serialize geometric data (line, region, schedule, targets) to JSON.
  static std::string SerializeParams(const AnalysisRule& rule);

  /// Deserialize geometric data from JSON into an AnalysisRule.
  static void DeserializeParams(const std::string& json, AnalysisRule& rule);

  static int64_t Now();

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_RULE_STORE_RULE_STORE_H_
