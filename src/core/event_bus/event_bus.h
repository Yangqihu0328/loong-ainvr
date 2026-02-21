// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_EVENT_BUS_EVENT_BUS_H_
#define LOONG_CORE_EVENT_BUS_EVENT_BUS_H_

#include <any>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace loong::core {

/// A simple publish-subscribe event bus for decoupled component communication.
class EventBus {
 public:
  using EventHandler = std::function<void(const std::any&)>;
  using SubscriptionId = uint64_t;

  static EventBus& Instance();

  /// Subscribe to an event type. Returns a subscription ID for unsubscribing.
  SubscriptionId Subscribe(const std::string& event_type,
                           EventHandler handler);

  /// Unsubscribe from an event.
  void Unsubscribe(SubscriptionId id);

  /// Publish an event to all subscribers of the given type.
  void Publish(const std::string& event_type, const std::any& data);

  /// Convenience: publish with no data.
  void Publish(const std::string& event_type);

  // Non-copyable
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;

 private:
  EventBus() = default;

  struct Subscription {
    SubscriptionId id;
    EventHandler handler;
  };

  std::unordered_map<std::string, std::vector<Subscription>> subscribers_;
  mutable std::mutex mutex_;
  SubscriptionId next_id_ = 1;
};

}  // namespace loong::core

#endif  // LOONG_CORE_EVENT_BUS_EVENT_BUS_H_
