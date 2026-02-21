// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_NOTIFICATION_MQTT_CHANNEL_H_
#define LOONG_SYSTEM_NOTIFICATION_MQTT_CHANNEL_H_

#include "system/notification/notification_channel.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace loong::system {

/// MQTT connection and publishing configuration.
struct MqttConfig {
  bool enabled = false;
  std::string broker_host = "localhost";
  int broker_port = 1883;
  std::string client_id = "loong-nvr";
  std::string username;
  std::string password;
  bool use_tls = false;

  // Topics
  std::string event_topic = "loong/events";
  std::string status_topic = "loong/status";
  std::string command_topic = "loong/commands";

  // QoS level (0, 1, or 2)
  int qos = 1;

  // Keep-alive interval (seconds)
  int keepalive_sec = 60;
};

/// Callback for MQTT commands received via subscription.
using MqttCommandCallback =
    std::function<void(const std::string& topic, const std::string& payload)>;

/// MQTT notification channel.
///
/// Publishes alarm/event notifications to an MQTT broker.
/// Optionally subscribes to a command topic for remote control.
/// Uses the paho-mqtt C library internally (or a lightweight stub
/// when the library is not available).
class MqttChannel : public NotificationChannel {
 public:
  MqttChannel();
  ~MqttChannel() override;

  /// Configure the MQTT connection.
  void Configure(const MqttConfig& config);

  /// Get current config.
  MqttConfig GetConfig() const;

  /// Connect to the MQTT broker.
  bool Connect();

  /// Disconnect from the broker.
  void Disconnect();

  /// Check if connected.
  bool IsConnected() const;

  /// Publish a message to a topic.
  bool Publish(const std::string& topic, const std::string& payload,
               int qos = -1);

  /// Subscribe to the command topic.
  bool SubscribeCommands(MqttCommandCallback callback);

  // NotificationChannel interface
  std::string Type() const override { return "mqtt"; }
  NotificationResult Test() override;
  NotificationResult Send(const NotificationMessage& msg) override;
  bool IsConfigured() const override;

  // Non-copyable
  MqttChannel(const MqttChannel&) = delete;
  MqttChannel& operator=(const MqttChannel&) = delete;

 private:
  std::string FormatEventPayload(const NotificationMessage& msg) const;

  MqttConfig config_;
  mutable std::mutex mutex_;
  std::atomic<bool> connected_{false};
  MqttCommandCallback command_cb_;

  // Opaque handle for the MQTT client library
  void* client_ = nullptr;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_NOTIFICATION_MQTT_CHANNEL_H_
