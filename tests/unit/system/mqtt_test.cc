// Copyright 2026 Loong AI NVR Project
// Unit tests for MQTT Channel (FEAT-6.2)

#include <gtest/gtest.h>

#include "system/notification/mqtt_channel.h"

namespace loong::system {
namespace {

// ============================================================
// MqttConfig Tests
// ============================================================

TEST(MqttConfigTest, Defaults) {
  MqttConfig config;
  EXPECT_FALSE(config.enabled);
  EXPECT_EQ(config.broker_host, "localhost");
  EXPECT_EQ(config.broker_port, 1883);
  EXPECT_EQ(config.client_id, "loong-nvr");
  EXPECT_TRUE(config.username.empty());
  EXPECT_FALSE(config.use_tls);
  EXPECT_EQ(config.event_topic, "loong/events");
  EXPECT_EQ(config.status_topic, "loong/status");
  EXPECT_EQ(config.command_topic, "loong/commands");
  EXPECT_EQ(config.qos, 1);
  EXPECT_EQ(config.keepalive_sec, 60);
}

// ============================================================
// MqttChannel Tests (using stub/simulated mode)
// ============================================================

class MqttChannelTest : public ::testing::Test {
 protected:
  void SetUp() override {
    channel_ = std::make_unique<MqttChannel>();
  }

  std::unique_ptr<MqttChannel> channel_;
};

TEST_F(MqttChannelTest, TypeIsMqtt) {
  EXPECT_EQ(channel_->Type(), "mqtt");
}

TEST_F(MqttChannelTest, NotConfiguredByDefault) {
  EXPECT_FALSE(channel_->IsConfigured());
}

TEST_F(MqttChannelTest, ConfigureEnables) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "mqtt.example.com";
  channel_->Configure(config);
  EXPECT_TRUE(channel_->IsConfigured());
}

TEST_F(MqttChannelTest, GetConfigReturnsSet) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "test-host";
  config.broker_port = 8883;
  config.use_tls = true;
  channel_->Configure(config);

  auto got = channel_->GetConfig();
  EXPECT_EQ(got.broker_host, "test-host");
  EXPECT_EQ(got.broker_port, 8883);
  EXPECT_TRUE(got.use_tls);
}

TEST_F(MqttChannelTest, NotConnectedByDefault) {
  EXPECT_FALSE(channel_->IsConnected());
}

TEST_F(MqttChannelTest, ConnectWithEmptyHost) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "";
  channel_->Configure(config);
  EXPECT_FALSE(channel_->Connect());
}

TEST_F(MqttChannelTest, StubConnectSucceeds) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);

  // In stub mode (no paho-mqtt), Connect() simulates success
  bool ok = channel_->Connect();
  EXPECT_TRUE(ok);
  EXPECT_TRUE(channel_->IsConnected());
}

TEST_F(MqttChannelTest, StubPublish) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);
  channel_->Connect();

  bool ok = channel_->Publish("test/topic", "{\"hello\":\"world\"}");
  EXPECT_TRUE(ok);
}

TEST_F(MqttChannelTest, PublishWithoutConnect) {
  EXPECT_FALSE(channel_->Publish("topic", "payload"));
}

TEST_F(MqttChannelTest, Disconnect) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);
  channel_->Connect();
  EXPECT_TRUE(channel_->IsConnected());

  channel_->Disconnect();
  EXPECT_FALSE(channel_->IsConnected());
}

TEST_F(MqttChannelTest, TestNotConfigured) {
  auto result = channel_->Test();
  EXPECT_FALSE(result.success);
  EXPECT_FALSE(result.error_message.empty());
}

TEST_F(MqttChannelTest, TestConfigured) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);

  auto result = channel_->Test();
  EXPECT_TRUE(result.success);
}

TEST_F(MqttChannelTest, SendNotification) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);
  channel_->Connect();

  NotificationMessage msg;
  msg.title = "Test Alert";
  msg.body = "Person detected";
  msg.severity = "warning";
  msg.channel_id = 1;
  msg.event_type = "person";
  msg.confidence = 0.95F;

  auto result = channel_->Send(msg);
  EXPECT_TRUE(result.success);
}

TEST_F(MqttChannelTest, SubscribeCommands) {
  MqttConfig config;
  config.enabled = true;
  config.broker_host = "localhost";
  channel_->Configure(config);
  channel_->Connect();

  bool callback_called = false;
  bool ok = channel_->SubscribeCommands(
      [&](const std::string&, const std::string&) {
        callback_called = true;
      });
  EXPECT_TRUE(ok);
}

}  // namespace
}  // namespace loong::system
