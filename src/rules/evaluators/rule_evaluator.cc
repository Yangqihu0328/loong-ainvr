// Copyright 2026 Loong AI NVR Project

#include "rules/evaluators/rule_evaluator.h"

#include "rules/evaluators/counting_evaluator.h"
#include "rules/evaluators/cross_line_evaluator.h"
#include "rules/evaluators/loitering_evaluator.h"
#include "rules/evaluators/region_intrusion_evaluator.h"
#include "spdlog/spdlog.h"

namespace loong::rules {

namespace {

/// Placeholder evaluator for rule types not yet implemented.
class PlaceholderEvaluator : public RuleEvaluator {
 public:
  explicit PlaceholderEvaluator(RuleType type) : type_(type) {}

  bool Configure(const AnalysisRule& /*rule*/) override { return true; }

  std::vector<RuleEvent> Evaluate(
      const std::vector<Detection>& /*detections*/,
      const FrameContext& /*context*/) override {
    return {};
  }

  void Reset() override {}

  RuleType GetType() const override { return type_; }

 private:
  RuleType type_;
};

}  // namespace

std::unique_ptr<RuleEvaluator> CreateEvaluator(RuleType type) {
  switch (type) {
    case RuleType::kCrossLine:
      return std::make_unique<CrossLineEvaluator>();

    case RuleType::kRegionIntrusion:
      return std::make_unique<RegionIntrusionEvaluator>();

    case RuleType::kObjectCounting:
      return std::make_unique<CountingEvaluator>();

    case RuleType::kLoitering:
      return std::make_unique<LoiteringEvaluator>();
  }
  return std::make_unique<PlaceholderEvaluator>(RuleType::kCrossLine);
}

}  // namespace loong::rules
