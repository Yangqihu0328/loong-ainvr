// Copyright 2026 Loong AI NVR Project

#include "rules/rule_engine/rule_engine.h"

#include <chrono>

#include "core/event_bus/event_bus.h"
#include "nlohmann/json.hpp"
#include "rules/evaluators/counting_evaluator.h"
#include "spdlog/spdlog.h"

namespace loong::rules {

RuleEngine::RuleEngine() = default;
RuleEngine::~RuleEngine() = default;

void RuleEngine::SetRuleStore(std::shared_ptr<RuleStore> store) {
  store_ = std::move(store);
}

void RuleEngine::LoadRules() {
  if (!store_) return;

  std::lock_guard<std::mutex> lock(mutex_);
  evaluators_.clear();

  auto all_rules = store_->ListRules();
  for (const auto& rule : all_rules) {
    if (!rule.enabled) continue;

    auto& entries = evaluators_[rule.channel_id];

    EvaluatorEntry entry;
    entry.rule_id = rule.id;
    entry.rule = rule;
    entry.cooldown_ms = rule.cooldown_sec * 1000;
    entry.evaluator = CreateEvaluator(rule.type);
    if (entry.evaluator) {
      entry.evaluator->Configure(rule);
      entries.push_back(std::move(entry));
    }
  }

  size_t total = 0;
  for (const auto& [ch, entries] : evaluators_) {
    total += entries.size();
  }
  spdlog::info("RuleEngine: loaded {} evaluators across {} channels", total,
               evaluators_.size());
}

void RuleEngine::ReloadChannel(int channel_id) {
  if (!store_) return;

  auto new_entries = BuildEvaluators(channel_id);

  std::lock_guard<std::mutex> lock(mutex_);
  evaluators_.erase(channel_id);
  evaluators_.emplace(channel_id, std::move(new_entries));

  spdlog::debug("RuleEngine: reloaded channel {} ({} evaluators)",
                channel_id, evaluators_[channel_id].size());
}

// ── CRUD ──

int64_t RuleEngine::CreateRule(const AnalysisRule& rule) {
  if (!store_) return -1;
  int64_t id = store_->CreateRule(rule);
  if (id > 0) {
    ReloadChannel(rule.channel_id);
  }
  return id;
}

bool RuleEngine::UpdateRule(const AnalysisRule& rule) {
  if (!store_) return false;
  bool ok = store_->UpdateRule(rule);
  if (ok) {
    ReloadChannel(rule.channel_id);
  }
  return ok;
}

bool RuleEngine::DeleteRule(int64_t rule_id) {
  if (!store_) return false;

  AnalysisRule existing;
  if (!store_->GetRule(rule_id, existing)) return false;

  bool ok = store_->DeleteRule(rule_id);
  if (ok) {
    ReloadChannel(existing.channel_id);
  }
  return ok;
}

bool RuleEngine::GetRule(int64_t rule_id, AnalysisRule& out) {
  if (!store_) return false;
  return store_->GetRule(rule_id, out);
}

std::vector<AnalysisRule> RuleEngine::ListRules() {
  if (!store_) return {};
  return store_->ListRules();
}

std::vector<AnalysisRule> RuleEngine::ListRulesByChannel(int channel_id) {
  if (!store_) return {};
  return store_->ListRulesByChannel(channel_id);
}

// ── Evaluation ──

std::vector<RuleEvent> RuleEngine::Evaluate(
    int channel_id, const std::vector<Detection>& detections,
    const FrameContext& context) {
  std::vector<RuleEvent> all_events;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = evaluators_.find(channel_id);
  if (it == evaluators_.end()) return all_events;

  for (auto& entry : it->second) {
    if (!entry.evaluator) continue;

    // Cooldown: skip if last event was too recent
    if (entry.last_event_time_ms > 0 &&
        (context.timestamp_ms - entry.last_event_time_ms) <
            entry.cooldown_ms) {
      continue;
    }

    auto events = entry.evaluator->Evaluate(detections, context);
    for (auto& ev : events) {
      entry.last_event_time_ms = ev.timestamp;

      // Log to store
      if (store_) {
        store_->LogEvent(ev);
      }

      // Publish to EventBus
      PublishEvent(ev);

      all_events.push_back(std::move(ev));
    }
  }

  return all_events;
}

size_t RuleEngine::EvaluatorCount(int channel_id) const {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = evaluators_.find(channel_id);
  if (it == evaluators_.end()) return 0;
  return it->second.size();
}

bool RuleEngine::HasRules() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !evaluators_.empty();
}

// ── Private ──

std::vector<RuleEngine::EvaluatorEntry> RuleEngine::BuildEvaluators(
    int channel_id) {
  std::vector<EvaluatorEntry> entries;
  if (!store_) return entries;

  auto rules = store_->ListEnabledRulesByChannel(channel_id);
  for (const auto& rule : rules) {
    EvaluatorEntry entry;
    entry.rule_id = rule.id;
    entry.rule = rule;
    entry.cooldown_ms = rule.cooldown_sec * 1000;
    entry.evaluator = CreateEvaluator(rule.type);
    if (entry.evaluator) {
      entry.evaluator->Configure(rule);
      entries.push_back(std::move(entry));
    }
  }
  return entries;
}

RuleOverlayData RuleEngine::BuildOverlayData(int channel_id) {
  RuleOverlayData data;

  std::lock_guard<std::mutex> lock(mutex_);
  auto it = evaluators_.find(channel_id);
  if (it == evaluators_.end()) return data;

  for (const auto& entry : it->second) {
    const auto& rule = entry.rule;

    switch (rule.type) {
      case RuleType::kCrossLine: {
        RuleLineOverlay ln;
        ln.x1 = rule.line.start.x;
        ln.y1 = rule.line.start.y;
        ln.x2 = rule.line.end.x;
        ln.y2 = rule.line.end.y;
        ln.label = rule.name;
        ln.show_counts = false;
        data.lines.push_back(std::move(ln));
        break;
      }
      case RuleType::kObjectCounting: {
        RuleLineOverlay ln;
        ln.x1 = rule.line.start.x;
        ln.y1 = rule.line.start.y;
        ln.x2 = rule.line.end.x;
        ln.y2 = rule.line.end.y;
        ln.label = rule.name;
        ln.show_counts = true;
        auto* ce = dynamic_cast<CountingEvaluator*>(entry.evaluator.get());
        if (ce) {
          ln.count_a_to_b = ce->GetCountAtoB();
          ln.count_b_to_a = ce->GetCountBtoA();
        }
        data.lines.push_back(std::move(ln));
        break;
      }
      case RuleType::kRegionIntrusion:
      case RuleType::kLoitering: {
        RuleRegionOverlay reg;
        reg.label = rule.name;
        reg.alarm_active = false;
        for (const auto& v : rule.region.vertices) {
          reg.vertices.emplace_back(v.x, v.y);
        }
        data.regions.push_back(std::move(reg));
        break;
      }
    }
  }

  return data;
}

void RuleEngine::PublishEvent(const RuleEvent& event) {
  nlohmann::json data = {
      {"rule_id", event.rule_id},
      {"rule_name", event.rule_name},
      {"rule_type", RuleTypeToString(event.rule_type)},
      {"channel_id", event.channel_id},
      {"timestamp", event.timestamp},
      {"severity", SeverityToString(event.severity)},
      {"direction", event.direction},
      {"count_value", event.count_value},
      {"dwell_time_sec", event.dwell_time_sec},
      {"trigger",
       {{"class_name", event.trigger_detection.class_name},
        {"confidence", event.trigger_detection.confidence}}},
  };

  core::EventBus::Instance().Publish("rule.triggered", data.dump());

  spdlog::info("RuleEvent: rule='{}' type={} ch={} severity={}",
               event.rule_name, RuleTypeToString(event.rule_type),
               event.channel_id, SeverityToString(event.severity));
}

}  // namespace loong::rules
