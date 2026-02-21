// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_NOTIFICATION_WEBHOOK_CHANNEL_H_
#define LOONG_SYSTEM_NOTIFICATION_WEBHOOK_CHANNEL_H_

#include "system/notification/notification_channel.h"

#include <string>

namespace loong::system {

/// Webhook configuration for HTTP POST notifications.
struct WebhookConfig {
  bool enabled = false;
  std::string url;
  std::string secret;  // Optional HMAC secret for payload signing
  std::string content_type = "application/json";
  int timeout_sec = 10;
  int retry_count = 2;
};

/// Webhook notification channel — sends alarm notifications as HTTP POST.
///
/// Sends a JSON payload to the configured URL. If a secret is configured,
/// the payload is signed with HMAC-SHA256 in the X-Signature header.
class WebhookChannel : public NotificationChannel {
 public:
  explicit WebhookChannel(const WebhookConfig& config);
  ~WebhookChannel() override = default;

  std::string Type() const override { return "webhook"; }
  NotificationResult Test() override;
  NotificationResult Send(const NotificationMessage& msg) override;
  bool IsConfigured() const override;

  void UpdateConfig(const WebhookConfig& config);
  WebhookConfig GetConfig() const { return config_; }

 private:
  NotificationResult PostJson(const std::string& json_body);
  static std::string BuildJsonPayload(const NotificationMessage& msg);
  static std::string HmacSha256(const std::string& key,
                                const std::string& data);

  WebhookConfig config_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_NOTIFICATION_WEBHOOK_CHANNEL_H_
