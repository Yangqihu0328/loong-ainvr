// Copyright 2026 Loong AI NVR Project

#include "system/notification/mqtt_channel.h"

#include <chrono>

#include <spdlog/spdlog.h>

// When LOONG_HAS_PAHO_MQTT is defined, use the real paho-mqtt-c library.
// Otherwise, provide a stub that logs warnings but compiles cleanly.
// This allows the code to be built without the optional MQTT dependency.

#ifdef LOONG_HAS_PAHO_MQTT
#include <MQTTClient.h>
#endif

namespace loong::system {

MqttChannel::MqttChannel() = default;

MqttChannel::~MqttChannel() { Disconnect(); }

void MqttChannel::Configure(const MqttConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  bool was_connected = connected_.load();
  if (was_connected) {
    Disconnect();
  }
  config_ = config;
}

MqttConfig MqttChannel::GetConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

bool MqttChannel::Connect() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_.load()) return true;

  if (config_.broker_host.empty()) {
    spdlog::warn("MqttChannel: broker_host is empty");
    return false;
  }

#ifdef LOONG_HAS_PAHO_MQTT
  std::string uri = (config_.use_tls ? "ssl://" : "tcp://") +
                    config_.broker_host + ":" +
                    std::to_string(config_.broker_port);

  MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
  opts.keepAliveInterval = config_.keepalive_sec;
  opts.cleansession = 1;

  if (!config_.username.empty()) {
    opts.username = config_.username.c_str();
    opts.password = config_.password.c_str();
  }

  int rc = MQTTClient_create(
      reinterpret_cast<MQTTClient*>(&client_), uri.c_str(),
      config_.client_id.c_str(), MQTTCLIENT_PERSISTENCE_NONE, nullptr);
  if (rc != MQTTCLIENT_SUCCESS) {
    spdlog::error("MqttChannel: create failed (rc={})", rc);
    return false;
  }

  rc = MQTTClient_connect(static_cast<MQTTClient>(client_), &opts);
  if (rc != MQTTCLIENT_SUCCESS) {
    spdlog::error("MqttChannel: connect to {} failed (rc={})", uri, rc);
    MQTTClient_destroy(reinterpret_cast<MQTTClient*>(&client_));
    client_ = nullptr;
    return false;
  }

  connected_.store(true);
  spdlog::info("MqttChannel: connected to {}", uri);
  return true;
#else
  spdlog::info("MqttChannel: simulated connect to {}:{}",
               config_.broker_host, config_.broker_port);
  connected_.store(true);
  return true;
#endif
}

void MqttChannel::Disconnect() {
  if (!connected_.load()) return;

#ifdef LOONG_HAS_PAHO_MQTT
  if (client_) {
    MQTTClient_disconnect(static_cast<MQTTClient>(client_), 1000);
    MQTTClient_destroy(reinterpret_cast<MQTTClient*>(&client_));
    client_ = nullptr;
  }
#endif

  connected_.store(false);
  spdlog::info("MqttChannel: disconnected");
}

bool MqttChannel::IsConnected() const { return connected_.load(); }

bool MqttChannel::Publish(const std::string& topic,
                           const std::string& payload, int qos) {
  if (!connected_.load()) {
    spdlog::warn("MqttChannel: not connected, cannot publish");
    return false;
  }

  int actual_qos = (qos >= 0) ? qos : config_.qos;

#ifdef LOONG_HAS_PAHO_MQTT
  MQTTClient_message msg = MQTTClient_message_initializer;
  msg.payload = const_cast<char*>(payload.data());
  msg.payloadlen = static_cast<int>(payload.size());
  msg.qos = actual_qos;
  msg.retained = 0;

  MQTTClient_deliveryToken token;
  int rc = MQTTClient_publishMessage(
      static_cast<MQTTClient>(client_), topic.c_str(), &msg, &token);
  if (rc != MQTTCLIENT_SUCCESS) {
    spdlog::error("MqttChannel: publish to '{}' failed (rc={})", topic, rc);
    return false;
  }
  MQTTClient_waitForCompletion(static_cast<MQTTClient>(client_), token, 5000);
  return true;
#else
  (void)actual_qos;
  spdlog::debug("MqttChannel: [stub] publish to '{}' ({} bytes)",
                topic, payload.size());
  return true;
#endif
}

bool MqttChannel::SubscribeCommands(MqttCommandCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  command_cb_ = std::move(callback);

  if (!connected_.load()) return false;

#ifdef LOONG_HAS_PAHO_MQTT
  int rc = MQTTClient_subscribe(
      static_cast<MQTTClient>(client_),
      config_.command_topic.c_str(), config_.qos);
  if (rc != MQTTCLIENT_SUCCESS) {
    spdlog::error("MqttChannel: subscribe to '{}' failed (rc={})",
                  config_.command_topic, rc);
    return false;
  }
  spdlog::info("MqttChannel: subscribed to '{}'", config_.command_topic);
  return true;
#else
  spdlog::info("MqttChannel: [stub] subscribe to '{}'",
               config_.command_topic);
  return true;
#endif
}

NotificationResult MqttChannel::Test() {
  NotificationResult result;

  if (!IsConfigured()) {
    result.error_message = "MQTT not configured";
    return result;
  }

  if (!connected_.load()) {
    if (!Connect()) {
      result.error_message = "failed to connect to broker";
      return result;
    }
  }

  bool ok = Publish(config_.status_topic,
                     R"({"type":"test","source":"loong-nvr"})");
  result.success = ok;
  if (!ok) result.error_message = "publish failed";
  return result;
}

NotificationResult MqttChannel::Send(const NotificationMessage& msg) {
  NotificationResult result;

  if (!connected_.load()) {
    if (!Connect()) {
      result.error_message = "not connected";
      return result;
    }
  }

  std::string payload = FormatEventPayload(msg);
  bool ok = Publish(config_.event_topic, payload);
  result.success = ok;
  if (!ok) result.error_message = "publish failed";
  return result;
}

bool MqttChannel::IsConfigured() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_.enabled && !config_.broker_host.empty();
}

std::string MqttChannel::FormatEventPayload(
    const NotificationMessage& msg) const {
  auto now = std::chrono::system_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch())
                .count();

  return "{\"title\":\"" + msg.title + "\","
         "\"body\":\"" + msg.body + "\","
         "\"severity\":\"" + msg.severity + "\","
         "\"channel_id\":" + std::to_string(msg.channel_id) + ","
         "\"event_type\":\"" + msg.event_type + "\","
         "\"confidence\":" + std::to_string(msg.confidence) + ","
         "\"timestamp\":" + std::to_string(ms) + "}";
}

}  // namespace loong::system
