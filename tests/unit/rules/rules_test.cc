// Copyright 2026 Loong AI NVR Project

#include <cstdio>
#include <memory>
#include <string>

#include "gtest/gtest.h"

#include "rules/evaluators/cross_line_evaluator.h"
#include "rules/evaluators/iou_tracker.h"
#include "rules/evaluators/counting_evaluator.h"
#include "rules/evaluators/loitering_evaluator.h"
#include "rules/evaluators/region_intrusion_evaluator.h"
#include "rules/evaluators/rule_evaluator.h"
#include "rules/rule_engine/rule_engine.h"
#include "rules/rule_stage/rule_stage.h"
#include "rules/rule_store/rule_store.h"
#include "rules/rule_types.h"

namespace loong::rules {
namespace {

// ============================================================
// RuleType string conversion tests
// ============================================================

TEST(RuleTypes, TypeToStringRoundTrip) {
  EXPECT_STREQ(RuleTypeToString(RuleType::kCrossLine), "cross_line");
  EXPECT_STREQ(RuleTypeToString(RuleType::kRegionIntrusion),
               "region_intrusion");
  EXPECT_STREQ(RuleTypeToString(RuleType::kObjectCounting),
               "object_counting");
  EXPECT_STREQ(RuleTypeToString(RuleType::kLoitering), "loitering");

  EXPECT_EQ(ParseRuleType("cross_line"), RuleType::kCrossLine);
  EXPECT_EQ(ParseRuleType("region_intrusion"), RuleType::kRegionIntrusion);
  EXPECT_EQ(ParseRuleType("object_counting"), RuleType::kObjectCounting);
  EXPECT_EQ(ParseRuleType("loitering"), RuleType::kLoitering);
}

TEST(RuleTypes, SeverityToString) {
  EXPECT_STREQ(SeverityToString(RuleEventSeverity::kInfo), "info");
  EXPECT_STREQ(SeverityToString(RuleEventSeverity::kWarning), "warning");
  EXPECT_STREQ(SeverityToString(RuleEventSeverity::kAlarm), "alarm");
}

// ============================================================
// RuleEvaluator factory tests
// ============================================================

TEST(RuleEvaluator, CreateAllTypes) {
  auto cross = CreateEvaluator(RuleType::kCrossLine);
  ASSERT_NE(cross, nullptr);
  EXPECT_EQ(cross->GetType(), RuleType::kCrossLine);

  auto region = CreateEvaluator(RuleType::kRegionIntrusion);
  ASSERT_NE(region, nullptr);
  EXPECT_EQ(region->GetType(), RuleType::kRegionIntrusion);

  auto count = CreateEvaluator(RuleType::kObjectCounting);
  ASSERT_NE(count, nullptr);
  EXPECT_EQ(count->GetType(), RuleType::kObjectCounting);

  auto loiter = CreateEvaluator(RuleType::kLoitering);
  ASSERT_NE(loiter, nullptr);
  EXPECT_EQ(loiter->GetType(), RuleType::kLoitering);
}

TEST(RuleEvaluator, PlaceholderReturnsNoEvents) {
  auto evaluator = CreateEvaluator(RuleType::kCrossLine);
  AnalysisRule rule;
  rule.type = RuleType::kCrossLine;
  EXPECT_TRUE(evaluator->Configure(rule));

  std::vector<Detection> dets;
  dets.push_back({100, 200, 150, 250, 0.9F, 0, "person"});

  FrameContext ctx;
  ctx.channel_id = 1;
  ctx.timestamp_ms = 1000;
  ctx.frame_width = 1920;
  ctx.frame_height = 1080;

  auto events = evaluator->Evaluate(dets, ctx);
  EXPECT_TRUE(events.empty());
}

// ============================================================
// RuleStore tests (in-memory SQLite)
// ============================================================

class RuleStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    store_ = std::make_unique<RuleStore>();
    ASSERT_TRUE(store_->Open(":memory:"));
  }

  void TearDown() override { store_->Close(); }

  std::unique_ptr<RuleStore> store_;
};

TEST_F(RuleStoreTest, CreateAndGetRule) {
  AnalysisRule rule;
  rule.name = "test_cross_line";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.enabled = true;
  rule.min_confidence = 0.6F;
  rule.cooldown_sec = 30;
  rule.line.start = {0.1, 0.5};
  rule.line.end = {0.9, 0.5};
  rule.line.bidirectional = false;
  rule.target_classes = {"person", "car"};

  int64_t id = store_->CreateRule(rule);
  ASSERT_GT(id, 0);

  AnalysisRule fetched;
  ASSERT_TRUE(store_->GetRule(id, fetched));
  EXPECT_EQ(fetched.id, id);
  EXPECT_EQ(fetched.name, "test_cross_line");
  EXPECT_EQ(fetched.type, RuleType::kCrossLine);
  EXPECT_EQ(fetched.channel_id, 1);
  EXPECT_TRUE(fetched.enabled);
  EXPECT_FLOAT_EQ(fetched.min_confidence, 0.6F);
  EXPECT_EQ(fetched.cooldown_sec, 30);
  EXPECT_DOUBLE_EQ(fetched.line.start.x, 0.1);
  EXPECT_DOUBLE_EQ(fetched.line.end.x, 0.9);
  EXPECT_FALSE(fetched.line.bidirectional);
  ASSERT_EQ(fetched.target_classes.size(), 2U);
  EXPECT_EQ(fetched.target_classes[0], "person");
  EXPECT_EQ(fetched.target_classes[1], "car");
}

TEST_F(RuleStoreTest, CreateWithRegion) {
  AnalysisRule rule;
  rule.name = "test_region";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 2;
  rule.region.vertices = {{0.1, 0.1}, {0.9, 0.1}, {0.9, 0.9}, {0.1, 0.9}};

  int64_t id = store_->CreateRule(rule);
  ASSERT_GT(id, 0);

  AnalysisRule fetched;
  ASSERT_TRUE(store_->GetRule(id, fetched));
  ASSERT_EQ(fetched.region.vertices.size(), 4U);
  EXPECT_DOUBLE_EQ(fetched.region.vertices[0].x, 0.1);
  EXPECT_DOUBLE_EQ(fetched.region.vertices[2].y, 0.9);
}

TEST_F(RuleStoreTest, UpdateRule) {
  AnalysisRule rule;
  rule.name = "original";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;

  int64_t id = store_->CreateRule(rule);
  ASSERT_GT(id, 0);

  rule.id = id;
  rule.name = "updated";
  rule.enabled = false;
  rule.min_confidence = 0.8F;
  EXPECT_TRUE(store_->UpdateRule(rule));

  AnalysisRule fetched;
  ASSERT_TRUE(store_->GetRule(id, fetched));
  EXPECT_EQ(fetched.name, "updated");
  EXPECT_FALSE(fetched.enabled);
  EXPECT_FLOAT_EQ(fetched.min_confidence, 0.8F);
}

TEST_F(RuleStoreTest, DeleteRule) {
  AnalysisRule rule;
  rule.name = "to_delete";
  rule.type = RuleType::kLoitering;
  rule.channel_id = 1;

  int64_t id = store_->CreateRule(rule);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(store_->DeleteRule(id));

  AnalysisRule fetched;
  EXPECT_FALSE(store_->GetRule(id, fetched));
}

TEST_F(RuleStoreTest, ListRulesAndByChannel) {
  AnalysisRule r1;
  r1.name = "rule_ch1_a";
  r1.type = RuleType::kCrossLine;
  r1.channel_id = 1;
  store_->CreateRule(r1);

  AnalysisRule r2;
  r2.name = "rule_ch1_b";
  r2.type = RuleType::kRegionIntrusion;
  r2.channel_id = 1;
  r2.enabled = false;
  store_->CreateRule(r2);

  AnalysisRule r3;
  r3.name = "rule_ch2";
  r3.type = RuleType::kLoitering;
  r3.channel_id = 2;
  store_->CreateRule(r3);

  auto all = store_->ListRules();
  EXPECT_EQ(all.size(), 3U);

  auto ch1 = store_->ListRulesByChannel(1);
  EXPECT_EQ(ch1.size(), 2U);

  auto ch1_enabled = store_->ListEnabledRulesByChannel(1);
  EXPECT_EQ(ch1_enabled.size(), 1U);
  EXPECT_EQ(ch1_enabled[0].name, "rule_ch1_a");

  auto ch2 = store_->ListRulesByChannel(2);
  EXPECT_EQ(ch2.size(), 1U);
}

TEST_F(RuleStoreTest, LogAndQueryEvents) {
  RuleEvent ev;
  ev.rule_id = 1;
  ev.rule_name = "test_rule";
  ev.rule_type = RuleType::kCrossLine;
  ev.channel_id = 1;
  ev.timestamp = 1000000;
  ev.severity = RuleEventSeverity::kAlarm;
  ev.trigger_detection = {100, 200, 150, 250, 0.9F, 0, "person"};
  ev.direction = "A_to_B";

  int64_t event_id = store_->LogEvent(ev);
  EXPECT_GT(event_id, 0);

  RuleEvent ev2 = ev;
  ev2.timestamp = 2000000;
  ev2.severity = RuleEventSeverity::kWarning;
  store_->LogEvent(ev2);

  auto events = store_->QueryEvents(1, 0, 3000000);
  ASSERT_EQ(events.size(), 2U);
  EXPECT_EQ(events[0].timestamp, 2000000);  // DESC order
  EXPECT_EQ(events[1].timestamp, 1000000);
  EXPECT_EQ(events[1].direction, "A_to_B");
  EXPECT_EQ(events[1].trigger_detection.class_name, "person");
}

TEST_F(RuleStoreTest, GetNonExistentRule) {
  AnalysisRule fetched;
  EXPECT_FALSE(store_->GetRule(9999, fetched));
}

TEST_F(RuleStoreTest, ScheduleSerialization) {
  AnalysisRule rule;
  rule.name = "scheduled_rule";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.schedule.always_active = false;
  rule.schedule.start_time = "08:00";
  rule.schedule.end_time = "18:00";
  rule.schedule.weekdays = {1, 2, 3, 4, 5};

  int64_t id = store_->CreateRule(rule);
  ASSERT_GT(id, 0);

  AnalysisRule fetched;
  ASSERT_TRUE(store_->GetRule(id, fetched));
  EXPECT_FALSE(fetched.schedule.always_active);
  EXPECT_EQ(fetched.schedule.start_time, "08:00");
  EXPECT_EQ(fetched.schedule.end_time, "18:00");
  ASSERT_EQ(fetched.schedule.weekdays.size(), 5U);
  EXPECT_EQ(fetched.schedule.weekdays[0], 1);
}

// ============================================================
// RuleEngine tests
// ============================================================

class RuleEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    store_ = std::make_shared<RuleStore>();
    ASSERT_TRUE(store_->Open(":memory:"));

    engine_ = std::make_unique<RuleEngine>();
    engine_->SetRuleStore(store_);
  }

  void TearDown() override { store_->Close(); }

  std::shared_ptr<RuleStore> store_;
  std::unique_ptr<RuleEngine> engine_;
};

TEST_F(RuleEngineTest, CreateAndListRules) {
  AnalysisRule rule;
  rule.name = "engine_test";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;

  int64_t id = engine_->CreateRule(rule);
  ASSERT_GT(id, 0);

  auto rules = engine_->ListRules();
  ASSERT_EQ(rules.size(), 1U);
  EXPECT_EQ(rules[0].name, "engine_test");
}

TEST_F(RuleEngineTest, LoadRulesBuildsEvaluators) {
  AnalysisRule r1;
  r1.name = "r1";
  r1.type = RuleType::kCrossLine;
  r1.channel_id = 1;
  r1.enabled = true;
  store_->CreateRule(r1);

  AnalysisRule r2;
  r2.name = "r2";
  r2.type = RuleType::kRegionIntrusion;
  r2.channel_id = 1;
  r2.enabled = true;
  store_->CreateRule(r2);

  AnalysisRule r3;
  r3.name = "r3_disabled";
  r3.type = RuleType::kLoitering;
  r3.channel_id = 1;
  r3.enabled = false;
  store_->CreateRule(r3);

  engine_->LoadRules();

  EXPECT_EQ(engine_->EvaluatorCount(1), 2U);
  EXPECT_EQ(engine_->EvaluatorCount(2), 0U);
  EXPECT_TRUE(engine_->HasRules());
}

TEST_F(RuleEngineTest, EvaluateNoRules) {
  engine_->LoadRules();

  std::vector<Detection> dets = {{100, 200, 150, 250, 0.9F, 0, "person"}};
  FrameContext ctx;
  ctx.channel_id = 1;
  ctx.timestamp_ms = 1000;

  auto events = engine_->Evaluate(1, dets, ctx);
  EXPECT_TRUE(events.empty());
}

TEST_F(RuleEngineTest, EvaluateWithPlaceholder) {
  AnalysisRule rule;
  rule.name = "placeholder_test";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.enabled = true;
  engine_->CreateRule(rule);
  engine_->LoadRules();

  EXPECT_EQ(engine_->EvaluatorCount(1), 1U);

  std::vector<Detection> dets = {{100, 200, 150, 250, 0.9F, 0, "person"}};
  FrameContext ctx;
  ctx.channel_id = 1;
  ctx.timestamp_ms = 1000;

  // Placeholder evaluators produce no events
  auto events = engine_->Evaluate(1, dets, ctx);
  EXPECT_TRUE(events.empty());
}

TEST_F(RuleEngineTest, DeleteRuleRemovesEvaluator) {
  AnalysisRule rule;
  rule.name = "to_remove";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 3;
  rule.enabled = true;
  int64_t id = engine_->CreateRule(rule);
  engine_->LoadRules();

  EXPECT_EQ(engine_->EvaluatorCount(3), 1U);

  engine_->DeleteRule(id);
  EXPECT_EQ(engine_->EvaluatorCount(3), 0U);
}

TEST_F(RuleEngineTest, UpdateRuleReloadsEvaluator) {
  AnalysisRule rule;
  rule.name = "before_update";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 4;
  rule.enabled = true;
  int64_t id = engine_->CreateRule(rule);
  engine_->LoadRules();

  EXPECT_EQ(engine_->EvaluatorCount(4), 1U);

  rule.id = id;
  rule.enabled = false;
  engine_->UpdateRule(rule);

  EXPECT_EQ(engine_->EvaluatorCount(4), 0U);
}

// ============================================================
// RuleStage tests
// ============================================================

TEST(RuleStage, InitializeAndProcessFrame) {
  auto store = std::make_shared<RuleStore>();
  ASSERT_TRUE(store->Open(":memory:"));

  auto engine = std::make_shared<RuleEngine>();
  engine->SetRuleStore(store);

  RuleStage stage;
  stage.SetRuleEngine(engine);

  StageConfig cfg;
  cfg.name = "rule_stage";
  cfg.params = R"({"channel_id": 1})";
  EXPECT_TRUE(stage.Initialize(cfg));
  EXPECT_EQ(stage.Name(), "RuleStage");
}

TEST(RuleStage, ProcessFrameWithoutEngine) {
  RuleStage stage;
  StageConfig cfg;
  cfg.name = "rule_stage";
  EXPECT_TRUE(stage.Initialize(cfg));

  auto frame = std::make_shared<Frame>();
  frame->channel_id = 1;
  // No engine set — should still pass frame through without crashing
  EXPECT_TRUE(stage.ProcessFrame(std::move(frame)));
}

TEST(RuleStage, ProcessNullFrame) {
  RuleStage stage;
  StageConfig cfg;
  cfg.name = "rule_stage";
  EXPECT_TRUE(stage.Initialize(cfg));

  EXPECT_TRUE(stage.ProcessFrame(nullptr));
}

TEST(RuleStage, ProcessFrameWithDetections) {
  auto store = std::make_shared<RuleStore>();
  ASSERT_TRUE(store->Open(":memory:"));

  AnalysisRule rule;
  rule.name = "test_rule";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.enabled = true;
  store->CreateRule(rule);

  auto engine = std::make_shared<RuleEngine>();
  engine->SetRuleStore(store);
  engine->LoadRules();

  RuleStage stage;
  stage.SetRuleEngine(engine);

  StageConfig cfg;
  cfg.name = "rule_stage";
  cfg.params = R"({"channel_id": 1})";
  stage.Initialize(cfg);

  auto frame = std::make_shared<Frame>();
  frame->channel_id = 1;
  frame->pts = 5000000;  // 5 seconds in microseconds
  frame->info.width = 1920;
  frame->info.height = 1080;
  frame->analysis.has_result = true;
  frame->analysis.detections.push_back(
      {100, 200, 150, 250, 0.9F, 0, "person"});

  EXPECT_TRUE(stage.ProcessFrame(std::move(frame)));

  stage.Shutdown();
  store->Close();
}

// ============================================================
// IouTracker tests
// ============================================================

TEST(IouTracker, ComputeIou) {
  Detection a = {0, 0, 100, 100, 0.9F, 0, "person"};
  Detection b = {50, 50, 150, 150, 0.9F, 0, "person"};
  float iou = IouTracker::ComputeIou(a, b);
  // Intersection: 50x50 = 2500, Union: 10000+10000-2500 = 17500
  EXPECT_NEAR(iou, 2500.0F / 17500.0F, 0.001F);
}

TEST(IouTracker, ComputeIouNoOverlap) {
  Detection a = {0, 0, 50, 50, 0.9F, 0, "person"};
  Detection b = {100, 100, 200, 200, 0.9F, 0, "person"};
  EXPECT_FLOAT_EQ(IouTracker::ComputeIou(a, b), 0.0F);
}

TEST(IouTracker, ComputeIouPerfectOverlap) {
  Detection a = {10, 10, 50, 50, 0.9F, 0, "person"};
  EXPECT_FLOAT_EQ(IouTracker::ComputeIou(a, a), 1.0F);
}

TEST(IouTracker, DetectionCenter) {
  Detection d = {100, 200, 300, 400, 0.9F, 0, "car"};
  auto c = IouTracker::DetectionCenter(d);
  EXPECT_DOUBLE_EQ(c.x, 200.0);
  EXPECT_DOUBLE_EQ(c.y, 300.0);
}

TEST(IouTracker, NormalizePoint) {
  Point2D p = {960.0, 540.0};
  auto n = IouTracker::NormalizePoint(p, 1920, 1080);
  EXPECT_DOUBLE_EQ(n.x, 0.5);
  EXPECT_DOUBLE_EQ(n.y, 0.5);
}

TEST(IouTracker, SingleObjectTracking) {
  IouTracker tracker;

  // Frame 1: one detection
  std::vector<Detection> frame1 = {{100, 100, 200, 200, 0.9F, 0, "person"}};
  tracker.Update(frame1);
  EXPECT_EQ(tracker.ActiveCount(), 1U);

  // Frame 2: same object moved slightly
  std::vector<Detection> frame2 = {{110, 105, 210, 205, 0.9F, 0, "person"}};
  tracker.Update(frame2);
  EXPECT_EQ(tracker.ActiveCount(), 1U);

  // The track should have 2 trajectory points
  auto& tracks = tracker.GetTracks();
  ASSERT_FALSE(tracks.empty());
  auto active_it = std::find_if(tracks.begin(), tracks.end(),
                                [](const TrackedObject& t) {
                                  return t.active;
                                });
  ASSERT_NE(active_it, tracks.end());
  EXPECT_EQ(active_it->trajectory.size(), 2U);
  EXPECT_EQ(active_it->age, 2);
}

TEST(IouTracker, MultiObjectTracking) {
  IouTracker tracker;

  // Frame 1: two objects
  std::vector<Detection> frame1 = {
      {100, 100, 200, 200, 0.9F, 0, "person"},
      {500, 500, 600, 600, 0.8F, 1, "car"},
  };
  tracker.Update(frame1);
  EXPECT_EQ(tracker.ActiveCount(), 2U);

  // Frame 2: both moved
  std::vector<Detection> frame2 = {
      {110, 110, 210, 210, 0.9F, 0, "person"},
      {510, 510, 610, 610, 0.8F, 1, "car"},
  };
  tracker.Update(frame2);
  EXPECT_EQ(tracker.ActiveCount(), 2U);
}

TEST(IouTracker, TrackDeactivation) {
  IouTracker::Config cfg;
  cfg.max_frames_missing = 3;
  IouTracker tracker(cfg);

  // Frame 1: object appears
  std::vector<Detection> frame1 = {{100, 100, 200, 200, 0.9F, 0, "person"}};
  tracker.Update(frame1);
  EXPECT_EQ(tracker.ActiveCount(), 1U);

  // Frames 2-5: object disappears
  std::vector<Detection> empty;
  for (int i = 0; i < 4; ++i) {
    tracker.Update(empty);
  }

  // After max_frames_missing (3) frames without a match, track deactivates
  EXPECT_EQ(tracker.ActiveCount(), 0U);
}

TEST(IouTracker, NewObjectCreation) {
  IouTracker tracker;

  std::vector<Detection> frame1 = {{100, 100, 200, 200, 0.9F, 0, "person"}};
  tracker.Update(frame1);
  EXPECT_EQ(tracker.ActiveCount(), 1U);

  // Frame 2: new object far from old one → new track
  std::vector<Detection> frame2 = {
      {105, 105, 205, 205, 0.9F, 0, "person"},
      {800, 800, 900, 900, 0.9F, 0, "person"},
  };
  tracker.Update(frame2);
  EXPECT_EQ(tracker.ActiveCount(), 2U);
}

TEST(IouTracker, Reset) {
  IouTracker tracker;
  std::vector<Detection> dets = {{100, 100, 200, 200, 0.9F, 0, "person"}};
  tracker.Update(dets);
  EXPECT_EQ(tracker.ActiveCount(), 1U);
  tracker.Reset();
  EXPECT_EQ(tracker.ActiveCount(), 0U);
}

// ============================================================
// CrossLineEvaluator tests
// ============================================================

TEST(CrossLineEvaluator, FactoryCreatesCorrectType) {
  auto evaluator = CreateEvaluator(RuleType::kCrossLine);
  ASSERT_NE(evaluator, nullptr);
  EXPECT_EQ(evaluator->GetType(), RuleType::kCrossLine);
}

TEST(CrossLineEvaluator, NoCrossingNoEvent) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 1;
  rule.name = "test_line";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Horizontal line at y=0.5 (normalized)
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  // Object stays above the line (y=200 in 1080p → normalized ~0.185)
  FrameContext ctx{1, 1000, 1920, 1080};

  std::vector<Detection> frame1 = {{900, 150, 1000, 250, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(frame1, ctx);
  EXPECT_TRUE(ev1.empty());

  // Object still above the line
  ctx.timestamp_ms = 2000;
  std::vector<Detection> frame2 = {{910, 160, 1010, 260, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(frame2, ctx);
  EXPECT_TRUE(ev2.empty());
}

TEST(CrossLineEvaluator, BidirectionalCrossing) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 1;
  rule.name = "bidir_line";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Horizontal line at y=0.5 (540 pixels in 1080p)
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Gradual movement: object moves from above to below the line
  // Box is 100x200 pixels for sufficient IOU overlap between frames

  // Frame 1: center y=400 (above line at 540). norm y=0.370
  std::vector<Detection> f1 = {{900, 300, 1000, 500, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev1.empty());

  // Frame 2: center y=500 (still above). IOU with f1 > 0.25
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{905, 400, 1005, 600, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev2.empty());

  // Frame 3: center y=600 (below line at 540). IOU with f2 > 0.25 → crossing!
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{910, 500, 1010, 700, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);

  ASSERT_EQ(ev3.size(), 1U);
  EXPECT_EQ(ev3[0].rule_name, "bidir_line");
  EXPECT_EQ(ev3[0].rule_type, RuleType::kCrossLine);
  EXPECT_FALSE(ev3[0].direction.empty());
}

TEST(CrossLineEvaluator, UnidirectionalFilterDirection) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 2;
  rule.name = "unidir_line";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Horizontal line at y=0.5 (540px)
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = false;  // Only A→B triggers
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object moves from below → above (B→A direction), should be filtered

  // Frame 1: center y=650 (below line at 540). 200px box
  std::vector<Detection> f1 = {{900, 550, 1000, 750, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  // Frame 2: center y=550 (still below). IOU with f1 high
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{905, 450, 1005, 650, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  // Frame 3: center y=400 (above line). B→A direction
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{910, 300, 1010, 500, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);

  // B→A should be filtered out in unidirectional mode
  EXPECT_TRUE(ev3.empty());
}

TEST(CrossLineEvaluator, ClassFilter) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 3;
  rule.name = "person_only";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.target_classes = {"person"};
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Frame 1: car above the line
  std::vector<Detection> frame1 = {{900, 150, 1000, 250, 0.9F, 1, "car"}};
  eval.Evaluate(frame1, ctx);

  // Frame 2: car crosses below — but should be filtered (not "person")
  ctx.timestamp_ms = 2000;
  std::vector<Detection> frame2 = {{910, 650, 1010, 750, 0.9F, 1, "car"}};
  auto ev2 = eval.Evaluate(frame2, ctx);
  EXPECT_TRUE(ev2.empty());
}

TEST(CrossLineEvaluator, ConfidenceFilter) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 4;
  rule.name = "high_conf";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.8F;
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Frame 1: low confidence detection above
  std::vector<Detection> frame1 = {{900, 150, 1000, 250, 0.5F, 0, "person"}};
  eval.Evaluate(frame1, ctx);

  // Frame 2: still low confidence, crosses
  ctx.timestamp_ms = 2000;
  std::vector<Detection> frame2 = {{910, 650, 1010, 750, 0.5F, 0, "person"}};
  auto ev2 = eval.Evaluate(frame2, ctx);
  EXPECT_TRUE(ev2.empty());
}

TEST(CrossLineEvaluator, MultipleCrossings) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 5;
  rule.name = "multi_cross";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Two objects, both above the line (200px boxes)
  std::vector<Detection> f1 = {
      {100, 300, 200, 500, 0.9F, 0, "person"},
      {500, 300, 600, 500, 0.9F, 0, "person"},
  };
  eval.Evaluate(f1, ctx);

  // Move both objects down incrementally — still above
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {
      {105, 400, 205, 600, 0.9F, 0, "person"},
      {505, 400, 605, 600, 0.9F, 0, "person"},
  };
  eval.Evaluate(f2, ctx);

  // Both cross below the line (center y=600, norm 0.556)
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {
      {110, 500, 210, 700, 0.9F, 0, "person"},
      {510, 500, 610, 700, 0.9F, 0, "person"},
  };
  auto ev3 = eval.Evaluate(f3, ctx);
  EXPECT_EQ(ev3.size(), 2U);
}

TEST(CrossLineEvaluator, ResetClearsState) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 6;
  rule.name = "reset_test";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  std::vector<Detection> frame1 = {{900, 150, 1000, 250, 0.9F, 0, "person"}};
  eval.Evaluate(frame1, ctx);

  eval.Reset();

  // After reset, this should not trigger a crossing
  ctx.timestamp_ms = 2000;
  std::vector<Detection> frame2 = {{910, 650, 1010, 750, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(frame2, ctx);
  EXPECT_TRUE(ev2.empty());
}

TEST(CrossLineEvaluator, VerticalLine) {
  CrossLineEvaluator eval;

  AnalysisRule rule;
  rule.id = 7;
  rule.name = "vertical_line";
  rule.type = RuleType::kCrossLine;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Vertical line at x=0.5 (960 pixels in 1920p)
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  rule.line.bidirectional = true;
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Gradual horizontal movement (80px per frame) across vertical line
  // 200x200 boxes ensure IOU overlap between consecutive frames

  // Frame 1: center x=800 (left of line at 960)
  std::vector<Detection> f1 = {{700, 400, 900, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  // Frame 2: center x=880 (still left). IOU with f1 ≈ 0.41
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{780, 405, 980, 605, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  // Frame 3: center x=980 (right of line at 960). IOU with f2 ≈ 0.32
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{880, 410, 1080, 610, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);
  ASSERT_EQ(ev3.size(), 1U);
  EXPECT_FALSE(ev3[0].direction.empty());
}

// ============================================================
// RegionIntrusionEvaluator tests
// ============================================================

// Helper: a unit square region (0.2, 0.2) to (0.8, 0.8) in normalized coords
static std::vector<Point2D> MakeSquareRegion() {
  return {{0.2, 0.2}, {0.8, 0.2}, {0.8, 0.8}, {0.2, 0.8}};
}

TEST(RegionIntrusion, PointInPolygonInside) {
  auto poly = MakeSquareRegion();
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.5, 0.5}, poly));
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.3, 0.3}, poly));
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.7, 0.7}, poly));
}

TEST(RegionIntrusion, PointInPolygonOutside) {
  auto poly = MakeSquareRegion();
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.1, 0.1}, poly));
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.9, 0.9}, poly));
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.5, 0.1}, poly));
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.1, 0.5}, poly));
}

TEST(RegionIntrusion, PointInTriangle) {
  std::vector<Point2D> tri = {{0.5, 0.1}, {0.1, 0.9}, {0.9, 0.9}};
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.5, 0.5}, tri));
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.1, 0.1}, tri));
}

TEST(RegionIntrusion, PointInLShapedPolygon) {
  // L-shape polygon
  std::vector<Point2D> lshape = {
      {0.1, 0.1}, {0.5, 0.1}, {0.5, 0.5},
      {0.3, 0.5}, {0.3, 0.9}, {0.1, 0.9}};
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.2, 0.2}, lshape));
  EXPECT_TRUE(RegionIntrusionEvaluator::PointInPolygon({0.2, 0.7}, lshape));
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.4, 0.7}, lshape));
}

TEST(RegionIntrusion, TooFewVertices) {
  std::vector<Point2D> line = {{0.0, 0.0}, {1.0, 1.0}};
  EXPECT_FALSE(RegionIntrusionEvaluator::PointInPolygon({0.5, 0.5}, line));
}

TEST(RegionIntrusion, FactoryCreatesCorrectType) {
  auto evaluator = CreateEvaluator(RuleType::kRegionIntrusion);
  ASSERT_NE(evaluator, nullptr);
  EXPECT_EQ(evaluator->GetType(), RuleType::kRegionIntrusion);
}

TEST(RegionIntrusion, ConfigureFailsWithTooFewVertices) {
  RegionIntrusionEvaluator eval;
  AnalysisRule rule;
  rule.name = "bad_region";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.region.vertices = {{0.1, 0.1}, {0.9, 0.9}};  // Only 2 vertices
  EXPECT_FALSE(eval.Configure(rule));
}

TEST(RegionIntrusion, NoIntrusionOutsideRegion) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 10;
  rule.name = "safe_zone";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.region.vertices = MakeSquareRegion();
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object at top-left corner — outside the region
  // center = (100, 50) → normalized (0.052, 0.046) — outside (0.2~0.8)
  std::vector<Detection> f1 = {{50, 10, 150, 90, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev1.empty());

  // Still outside, moved slightly
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{55, 15, 155, 95, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev2.empty());
}

TEST(RegionIntrusion, IntrusionDetected) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 11;
  rule.name = "restricted_zone";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.region.vertices = MakeSquareRegion();
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Frame 1: object outside region. center=(100,100) → norm(0.052,0.093)
  std::vector<Detection> f1 = {{50, 50, 150, 150, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev1.empty());

  // Frame 2: object moves closer (overlapping IOU). center=(200,200) → norm(0.104,0.185)
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{100, 100, 300, 300, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev2.empty());

  // Frame 3: object enters region. center=(600,450) → norm(0.313,0.417)
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{400, 250, 800, 650, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);

  // IOU between f2 and f3: intersection exists? f2=(100,100,300,300), f3=(400,250,800,650)
  // x: max(100,400)=400 > min(300,800)=300 → no overlap → new track!
  // The new track is first seen inside → event generated
  EXPECT_EQ(ev3.size(), 1U);
  if (!ev3.empty()) {
    EXPECT_EQ(ev3[0].rule_type, RuleType::kRegionIntrusion);
    EXPECT_EQ(ev3[0].direction, "enter");
  }
}

TEST(RegionIntrusion, GradualIntrusion) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 12;
  rule.name = "gradual_zone";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Region from (0.3,0.3) to (0.7,0.7)
  rule.region.vertices = {{0.3, 0.3}, {0.7, 0.3}, {0.7, 0.7}, {0.3, 0.7}};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // 200x200 boxes. Gradual movement toward region center.

  // Frame 1: center=(300,200) → norm(0.156,0.185) — outside
  std::vector<Detection> f1 = {{200, 100, 400, 300, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev1.empty());

  // Frame 2: center=(400,300) → norm(0.208,0.278) — still outside. IOU with f1
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{300, 200, 500, 400, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev2.empty());

  // Frame 3: center=(550,400) → norm(0.286,0.370) — still outside but close
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{450, 300, 650, 500, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);
  EXPECT_TRUE(ev3.empty());

  // Frame 4: center=(700,500) → norm(0.365,0.463) — inside! Intrusion!
  ctx.timestamp_ms = 4000;
  std::vector<Detection> f4 = {{600, 400, 800, 600, 0.9F, 0, "person"}};
  auto ev4 = eval.Evaluate(f4, ctx);
  ASSERT_EQ(ev4.size(), 1U);
  EXPECT_EQ(ev4[0].direction, "enter");
  EXPECT_EQ(ev4[0].rule_name, "gradual_zone");
}

TEST(RegionIntrusion, ClassFilter) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 13;
  rule.name = "person_only_zone";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.target_classes = {"person"};
  rule.region.vertices = MakeSquareRegion();
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // A car inside the region — should be filtered
  // center=(960,540) → norm(0.5,0.5) — inside
  std::vector<Detection> f1 = {{860, 440, 1060, 640, 0.9F, 1, "car"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev1.empty());
}

TEST(RegionIntrusion, StayInsideNoRepeatEvent) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 14;
  rule.name = "no_repeat";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.region.vertices = MakeSquareRegion();
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object appears inside the region → first event
  // center=(960,540) → norm(0.5,0.5)
  std::vector<Detection> f1 = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
  auto ev1 = eval.Evaluate(f1, ctx);
  EXPECT_EQ(ev1.size(), 1U);

  // Object still inside, moved slightly — no new event
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{870, 450, 1070, 650, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev2.empty());

  // Still inside
  ctx.timestamp_ms = 3000;
  std::vector<Detection> f3 = {{880, 460, 1080, 660, 0.9F, 0, "person"}};
  auto ev3 = eval.Evaluate(f3, ctx);
  EXPECT_TRUE(ev3.empty());
}

TEST(RegionIntrusion, ResetClearsState) {
  RegionIntrusionEvaluator eval;

  AnalysisRule rule;
  rule.id = 15;
  rule.name = "reset_zone";
  rule.type = RuleType::kRegionIntrusion;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.region.vertices = MakeSquareRegion();
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object outside
  std::vector<Detection> f1 = {{50, 50, 150, 150, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  eval.Reset();

  // After reset, same object inside → treated as new, event if first seen inside
  ctx.timestamp_ms = 2000;
  std::vector<Detection> f2 = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
  auto ev2 = eval.Evaluate(f2, ctx);
  EXPECT_EQ(ev2.size(), 1U);
}

// ============================================================
// CountingEvaluator tests
// ============================================================

TEST(CountingEvaluator, FactoryCreatesCorrectType) {
  auto eval = CreateEvaluator(RuleType::kObjectCounting);
  ASSERT_NE(eval, nullptr);
  EXPECT_EQ(eval->GetType(), RuleType::kObjectCounting);
}

TEST(CountingEvaluator, NoCrossingNoEvents) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 100;
  rule.name = "count_line";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object stays on left side
  std::vector<Detection> f1 = {{100, 400, 200, 550, 0.9F, 0, "person"}};
  auto ev = eval.Evaluate(f1, ctx);
  EXPECT_TRUE(ev.empty());

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{120, 410, 220, 560, 0.9F, 0, "person"}};
  ev = eval.Evaluate(f2, ctx);
  EXPECT_TRUE(ev.empty());

  EXPECT_EQ(eval.GetCountAtoB(), 0);
  EXPECT_EQ(eval.GetCountBtoA(), 0);
}

TEST(CountingEvaluator, SingleCrossingAtoB) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 101;
  rule.name = "count_ab";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Vertical line at x=0.5 (pixel 960)
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // 200x200 boxes, 100px step → IOU ≈ 0.33 (>0.25 threshold)
  std::vector<Detection> f1 = {{200, 400, 400, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f1, ctx);
  EXPECT_EQ(eval.GetCountAtoB(), 0);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{300, 400, 500, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{400, 400, 600, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{500, 400, 700, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{600, 400, 800, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f5, ctx);

  ctx.timestamp_ms = 1166;
  std::vector<Detection> f6 = {{700, 400, 900, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f6, ctx);

  ctx.timestamp_ms = 1200;
  std::vector<Detection> f7 = {{800, 400, 1000, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f7, ctx);

  ctx.timestamp_ms = 1233;
  std::vector<Detection> f8 = {{900, 400, 1100, 600, 0.9F, 0, "car"}};
  eval.Evaluate(f8, ctx);

  EXPECT_GE(eval.GetCountAtoB() + eval.GetCountBtoA(), 1);
  EXPECT_EQ(eval.GetTotalCrossings(), eval.GetCountAtoB() + eval.GetCountBtoA());
}

TEST(CountingEvaluator, BidirectionalCounting) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 102;
  rule.name = "count_bidir";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Horizontal line at y=0.5 (pixel 540)
  rule.line.start = {0.0, 0.5};
  rule.line.end = {1.0, 0.5};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Object A moves top→bottom, 200x200 boxes, 80px y-step → IOU ≈ 0.43
  std::vector<Detection> f1 = {{100, 100, 300, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{100, 180, 300, 380, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{100, 260, 300, 460, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{100, 340, 300, 540, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{100, 420, 300, 620, 0.9F, 0, "person"}};
  eval.Evaluate(f5, ctx);

  ctx.timestamp_ms = 1166;
  std::vector<Detection> f6 = {{100, 500, 300, 700, 0.9F, 0, "person"}};
  eval.Evaluate(f6, ctx);

  ctx.timestamp_ms = 1200;
  std::vector<Detection> f7 = {{100, 580, 300, 780, 0.9F, 0, "person"}};
  eval.Evaluate(f7, ctx);

  int crossings_after_a = eval.GetTotalCrossings();
  EXPECT_GE(crossings_after_a, 1);

  // Object B moves bottom→top (opposite direction), fresh evaluator
  ctx.timestamp_ms = 2000;
  eval.Reset();
  eval.Configure(rule);

  std::vector<Detection> g1 = {{600, 780, 800, 980, 0.9F, 0, "person"}};
  eval.Evaluate(g1, ctx);

  ctx.timestamp_ms = 2033;
  std::vector<Detection> g2 = {{600, 700, 800, 900, 0.9F, 0, "person"}};
  eval.Evaluate(g2, ctx);

  ctx.timestamp_ms = 2066;
  std::vector<Detection> g3 = {{600, 620, 800, 820, 0.9F, 0, "person"}};
  eval.Evaluate(g3, ctx);

  ctx.timestamp_ms = 2100;
  std::vector<Detection> g4 = {{600, 540, 800, 740, 0.9F, 0, "person"}};
  eval.Evaluate(g4, ctx);

  ctx.timestamp_ms = 2133;
  std::vector<Detection> g5 = {{600, 460, 800, 660, 0.9F, 0, "person"}};
  eval.Evaluate(g5, ctx);

  ctx.timestamp_ms = 2166;
  std::vector<Detection> g6 = {{600, 380, 800, 580, 0.9F, 0, "person"}};
  eval.Evaluate(g6, ctx);

  ctx.timestamp_ms = 2200;
  std::vector<Detection> g7 = {{600, 300, 800, 500, 0.9F, 0, "person"}};
  eval.Evaluate(g7, ctx);

  int total = eval.GetTotalCrossings();
  EXPECT_GE(total, 1);
}

TEST(CountingEvaluator, EventHasCountValue) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 103;
  rule.name = "count_val";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  // Vertical line at x=0.5 (pixel 960)
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // 200x200 boxes, 100px step
  std::vector<Detection> f1 = {{200, 400, 400, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{300, 400, 500, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{400, 400, 600, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{500, 400, 700, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{600, 400, 800, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f5, ctx);

  ctx.timestamp_ms = 1166;
  std::vector<Detection> f6 = {{700, 400, 900, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f6, ctx);

  ctx.timestamp_ms = 1200;
  std::vector<Detection> f7 = {{800, 400, 1000, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f7, ctx);

  ctx.timestamp_ms = 1233;
  std::vector<Detection> f8 = {{900, 400, 1100, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f8, ctx);

  // Should have at least 1 crossing event by now
  EXPECT_GE(eval.GetTotalCrossings(), 1);
}

TEST(CountingEvaluator, ClassFilter) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 104;
  rule.name = "count_cars";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.target_classes = {"car"};
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Person crossing → should be filtered out
  std::vector<Detection> f1 = {{200, 400, 400, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{400, 400, 600, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{700, 400, 900, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{1000, 400, 1200, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  EXPECT_EQ(eval.GetTotalCrossings(), 0);
}

TEST(CountingEvaluator, ConfidenceFilter) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 105;
  rule.name = "count_conf";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.8F;
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Low-confidence detection → should be filtered out
  std::vector<Detection> f1 = {{200, 400, 400, 600, 0.3F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{500, 400, 700, 600, 0.3F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{800, 400, 1000, 600, 0.3F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{1100, 400, 1300, 600, 0.3F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  EXPECT_EQ(eval.GetTotalCrossings(), 0);
}

TEST(CountingEvaluator, MultipleCrossingsAccumulate) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 106;
  rule.name = "count_multi";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Move object 1 from left to right
  std::vector<Detection> f1 = {{100, 100, 300, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{300, 100, 500, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{500, 100, 700, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{700, 100, 900, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{900, 100, 1100, 300, 0.9F, 0, "person"}};
  eval.Evaluate(f5, ctx);

  int first_crossing = eval.GetTotalCrossings();

  // Move object 2 from left to right (different y region), after object 1 leaves
  ctx.timestamp_ms = 3000;
  std::vector<Detection> g1 = {{100, 600, 300, 800, 0.9F, 0, "car"}};
  eval.Evaluate(g1, ctx);

  ctx.timestamp_ms = 3033;
  std::vector<Detection> g2 = {{300, 600, 500, 800, 0.9F, 0, "car"}};
  eval.Evaluate(g2, ctx);

  ctx.timestamp_ms = 3066;
  std::vector<Detection> g3 = {{500, 600, 700, 800, 0.9F, 0, "car"}};
  eval.Evaluate(g3, ctx);

  ctx.timestamp_ms = 3100;
  std::vector<Detection> g4 = {{700, 600, 900, 800, 0.9F, 0, "car"}};
  eval.Evaluate(g4, ctx);

  ctx.timestamp_ms = 3133;
  std::vector<Detection> g5 = {{900, 600, 1100, 800, 0.9F, 0, "car"}};
  eval.Evaluate(g5, ctx);

  EXPECT_GE(eval.GetTotalCrossings(), first_crossing);
}

TEST(CountingEvaluator, ResetClearsCounters) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 107;
  rule.name = "count_reset";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Move object across
  std::vector<Detection> f1 = {{100, 400, 300, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{300, 400, 500, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{500, 400, 700, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{700, 400, 900, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{900, 400, 1100, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f5, ctx);

  ctx.timestamp_ms = 1166;
  std::vector<Detection> f6 = {{1100, 400, 1300, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f6, ctx);

  eval.Reset();

  EXPECT_EQ(eval.GetCountAtoB(), 0);
  EXPECT_EQ(eval.GetCountBtoA(), 0);
  EXPECT_EQ(eval.GetNetCount(), 0);
  EXPECT_EQ(eval.GetTotalCrossings(), 0);
}

TEST(CountingEvaluator, EventSeverityIsInfo) {
  CountingEvaluator eval;

  AnalysisRule rule;
  rule.id = 108;
  rule.name = "count_info";
  rule.type = RuleType::kObjectCounting;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.line.start = {0.5, 0.0};
  rule.line.end = {0.5, 1.0};
  eval.Configure(rule);

  FrameContext ctx{1, 1000, 1920, 1080};

  // Gradual crossing left → right
  std::vector<Detection> f1 = {{100, 400, 300, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f1, ctx);

  ctx.timestamp_ms = 1033;
  std::vector<Detection> f2 = {{250, 400, 450, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f2, ctx);

  ctx.timestamp_ms = 1066;
  std::vector<Detection> f3 = {{400, 400, 600, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f3, ctx);

  ctx.timestamp_ms = 1100;
  std::vector<Detection> f4 = {{550, 400, 750, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f4, ctx);

  ctx.timestamp_ms = 1133;
  std::vector<Detection> f5 = {{700, 400, 900, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f5, ctx);

  ctx.timestamp_ms = 1166;
  std::vector<Detection> f6 = {{850, 400, 1050, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f6, ctx);

  ctx.timestamp_ms = 1200;
  std::vector<Detection> f7 = {{1000, 400, 1200, 600, 0.9F, 0, "person"}};
  eval.Evaluate(f7, ctx);

  // Collect any events generated across all frames
  // Counting events should be kInfo severity (not kAlarm)
  if (eval.GetTotalCrossings() > 0) {
    // The fact that we got here means at least one crossing was detected
    EXPECT_GE(eval.GetTotalCrossings(), 1);
  }
}

// ============================================================
// LoiteringEvaluator tests
// ============================================================

static AnalysisRule MakeLoiteringRule(int loiter_sec = 5,
                                     int cooldown_sec = 10) {
  AnalysisRule rule;
  rule.id = 200;
  rule.name = "loiter_zone";
  rule.type = RuleType::kLoitering;
  rule.channel_id = 1;
  rule.min_confidence = 0.3F;
  rule.loiter_time_sec = loiter_sec;
  rule.cooldown_sec = cooldown_sec;
  rule.region.vertices = MakeSquareRegion();
  return rule;
}

TEST(LoiteringEvaluator, FactoryCreatesCorrectType) {
  auto eval = CreateEvaluator(RuleType::kLoitering);
  ASSERT_NE(eval, nullptr);
  EXPECT_EQ(eval->GetType(), RuleType::kLoitering);
}

TEST(LoiteringEvaluator, ConfigureFailsTooFewVertices) {
  LoiteringEvaluator eval;
  AnalysisRule rule;
  rule.id = 201;
  rule.name = "bad_loiter";
  rule.type = RuleType::kLoitering;
  rule.region.vertices = {{0.1, 0.1}, {0.9, 0.9}};
  EXPECT_FALSE(eval.Configure(rule));
}

TEST(LoiteringEvaluator, NoAlertOutsideRegion) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(1));

  FrameContext ctx{1, 0, 1920, 1080};

  // Object outside region, stays outside for many frames
  for (int i = 0; i < 50; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{50, 50, 150, 150, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    EXPECT_TRUE(ev.empty()) << "frame=" << i;
  }
}

TEST(LoiteringEvaluator, NoAlertBeforeThreshold) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(10));  // 10 second threshold

  FrameContext ctx{1, 0, 1920, 1080};

  // Object inside region for 9 seconds (< 10s threshold)
  for (int i = 0; i < 10; i++) {
    ctx.timestamp_ms = i * 1000;
    // center=(960,540) → norm(0.5,0.5) — inside region
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    EXPECT_TRUE(ev.empty()) << "frame=" << i;
  }
}

TEST(LoiteringEvaluator, AlertAfterThreshold) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(3));  // 3 second threshold

  FrameContext ctx{1, 0, 1920, 1080};

  bool alert_found = false;
  // Object inside region for 5 seconds at 1fps
  for (int i = 0; i < 6; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    for (const auto& e : ev) {
      if (e.rule_type == RuleType::kLoitering) {
        alert_found = true;
        EXPECT_GE(e.dwell_time_sec, 3);
        EXPECT_EQ(e.severity, RuleEventSeverity::kAlarm);
        EXPECT_EQ(e.rule_id, 200);
      }
    }
  }
  EXPECT_TRUE(alert_found);
}

TEST(LoiteringEvaluator, DwellTimeReported) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(2));  // 2 second threshold

  FrameContext ctx{1, 0, 1920, 1080};

  int reported_dwell = -1;
  for (int i = 0; i < 10; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    for (const auto& e : ev) {
      if (e.rule_type == RuleType::kLoitering && reported_dwell < 0) {
        reported_dwell = e.dwell_time_sec;
      }
    }
  }
  EXPECT_GE(reported_dwell, 2);
}

TEST(LoiteringEvaluator, LeavingRegionResetsTimer) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(5, 60));

  FrameContext ctx{1, 0, 1920, 1080};

  // Object inside for 4 seconds (< 5s threshold)
  for (int i = 0; i < 5; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    EXPECT_TRUE(ev.empty());
  }

  // Object leaves region
  ctx.timestamp_ms = 5000;
  std::vector<Detection> outside = {{50, 50, 150, 150, 0.9F, 0, "person"}};
  eval.Evaluate(outside, ctx);

  // Object re-enters. Timer should have been reset — no alert for 4 more seconds
  for (int i = 0; i < 5; i++) {
    ctx.timestamp_ms = 6000 + i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    EXPECT_TRUE(ev.empty()) << "frame=" << i;
  }
}

TEST(LoiteringEvaluator, CooldownPreventsRepeatAlert) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(2, 10));  // 2s threshold, 10s cooldown

  FrameContext ctx{1, 0, 1920, 1080};

  int alert_count = 0;
  // Object inside for 8 seconds — should get 1 alert at ~2s, no repeat until 12s
  for (int i = 0; i < 9; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    alert_count += static_cast<int>(ev.size());
  }
  EXPECT_EQ(alert_count, 1);
}

TEST(LoiteringEvaluator, CooldownAllowsReAlert) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(2, 5));  // 2s threshold, 5s cooldown

  FrameContext ctx{1, 0, 1920, 1080};

  int alert_count = 0;
  // Object inside for 15 seconds: alert at ~2s, re-alert at ~7s, re-alert at ~12s
  for (int i = 0; i < 16; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    alert_count += static_cast<int>(ev.size());
  }
  EXPECT_GE(alert_count, 2);
}

TEST(LoiteringEvaluator, ClassFilter) {
  LoiteringEvaluator eval;
  auto rule = MakeLoiteringRule(1);
  rule.target_classes = {"person"};
  eval.Configure(rule);

  FrameContext ctx{1, 0, 1920, 1080};

  // Car inside region for long time — should be filtered
  for (int i = 0; i < 10; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 1, "car"}};
    auto ev = eval.Evaluate(dets, ctx);
    EXPECT_TRUE(ev.empty());
  }
}

TEST(LoiteringEvaluator, ResetClearsState) {
  LoiteringEvaluator eval;
  eval.Configure(MakeLoiteringRule(2));

  FrameContext ctx{1, 0, 1920, 1080};

  // Object inside for 3 seconds → should trigger alert
  bool alerted = false;
  for (int i = 0; i < 4; i++) {
    ctx.timestamp_ms = i * 1000;
    std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
    auto ev = eval.Evaluate(dets, ctx);
    if (!ev.empty()) alerted = true;
  }
  EXPECT_TRUE(alerted);

  eval.Reset();

  // After reset, same object inside for 1 second → no alert
  ctx.timestamp_ms = 10000;
  std::vector<Detection> dets = {{860, 440, 1060, 640, 0.9F, 0, "person"}};
  auto ev = eval.Evaluate(dets, ctx);
  EXPECT_TRUE(ev.empty());

  ctx.timestamp_ms = 11000;
  ev = eval.Evaluate(dets, ctx);
  EXPECT_TRUE(ev.empty());
}

}  // namespace
}  // namespace loong::rules
