// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_RULES_RULE_ENGINE_RULE_ENGINE_H_
#define LOONG_RULES_RULE_ENGINE_RULE_ENGINE_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "rules/evaluators/rule_evaluator.h"
#include "rules/rule_store/rule_store.h"
#include "rules/rule_types.h"

namespace loong::rules {

/// Central rule engine that manages analysis rules and their evaluators.
///
/// Responsibilities:
///  - CRUD operations on rules (delegated to RuleStore for persistence)
///  - Maintains a hot cache of per-channel evaluator instances
///  - Evaluates detections against all enabled rules for a channel
///  - Publishes RuleEvents to EventBus and logs them to RuleStore
class RuleEngine {
 public:
  RuleEngine();
  ~RuleEngine();

  /// Set the backing store for persistent rules.
  void SetRuleStore(std::shared_ptr<RuleStore> store);

  /// Load all enabled rules from the store and build evaluator cache.
  /// Call once at startup after SetRuleStore().
  void LoadRules();

  /// Reload rules for a specific channel (e.g., after CRUD operations).
  void ReloadChannel(int channel_id);

  // ── CRUD (delegates to RuleStore + refreshes evaluator cache) ──

  int64_t CreateRule(const AnalysisRule& rule);
  bool UpdateRule(const AnalysisRule& rule);
  bool DeleteRule(int64_t rule_id);
  bool GetRule(int64_t rule_id, AnalysisRule& out);
  std::vector<AnalysisRule> ListRules();
  std::vector<AnalysisRule> ListRulesByChannel(int channel_id);

  // ── Evaluation ──

  /// Evaluate all enabled rules for the given channel against detections.
  /// Returns generated events (also published to EventBus and logged).
  std::vector<RuleEvent> Evaluate(int channel_id,
                                  const std::vector<Detection>& detections,
                                  const FrameContext& context);

  /// Get the number of active evaluators for a channel.
  size_t EvaluatorCount(int channel_id) const;

  /// Whether the rule engine has any rules loaded.
  bool HasRules() const;

  /// Get the backing rule store (for event queries).
  std::shared_ptr<RuleStore> GetRuleStore() const { return store_; }

  /// Build overlay visualization data for a channel's active rules.
  /// Includes virtual lines with counts, polygon regions, etc.
  RuleOverlayData BuildOverlayData(int channel_id);

  // Non-copyable
  RuleEngine(const RuleEngine&) = delete;
  RuleEngine& operator=(const RuleEngine&) = delete;

 private:
  /// Cached evaluator instance bound to a specific rule.
  struct EvaluatorEntry {
    int64_t rule_id = 0;
    AnalysisRule rule;                       // Cached for overlay geometry
    std::unique_ptr<RuleEvaluator> evaluator;
    int64_t last_event_time_ms = 0;  // Cooldown tracking
    int cooldown_ms = 60000;
  };

  /// Build evaluator entries for one channel from the store.
  std::vector<EvaluatorEntry> BuildEvaluators(int channel_id);

  /// Publish a RuleEvent to EventBus.
  static void PublishEvent(const RuleEvent& event);

  std::shared_ptr<RuleStore> store_;

  // channel_id → list of evaluator entries
  mutable std::mutex mutex_;
  std::unordered_map<int, std::vector<EvaluatorEntry>> evaluators_;
};

}  // namespace loong::rules

#endif  // LOONG_RULES_RULE_ENGINE_RULE_ENGINE_H_
