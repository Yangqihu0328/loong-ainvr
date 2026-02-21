// Copyright 2026 Loong AI NVR Project

#include "network/websocket/websocket_frame.h"

#include <cstring>
#include <sstream>

#include <openssl/evp.h>
#include <openssl/sha.h>

namespace loong::network {

namespace {

// Base64 encode a binary blob.
std::string Base64Encode(const unsigned char* data, size_t len) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  for (size_t i = 0; i < len; i += 3) {
    unsigned val = static_cast<unsigned>(data[i]) << 16;
    if (i + 1 < len) val |= static_cast<unsigned>(data[i + 1]) << 8;
    if (i + 2 < len) val |= static_cast<unsigned>(data[i + 2]);
    out.push_back(kTable[(val >> 18) & 0x3F]);
    out.push_back(kTable[(val >> 12) & 0x3F]);
    out.push_back((i + 1 < len) ? kTable[(val >> 6) & 0x3F] : '=');
    out.push_back((i + 2 < len) ? kTable[val & 0x3F] : '=');
  }
  return out;
}

}  // namespace

std::string WebSocketFrame::ComputeAcceptKey(const std::string& client_key) {
  // RFC 6455: concatenate client key + magic GUID, then SHA-1 + Base64.
  const std::string kMagicGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string combined = client_key + kMagicGuid;

  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(combined.data()),
       combined.size(), hash);

  return Base64Encode(hash, SHA_DIGEST_LENGTH);
}

std::string WebSocketFrame::BuildUpgradeResponse(
    const std::string& accept_key) {
  std::ostringstream ss;
  ss << "HTTP/1.1 101 Switching Protocols\r\n"
     << "Upgrade: websocket\r\n"
     << "Connection: Upgrade\r\n"
     << "Sec-WebSocket-Accept: " << accept_key << "\r\n"
     << "\r\n";
  return ss.str();
}

std::vector<uint8_t> WebSocketFrame::BuildTextFrame(const std::string& text) {
  std::vector<uint8_t> frame;
  size_t len = text.size();

  // FIN=1, opcode=text
  frame.push_back(0x81);

  // Payload length (no mask for server frames)
  if (len < 126) {
    frame.push_back(static_cast<uint8_t>(len));
  } else if (len <= 0xFFFF) {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  } else {
    frame.push_back(127);
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFF));
    }
  }

  frame.insert(frame.end(), text.begin(), text.end());
  return frame;
}

std::vector<uint8_t> WebSocketFrame::BuildPongFrame(
    const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> frame;
  frame.push_back(0x8A);  // FIN=1, opcode=pong

  size_t len = payload.size();
  if (len < 126) {
    frame.push_back(static_cast<uint8_t>(len));
  } else {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(len & 0xFF));
  }
  frame.insert(frame.end(), payload.begin(), payload.end());
  return frame;
}

std::vector<uint8_t> WebSocketFrame::BuildCloseFrame(uint16_t code) {
  std::vector<uint8_t> frame;
  frame.push_back(0x88);  // FIN=1, opcode=close
  frame.push_back(2);     // payload = 2 bytes (status code)
  frame.push_back(static_cast<uint8_t>((code >> 8) & 0xFF));
  frame.push_back(static_cast<uint8_t>(code & 0xFF));
  return frame;
}

size_t WebSocketFrame::ParseFrame(const uint8_t* data, size_t len,
                                  WsFrame& frame) {
  if (len < 2) return 0;

  size_t pos = 0;
  frame.fin = (data[0] & 0x80) != 0;
  frame.opcode = static_cast<WsOpcode>(data[0] & 0x0F);
  frame.masked = (data[1] & 0x80) != 0;

  uint64_t payload_len = data[1] & 0x7F;
  pos = 2;

  if (payload_len == 126) {
    if (len < 4) return 0;
    payload_len = (static_cast<uint64_t>(data[2]) << 8) |
                  static_cast<uint64_t>(data[3]);
    pos = 4;
  } else if (payload_len == 127) {
    if (len < 10) return 0;
    payload_len = 0;
    for (int i = 0; i < 8; ++i) {
      payload_len = (payload_len << 8) | static_cast<uint64_t>(data[2 + i]);
    }
    pos = 10;
  }

  if (frame.masked) {
    if (len < pos + 4) return 0;
    std::memcpy(frame.mask_key, data + pos, 4);
    pos += 4;
  }

  if (len < pos + payload_len) return 0;

  frame.payload.resize(static_cast<size_t>(payload_len));
  std::memcpy(frame.payload.data(), data + pos,
              static_cast<size_t>(payload_len));

  // Unmask payload
  if (frame.masked) {
    for (size_t i = 0; i < frame.payload.size(); ++i) {
      frame.payload[i] ^= frame.mask_key[i % 4];
    }
  }

  return pos + static_cast<size_t>(payload_len);
}

}  // namespace loong::network
