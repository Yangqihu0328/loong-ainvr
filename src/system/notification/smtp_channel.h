// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_NOTIFICATION_SMTP_CHANNEL_H_
#define LOONG_SYSTEM_NOTIFICATION_SMTP_CHANNEL_H_

#include "system/notification/notification_channel.h"

#include <string>
#include <vector>

namespace loong::system {

/// SMTP configuration for email notifications.
struct SmtpConfig {
  bool enabled = false;
  std::string host = "smtp.example.com";
  int port = 587;
  bool use_tls = true;
  std::string username;
  std::string password;
  std::string from_address;
  std::string from_name = "Loong AI NVR";
  std::vector<std::string> to_addresses;
};

/// SMTP notification channel — sends alarm notifications as emails.
///
/// Supports plain SMTP and STARTTLS. Uses OpenSSL for TLS connections.
/// Sends multipart MIME emails with HTML body.
class SmtpChannel : public NotificationChannel {
 public:
  explicit SmtpChannel(const SmtpConfig& config);
  ~SmtpChannel() override = default;

  std::string Type() const override { return "smtp"; }
  NotificationResult Test() override;
  NotificationResult Send(const NotificationMessage& msg) override;
  bool IsConfigured() const override;

  void UpdateConfig(const SmtpConfig& config);
  SmtpConfig GetConfig() const { return config_; }

 private:
  NotificationResult SendEmail(const std::string& subject,
                               const std::string& html_body);
  static std::string FormatHtmlBody(const NotificationMessage& msg);
  static std::string Base64Encode(const std::string& input);

  SmtpConfig config_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_NOTIFICATION_SMTP_CHANNEL_H_
