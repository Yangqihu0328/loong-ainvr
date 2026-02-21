// Copyright 2026 Loong AI NVR Project

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "system/notification/notification_channel.h"
#include "system/notification/notification_manager.h"
#include "system/notification/smtp_channel.h"
#include "system/notification/webhook_channel.h"

namespace loong::system {

// ============================================================
// SmtpChannel Tests (config-only, no actual network)
// ============================================================

TEST(SmtpChannelTest, NotConfiguredByDefault) {
  SmtpConfig config;
  SmtpChannel channel(config);
  EXPECT_FALSE(channel.IsConfigured());
}

TEST(SmtpChannelTest, ConfiguredWhenEnabled) {
  SmtpConfig config;
  config.enabled = true;
  config.host = "smtp.example.com";
  config.from_address = "test@example.com";
  config.to_addresses = {"user@example.com"};
  SmtpChannel channel(config);
  EXPECT_TRUE(channel.IsConfigured());
}

TEST(SmtpChannelTest, NotConfiguredWithoutRecipients) {
  SmtpConfig config;
  config.enabled = true;
  config.host = "smtp.example.com";
  config.from_address = "test@example.com";
  SmtpChannel channel(config);
  EXPECT_FALSE(channel.IsConfigured());
}

TEST(SmtpChannelTest, TypeIsSmtp) {
  SmtpChannel channel(SmtpConfig{});
  EXPECT_EQ(channel.Type(), "smtp");
}

TEST(SmtpChannelTest, SendFailsWhenNotConfigured) {
  SmtpChannel channel(SmtpConfig{});
  NotificationMessage msg;
  msg.title = "Test";
  auto result = channel.Send(msg);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "SMTP not configured");
}

TEST(SmtpChannelTest, UpdateConfig) {
  SmtpChannel channel(SmtpConfig{});
  EXPECT_FALSE(channel.IsConfigured());

  SmtpConfig new_config;
  new_config.enabled = true;
  new_config.host = "mail.test.com";
  new_config.from_address = "a@b.c";
  new_config.to_addresses = {"d@e.f"};
  channel.UpdateConfig(new_config);
  EXPECT_TRUE(channel.IsConfigured());
  EXPECT_EQ(channel.GetConfig().host, "mail.test.com");
}

// ============================================================
// WebhookChannel Tests (config-only, no actual network)
// ============================================================

TEST(WebhookChannelTest, NotConfiguredByDefault) {
  WebhookChannel channel(WebhookConfig{});
  EXPECT_FALSE(channel.IsConfigured());
}

TEST(WebhookChannelTest, ConfiguredWhenEnabled) {
  WebhookConfig config;
  config.enabled = true;
  config.url = "https://example.com/hook";
  WebhookChannel channel(config);
  EXPECT_TRUE(channel.IsConfigured());
}

TEST(WebhookChannelTest, NotConfiguredWithoutUrl) {
  WebhookConfig config;
  config.enabled = true;
  WebhookChannel channel(config);
  EXPECT_FALSE(channel.IsConfigured());
}

TEST(WebhookChannelTest, TypeIsWebhook) {
  WebhookChannel channel(WebhookConfig{});
  EXPECT_EQ(channel.Type(), "webhook");
}

TEST(WebhookChannelTest, SendFailsWhenNotConfigured) {
  WebhookChannel channel(WebhookConfig{});
  NotificationMessage msg;
  msg.title = "Test";
  auto result = channel.Send(msg);
  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "webhook not configured");
}

TEST(WebhookChannelTest, UpdateConfig) {
  WebhookChannel channel(WebhookConfig{});
  EXPECT_FALSE(channel.IsConfigured());

  WebhookConfig new_config;
  new_config.enabled = true;
  new_config.url = "https://test.com/webhook";
  channel.UpdateConfig(new_config);
  EXPECT_TRUE(channel.IsConfigured());
  EXPECT_EQ(channel.GetConfig().url, "https://test.com/webhook");
}

// ============================================================
// NotificationManager Tests
// ============================================================

TEST(NotificationManagerTest, StartAndStop) {
  NotificationManager mgr;
  mgr.Start();
  mgr.Stop();
}

TEST(NotificationManagerTest, ConfigureSmtp) {
  NotificationManager mgr;
  SmtpConfig config;
  config.enabled = true;
  config.host = "smtp.test.com";
  config.from_address = "nvr@test.com";
  config.to_addresses = {"admin@test.com"};
  mgr.ConfigureSmtp(config);

  auto retrieved = mgr.GetSmtpConfig();
  EXPECT_TRUE(retrieved.enabled);
  EXPECT_EQ(retrieved.host, "smtp.test.com");
}

TEST(NotificationManagerTest, ConfigureWebhook) {
  NotificationManager mgr;
  WebhookConfig config;
  config.enabled = true;
  config.url = "https://test.com/hook";
  mgr.ConfigureWebhook(config);

  auto retrieved = mgr.GetWebhookConfig();
  EXPECT_TRUE(retrieved.enabled);
  EXPECT_EQ(retrieved.url, "https://test.com/hook");
}

TEST(NotificationManagerTest, HistoryEmpty) {
  NotificationManager mgr;
  auto history = mgr.GetHistory();
  EXPECT_TRUE(history.empty());
}

TEST(NotificationManagerTest, TestSmtpRecordsHistory) {
  NotificationManager mgr;
  mgr.TestSmtp();  // Will fail since not configured
  auto history = mgr.GetHistory();
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].channel_type, "smtp");
  EXPECT_FALSE(history[0].success);
}

TEST(NotificationManagerTest, TestWebhookRecordsHistory) {
  NotificationManager mgr;
  mgr.TestWebhook();  // Will fail since not configured
  auto history = mgr.GetHistory();
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history[0].channel_type, "webhook");
  EXPECT_FALSE(history[0].success);
}

TEST(NotificationManagerTest, ClearHistory) {
  NotificationManager mgr;
  mgr.TestSmtp();
  mgr.TestWebhook();
  EXPECT_EQ(mgr.GetHistory().size(), 2u);
  mgr.ClearHistory();
  EXPECT_TRUE(mgr.GetHistory().empty());
}

TEST(NotificationManagerTest, HistoryBounded) {
  NotificationManager mgr;
  for (int i = 0; i < 250; ++i) {
    mgr.TestSmtp();
  }
  auto history = mgr.GetHistory(300);
  EXPECT_LE(history.size(), NotificationManager::kMaxHistorySize);
}

TEST(NotificationManagerTest, NotifyNoChannelsConfigured) {
  NotificationManager mgr;
  NotificationMessage msg;
  msg.title = "Test Alert";
  msg.body = "Something happened.";
  msg.severity = "warning";
  mgr.Notify(msg);
  auto history = mgr.GetHistory();
  EXPECT_TRUE(history.empty());
}

}  // namespace loong::system
