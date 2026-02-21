// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_WEBSOCKET_WEBSOCKET_FRAME_H_
#define LOONG_NETWORK_WEBSOCKET_WEBSOCKET_FRAME_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace loong::network {

/// WebSocket opcode values (RFC 6455 Section 5.2).
enum class WsOpcode : uint8_t {
  kContinuation = 0x0,
  kText = 0x1,
  kBinary = 0x2,
  kClose = 0x8,
  kPing = 0x9,
  kPong = 0xA,
};

/// A parsed WebSocket frame.
struct WsFrame {
  bool fin = true;
  WsOpcode opcode = WsOpcode::kText;
  bool masked = false;
  uint8_t mask_key[4] = {};
  std::vector<uint8_t> payload;
};

/// Utilities for building and parsing WebSocket frames.
class WebSocketFrame {
 public:
  /// Compute the Sec-WebSocket-Accept header value from the client key.
  static std::string ComputeAcceptKey(const std::string& client_key);

  /// Build an HTTP 101 upgrade response.
  static std::string BuildUpgradeResponse(const std::string& accept_key);

  /// Build a WebSocket text frame (server→client, no mask).
  static std::vector<uint8_t> BuildTextFrame(const std::string& text);

  /// Build a WebSocket pong frame (echo back the ping payload).
  static std::vector<uint8_t> BuildPongFrame(
      const std::vector<uint8_t>& payload);

  /// Build a WebSocket close frame.
  static std::vector<uint8_t> BuildCloseFrame(uint16_t code = 1000);

  /// Try to parse one WebSocket frame from a byte buffer.
  /// Returns the number of bytes consumed, or 0 if not enough data.
  static size_t ParseFrame(const uint8_t* data, size_t len, WsFrame& frame);
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_WEBSOCKET_WEBSOCKET_FRAME_H_
