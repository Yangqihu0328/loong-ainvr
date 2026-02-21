// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_CHANNEL_H_
#define LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_CHANNEL_H_

#include <cstdint>
#include <string>

namespace loong::system {

/// A notification message to be sent through channels.
struct NotificationMessage {
  std::string title;
  std::string body;
  std::string severity;    // "info", "warning", "critical"
  int channel_id = -1;
  std::string event_type;
  float confidence = 0.0F;
  int64_t timestamp = 0;
  std::string screenshot_path;
};

/// Send result returned by notification channels.
struct NotificationResult {
  bool success = false;
  std::string error_message;
};

/// Abstract interface for notification delivery channels.
class NotificationChannel {
 public:
  virtual ~NotificationChannel() = default;

  /// Get the channel type name (e.g. "smtp", "webhook").
  virtual std::string Type() const = 0;

  /// Test the channel configuration (send a test message).
  virtual NotificationResult Test() = 0;

  /// Send a notification through this channel.
  virtual NotificationResult Send(const NotificationMessage& msg) = 0;

  /// Check if the channel is properly configured.
  virtual bool IsConfigured() const = 0;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_NOTIFICATION_NOTIFICATION_CHANNEL_H_
