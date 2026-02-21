// Copyright 2026 Loong AI NVR Project

#include "network/websocket/websocket_frame.h"
#include "network/websocket/websocket_server.h"

#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <thread>

namespace loong {
namespace network {
namespace {

// ============================================================
// WebSocket Frame Tests
// ============================================================

TEST(WebSocketFrameTest, ComputeAcceptKeyMatchesRfc) {
  // RFC 6455 Section 4.2.2 example.
  std::string client_key = "dGhlIHNhbXBsZSBub25jZQ==";
  std::string expected = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
  EXPECT_EQ(WebSocketFrame::ComputeAcceptKey(client_key), expected);
}

TEST(WebSocketFrameTest, BuildTextFrameShort) {
  std::string text = "hello";
  auto frame = WebSocketFrame::BuildTextFrame(text);
  ASSERT_GE(frame.size(), 7u);
  EXPECT_EQ(frame[0], 0x81);  // FIN + TEXT
  EXPECT_EQ(frame[1], 5);     // payload length = 5
  EXPECT_EQ(std::string(frame.begin() + 2, frame.end()), "hello");
}

TEST(WebSocketFrameTest, BuildTextFrameMedium) {
  std::string text(200, 'x');
  auto frame = WebSocketFrame::BuildTextFrame(text);
  ASSERT_GE(frame.size(), 4u + 200u);
  EXPECT_EQ(frame[0], 0x81);
  EXPECT_EQ(frame[1], 126);  // Extended 16-bit length
}

TEST(WebSocketFrameTest, BuildCloseFrame) {
  auto frame = WebSocketFrame::BuildCloseFrame(1000);
  ASSERT_EQ(frame.size(), 4u);
  EXPECT_EQ(frame[0], 0x88);  // FIN + CLOSE
  EXPECT_EQ(frame[1], 2);
  // 1000 = 0x03E8
  EXPECT_EQ(frame[2], 0x03);
  EXPECT_EQ(frame[3], 0xE8);
}

TEST(WebSocketFrameTest, ParseUnmaskedFrame) {
  // Build a frame manually: FIN + TEXT, len=5, "hello", no mask.
  uint8_t data[] = {0x81, 0x05, 'h', 'e', 'l', 'l', 'o'};
  WsFrame frame;
  size_t consumed = WebSocketFrame::ParseFrame(data, sizeof(data), frame);
  EXPECT_EQ(consumed, 7u);
  EXPECT_TRUE(frame.fin);
  EXPECT_EQ(frame.opcode, WsOpcode::kText);
  EXPECT_FALSE(frame.masked);
  EXPECT_EQ(frame.payload.size(), 5u);
  EXPECT_EQ(std::string(frame.payload.begin(), frame.payload.end()), "hello");
}

TEST(WebSocketFrameTest, ParseMaskedFrame) {
  // Build: FIN + TEXT, MASKED, len=5, mask=0x37FA213D, masked payload.
  std::string text = "hello";
  uint8_t mask[4] = {0x37, 0xFA, 0x21, 0x3D};
  std::vector<uint8_t> data;
  data.push_back(0x81);
  data.push_back(0x85);  // MASK bit + len 5
  data.insert(data.end(), mask, mask + 4);
  for (size_t i = 0; i < text.size(); ++i) {
    data.push_back(static_cast<uint8_t>(text[i]) ^ mask[i % 4]);
  }

  WsFrame frame;
  size_t consumed = WebSocketFrame::ParseFrame(data.data(), data.size(), frame);
  EXPECT_EQ(consumed, data.size());
  EXPECT_TRUE(frame.masked);
  EXPECT_EQ(std::string(frame.payload.begin(), frame.payload.end()), "hello");
}

// ============================================================
// WebSocket Server Tests
// ============================================================

TEST(WebSocketServerTest, StartAndStop) {
  WebSocketServer server("127.0.0.1", 19876);
  EXPECT_TRUE(server.Start());
  EXPECT_EQ(server.ConnectionCount(), 0u);
  server.Stop();
}

TEST(WebSocketServerTest, BroadcastWithNoClients) {
  WebSocketServer server("127.0.0.1", 19877);
  ASSERT_TRUE(server.Start());
  // Should not crash.
  server.Broadcast("test", {{"key", "value"}});
  server.Stop();
}

}  // namespace
}  // namespace network
}  // namespace loong
