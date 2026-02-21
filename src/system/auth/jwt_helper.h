// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_AUTH_JWT_HELPER_H_
#define LOONG_SYSTEM_AUTH_JWT_HELPER_H_

#include <cstdint>
#include <string>

#include "system/auth/user_store.h"

namespace loong::system {

/// Claims extracted from a JWT token.
struct JwtClaims {
  int64_t user_id = 0;
  std::string username;
  UserRole role = UserRole::kViewer;
  int64_t issued_at = 0;   // Unix timestamp (seconds)
  int64_t expires_at = 0;  // Unix timestamp (seconds)
};

/// Minimal JWT (JSON Web Token) helper using HMAC-SHA256.
///
/// Token format: base64url(header).base64url(payload).base64url(signature)
/// This is a compact implementation suitable for embedded systems.
class JwtHelper {
 public:
  /// Create a JWT helper with the given secret key.
  /// @param secret HMAC signing secret (should be >= 32 bytes for security).
  /// @param expiry_seconds Token validity duration in seconds (default 24h).
  explicit JwtHelper(std::string  secret,
                     int64_t expiry_seconds = 86400);

  /// Generate a JWT token for the given user.
  std::string GenerateToken(const UserInfo& user);

  /// Verify and decode a JWT token.
  /// Returns true if the token is valid and not expired.
  bool VerifyToken(const std::string& token, JwtClaims& claims);

  // Non-copyable
  JwtHelper(const JwtHelper&) = delete;
  JwtHelper& operator=(const JwtHelper&) = delete;

 private:
  /// HMAC-SHA256 sign data with the secret key.
  std::string Sign(const std::string& data);

  /// Base64url encode.
  static std::string Base64UrlEncode(const std::string& input);

  /// Base64url decode.
  static std::string Base64UrlDecode(const std::string& input);

  std::string secret_;
  int64_t expiry_seconds_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_AUTH_JWT_HELPER_H_
