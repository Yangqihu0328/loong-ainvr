// Copyright 2026 Loong AI NVR Project

#include "core/event_bus/event_bus.h"

#include <algorithm>

#include "spdlog/spdlog.h"

namespace loong::core {

EventBus& EventBus::Instance() {
  static EventBus instance;
  return instance;
}

EventBus::SubscriptionId EventBus::Subscribe(const std::string& event_type,
                                              EventHandler handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto id = next_id_++;
  subscribers_[event_type].push_back({id, std::move(handler)});
  spdlog::debug("EventBus: subscribed to '{}' (id={})", event_type, id);
  return id;
}

void EventBus::Unsubscribe(SubscriptionId id) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& [event_type, subs] : subscribers_) {
    auto it = std::remove_if(subs.begin(), subs.end(),
                             [id](const Subscription& s) {
                               return s.id == id;
                             });
    if (it != subs.end()) {
      subs.erase(it, subs.end());
      spdlog::debug("EventBus: unsubscribed id={}", id);
      return;
    }
  }
}

void EventBus::Publish(const std::string& event_type, const std::any& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = subscribers_.find(event_type);
  if (it == subscribers_.end()) {
    return;
  }
  for (const auto& sub : it->second) {
    try {
      sub.handler(data);
    } catch (const std::exception& e) {
      spdlog::error("EventBus: handler error for '{}': {}",
                    event_type, e.what());
    }
  }
}

void EventBus::Publish(const std::string& event_type) {
  Publish(event_type, std::any{});
}

}  // namespace loong::core
