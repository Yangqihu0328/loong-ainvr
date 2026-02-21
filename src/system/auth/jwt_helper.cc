// Copyright 2026 Loong AI NVR Project

#include "system/auth/jwt_helper.h"

#include "spdlog/spdlog.h"

#include <chrono>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <utility>

#include "nlohmann/json.hpp"

namespace loong::system {

namespace {

int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace

JwtHelper::JwtHelper(std::string secret, int64_t expiry_seconds)
    : secret_(std::move(secret)), expiry_seconds_(expiry_seconds) {}

std::string JwtHelper::GenerateToken(const UserInfo& user) {
  // Header
  nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};

  // Payload
  int64_t now = Now();
  nlohmann::json payload = {
      {"sub", user.id},
      {"usr", user.username},
      {"rol", UserRoleToString(user.role)},
      {"iat", now},
      {"exp", now + expiry_seconds_},
  };

  std::string header_b64 = Base64UrlEncode(header.dump());
  std::string payload_b64 = Base64UrlEncode(payload.dump());
  std::string data = header_b64 + "." + payload_b64;
  std::string signature = Base64UrlEncode(Sign(data));

  return data + "." + signature;
}

bool JwtHelper::VerifyToken(const std::string& token, JwtClaims& claims) {
  // Split token into 3 parts
  auto first_dot = token.find('.');
  if (first_dot == std::string::npos) return false;

  auto second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string::npos) return false;

  std::string header_b64 = token.substr(0, first_dot);
  std::string payload_b64 =
      token.substr(first_dot + 1, second_dot - first_dot - 1);
  std::string signature_b64 = token.substr(second_dot + 1);

  // Verify signature
  std::string data = header_b64 + "." + payload_b64;
  std::string expected_sig = Base64UrlEncode(Sign(data));
  if (signature_b64 != expected_sig) {
    spdlog::debug("JWT: signature mismatch");
    return false;
  }

  // Decode payload
  try {
    std::string payload_json = Base64UrlDecode(payload_b64);
    auto payload = nlohmann::json::parse(payload_json);

    claims.user_id = payload.value("sub", static_cast<int64_t>(0));
    claims.username = payload.value("usr", std::string(""));
    claims.role = ParseUserRole(payload.value("rol", std::string("viewer")));
    claims.issued_at = payload.value("iat", static_cast<int64_t>(0));
    claims.expires_at = payload.value("exp", static_cast<int64_t>(0));

    // Check expiry
    if (claims.expires_at < Now()) {
      spdlog::debug("JWT: token expired");
      return false;
    }

    return true;
  } catch (const std::exception& e) {
    spdlog::debug("JWT: failed to parse payload: {}", e.what());
    return false;
  }
}

std::string JwtHelper::Sign(const std::string& data) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int result_len = 0;

  HMAC(EVP_sha256(), secret_.data(), static_cast<int>(secret_.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result,
       &result_len);

  return std::string(reinterpret_cast<char*>(result), result_len);
}

std::string JwtHelper::Base64UrlEncode(const std::string& input) {
  static const char kTable[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  std::string output;
  size_t len = input.size();
  output.reserve(((len + 2) / 3) * 4);

  for (size_t i = 0; i < len; i += 3) {
    auto a = static_cast<uint32_t>(static_cast<unsigned char>(input[i]));
    auto b =
        (i + 1 < len)
            ? static_cast<uint32_t>(static_cast<unsigned char>(input[i + 1]))
            : 0U;
    auto c =
        (i + 2 < len)
            ? static_cast<uint32_t>(static_cast<unsigned char>(input[i + 2]))
            : 0U;

    uint32_t triple = (a << 16) | (b << 8) | c;

    output.push_back(kTable[(triple >> 18) & 0x3F]);
    output.push_back(kTable[(triple >> 12) & 0x3F]);

    if (i + 1 < len) {
      output.push_back(kTable[(triple >> 6) & 0x3F]);
    }
    if (i + 2 < len) {
      output.push_back(kTable[triple & 0x3F]);
    }
  }

  return output;
}

std::string JwtHelper::Base64UrlDecode(const std::string& input) {
  // Convert base64url to base64
  std::string b64 = input;
  for (auto& ch : b64) {
    if (ch == '-') {
      ch = '+';
    } else if (ch == '_') {
      ch = '/';
    }
  }
  // Add padding
  while (b64.size() % 4 != 0) {
    b64.push_back('=');
  }

  // Standard base64 decode
  static const unsigned char kDecodeTable[256] = {
      // clang-format off
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255, 62,255,255,255, 63,
       52, 53, 54, 55, 56, 57, 58, 59, 60, 61,255,255,255,  0,255,255,
      255,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
       15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,255,255,255,255,255,
      255, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
       41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,
      // clang-format on
  };

  std::string output;
  output.reserve((b64.size() / 4) * 3);

  for (size_t i = 0; i + 3 < b64.size(); i += 4) {
    auto a = kDecodeTable[static_cast<unsigned char>(b64[i])];
    auto b = kDecodeTable[static_cast<unsigned char>(b64[i + 1])];
    auto c = kDecodeTable[static_cast<unsigned char>(b64[i + 2])];
    auto d = kDecodeTable[static_cast<unsigned char>(b64[i + 3])];

    uint32_t triple =
        (static_cast<uint32_t>(a) << 18) | (static_cast<uint32_t>(b) << 12) |
        (static_cast<uint32_t>(c) << 6) | static_cast<uint32_t>(d);

    output.push_back(static_cast<char>((triple >> 16) & 0xFF));
    if (b64[i + 2] != '=') {
      output.push_back(static_cast<char>((triple >> 8) & 0xFF));
    }
    if (b64[i + 3] != '=') {
      output.push_back(static_cast<char>(triple & 0xFF));
    }
  }

  return output;
}

}  // namespace loong::system
