// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_MANAGER_H_
#define LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_MANAGER_H_

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "system/alarm/alarm_manager.h"
#include "system/notification/notification_channel.h"
#include "system/notification/smtp_channel.h"
#include "system/notification/webhook_channel.h"
#include "system/notification/mqtt_channel.h"

namespace loong::system {

/// Record of a sent notification (for history tracking).
struct NotificationRecord {
  int64_t id = 0;
  std::string channel_type;  // "smtp" or "webhook"
  std::string title;
  std::string severity;
  bool success = false;
  std::string error_message;
  int64_t sent_at = 0;        // Unix timestamp (ms)
};

/// Manages notification channels and dispatches alarm notifications.
///
/// Subscribes to "alarm.triggered" events on the EventBus and forwards
/// them to configured notification channels (SMTP, Webhook).
/// Maintains a bounded in-memory history of sent notifications.
class NotificationManager {
 public:
  static constexpr size_t kMaxHistorySize = 200;

  NotificationManager();
  ~NotificationManager();

  /// Start listening for alarm events.
  void Start();

  /// Stop listening.
  void Stop();

  // ---- Channel configuration ----

  /// Configure the SMTP notification channel.
  void ConfigureSmtp(const SmtpConfig& config);

  /// Configure the Webhook notification channel.
  void ConfigureWebhook(const WebhookConfig& config);

  /// Get current SMTP config.
  SmtpConfig GetSmtpConfig() const;

  /// Get current Webhook config.
  WebhookConfig GetWebhookConfig() const;

  /// Configure the MQTT notification channel.
  void ConfigureMqtt(const MqttConfig& config);

  /// Get current MQTT config.
  MqttConfig GetMqttConfig() const;

  // ---- Testing ----

  /// Test the SMTP channel.
  NotificationResult TestSmtp();

  /// Test the Webhook channel.
  NotificationResult TestWebhook();

  /// Test the MQTT channel.
  NotificationResult TestMqtt();

  // ---- Manual send ----

  /// Manually send a notification through all enabled channels.
  void Notify(const NotificationMessage& msg);

  // ---- History ----

  /// Get recent notification history.
  std::vector<NotificationRecord> GetHistory(int limit = 50) const;

  /// Clear notification history.
  void ClearHistory();

  // Non-copyable
  NotificationManager(const NotificationManager&) = delete;
  NotificationManager& operator=(const NotificationManager&) = delete;

 private:
  void OnAlarmTriggered(const AlarmRecord& alarm);
  void AddHistory(const std::string& channel_type,
                  const NotificationMessage& msg,
                  const NotificationResult& result);
  static int64_t NowMs();

  std::unique_ptr<SmtpChannel> smtp_channel_;
  std::unique_ptr<WebhookChannel> webhook_channel_;
  std::unique_ptr<MqttChannel> mqtt_channel_;

  mutable std::mutex history_mutex_;
  std::deque<NotificationRecord> history_;
  int64_t next_history_id_ = 1;

  std::atomic<bool> running_{false};
  uint64_t event_sub_id_ = 0;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_MANAGER_H_
