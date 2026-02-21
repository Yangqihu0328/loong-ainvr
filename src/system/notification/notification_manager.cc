// Copyright 2026 Loong AI NVR Project

#include "system/notification/notification_manager.h"

#include "core/event_bus/event_bus.h"
#include "spdlog/spdlog.h"

#include <chrono>

namespace loong::system {

NotificationManager::NotificationManager()
    : smtp_channel_(std::make_unique<SmtpChannel>(SmtpConfig{})),
      webhook_channel_(std::make_unique<WebhookChannel>(WebhookConfig{})),
      mqtt_channel_(std::make_unique<MqttChannel>()) {}

NotificationManager::~NotificationManager() { Stop(); }

void NotificationManager::Start() {
  if (running_) return;
  running_ = true;

  auto& bus = core::EventBus::Instance();
  event_sub_id_ =
      bus.Subscribe("alarm.triggered", [this](const std::any& data) {
        if (!running_) return;
        try {
          auto record = std::any_cast<AlarmRecord>(data);
          OnAlarmTriggered(record);
        } catch (const std::bad_any_cast&) {
          // Ignore malformed events
        }
      });

  spdlog::info("NotificationManager: started (SMTP={}, Webhook={})",
               smtp_channel_->IsConfigured(), webhook_channel_->IsConfigured());
}

void NotificationManager::Stop() {
  if (!running_) return;
  running_ = false;

  if (event_sub_id_ != 0) {
    core::EventBus::Instance().Unsubscribe(event_sub_id_);
    event_sub_id_ = 0;
  }

  spdlog::info("NotificationManager: stopped");
}

void NotificationManager::ConfigureSmtp(const SmtpConfig& config) {
  smtp_channel_->UpdateConfig(config);
  spdlog::info("NotificationManager: SMTP configured (enabled={}, host={})",
               config.enabled, config.host);
}

void NotificationManager::ConfigureWebhook(const WebhookConfig& config) {
  webhook_channel_->UpdateConfig(config);
  spdlog::info("NotificationManager: Webhook configured (enabled={}, url={})",
               config.enabled, config.url);
}

SmtpConfig NotificationManager::GetSmtpConfig() const {
  return smtp_channel_->GetConfig();
}

WebhookConfig NotificationManager::GetWebhookConfig() const {
  return webhook_channel_->GetConfig();
}

NotificationResult NotificationManager::TestSmtp() {
  auto result = smtp_channel_->Test();
  NotificationMessage msg;
  msg.title = "SMTP Test";
  msg.severity = "info";
  AddHistory("smtp", msg, result);
  return result;
}

NotificationResult NotificationManager::TestWebhook() {
  auto result = webhook_channel_->Test();
  NotificationMessage msg;
  msg.title = "Webhook Test";
  msg.severity = "info";
  AddHistory("webhook", msg, result);
  return result;
}

void NotificationManager::ConfigureMqtt(const MqttConfig& config) {
  mqtt_channel_->Configure(config);
  if (config.enabled) {
    mqtt_channel_->Connect();
  } else {
    mqtt_channel_->Disconnect();
  }
  spdlog::info(
      "NotificationManager: MQTT configured (enabled={}, broker={}:{})",
      config.enabled, config.broker_host, config.broker_port);
}

MqttConfig NotificationManager::GetMqttConfig() const {
  return mqtt_channel_->GetConfig();
}

NotificationResult NotificationManager::TestMqtt() {
  auto result = mqtt_channel_->Test();
  NotificationMessage msg;
  msg.title = "MQTT Test";
  msg.severity = "info";
  AddHistory("mqtt", msg, result);
  return result;
}

void NotificationManager::Notify(const NotificationMessage& msg) {
  if (smtp_channel_->IsConfigured()) {
    auto result = smtp_channel_->Send(msg);
    AddHistory("smtp", msg, result);
    if (!result.success) {
      spdlog::warn("NotificationManager: SMTP send failed: {}",
                   result.error_message);
    }
  }

  if (webhook_channel_->IsConfigured()) {
    auto result = webhook_channel_->Send(msg);
    AddHistory("webhook", msg, result);
    if (!result.success) {
      spdlog::warn("NotificationManager: Webhook send failed: {}",
                   result.error_message);
    }
  }

  if (mqtt_channel_->IsConfigured()) {
    auto result = mqtt_channel_->Send(msg);
    AddHistory("mqtt", msg, result);
    if (!result.success) {
      spdlog::warn("NotificationManager: MQTT send failed: {}",
                   result.error_message);
    }
  }
}

void NotificationManager::OnAlarmTriggered(const AlarmRecord& alarm) {
  NotificationMessage msg;
  msg.title = "Alarm: " + alarm.event_type + " on CH" +
              std::to_string(alarm.channel_id);
  msg.body = "Detection event '" + alarm.event_type +
             "' triggered on "
             "channel " +
             std::to_string(alarm.channel_id) + " with confidence " +
             std::to_string(static_cast<int>(alarm.confidence * 100)) + "%.";
  msg.severity = AlarmSeverityToString(alarm.severity);
  msg.channel_id = alarm.channel_id;
  msg.event_type = alarm.event_type;
  msg.confidence = alarm.confidence;
  msg.timestamp = alarm.triggered_at;

  Notify(msg);
}

void NotificationManager::AddHistory(const std::string& channel_type,
                                     const NotificationMessage& msg,
                                     const NotificationResult& result) {
  std::lock_guard<std::mutex> lock(history_mutex_);

  NotificationRecord record;
  record.id = next_history_id_++;
  record.channel_type = channel_type;
  record.title = msg.title;
  record.severity = msg.severity;
  record.success = result.success;
  record.error_message = result.error_message;
  record.sent_at = NowMs();

  history_.push_back(record);

  while (history_.size() > kMaxHistorySize) {
    history_.pop_front();
  }
}

std::vector<NotificationRecord> NotificationManager::GetHistory(
    int limit) const {
  std::lock_guard<std::mutex> lock(history_mutex_);

  auto count = static_cast<size_t>(limit);
  if (count > history_.size()) count = history_.size();

  std::vector<NotificationRecord> result;
  result.reserve(count);

  auto it = history_.rbegin();
  for (size_t i = 0; i < count && it != history_.rend(); ++i, ++it) {
    result.push_back(*it);
  }
  return result;
}

void NotificationManager::ClearHistory() {
  std::lock_guard<std::mutex> lock(history_mutex_);
  history_.clear();
}

int64_t NotificationManager::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace loong::system
