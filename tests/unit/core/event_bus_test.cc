// Copyright 2026 Loong AI NVR Project

#include "core/event_bus/event_bus.h"

#include <string>

#include "gtest/gtest.h"

namespace loong {
namespace core {
namespace {

TEST(EventBus, PublishAndSubscribe) {
  auto& bus = EventBus::Instance();
  bool called = false;

  auto id = bus.Subscribe("test_event", [&called](const std::any&) {
    called = true;
  });

  bus.Publish("test_event");
  EXPECT_TRUE(called);

  bus.Unsubscribe(id);
}

TEST(EventBus, PublishWithData) {
  auto& bus = EventBus::Instance();
  int received_value = 0;

  auto id = bus.Subscribe("data_event", [&received_value](const std::any& data) {
    received_value = std::any_cast<int>(data);
  });

  bus.Publish("data_event", std::any(42));
  EXPECT_EQ(received_value, 42);

  bus.Unsubscribe(id);
}

TEST(EventBus, Unsubscribe) {
  auto& bus = EventBus::Instance();
  int call_count = 0;

  auto id = bus.Subscribe("unsub_event", [&call_count](const std::any&) {
    ++call_count;
  });

  bus.Publish("unsub_event");
  EXPECT_EQ(call_count, 1);

  bus.Unsubscribe(id);
  bus.Publish("unsub_event");
  EXPECT_EQ(call_count, 1);
}

}  // namespace
}  // namespace core
}  // namespace loong
