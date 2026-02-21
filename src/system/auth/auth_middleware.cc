// Copyright 2026 Loong AI NVR Project

#include "system/auth/auth_middleware.h"

#include "spdlog/spdlog.h"

#include <algorithm>

namespace loong::system {

AuthMiddleware::AuthMiddleware(std::shared_ptr<JwtHelper> jwt,
                               std::shared_ptr<UserStore> user_store)
    : jwt_(std::move(jwt)), user_store_(std::move(user_store)) {}

AuthContext AuthMiddleware::Authenticate(const std::string& raw_request) {
  AuthContext ctx;

  std::string token = ExtractBearerToken(raw_request);
  if (token.empty()) {
    return ctx;
  }

  JwtClaims claims;
  if (!jwt_->VerifyToken(token, claims)) {
    spdlog::debug("AuthMiddleware: invalid token for user_id={}",
                  claims.user_id);
    return ctx;
  }

  // Verify user still exists and is enabled
  UserInfo user;
  if (!user_store_->GetUser(claims.user_id, user) || !user.enabled) {
    spdlog::debug("AuthMiddleware: user {} not found or disabled",
                  claims.user_id);
    return ctx;
  }

  ctx.authenticated = true;
  ctx.claims = claims;
  return ctx;
}

bool AuthMiddleware::HasPermission(UserRole role, const std::string& method,
                                   const std::string& path) {
  // Admin has full access
  if (role == UserRole::kAdmin) {
    return true;
  }

  // Auth profile endpoints: all authenticated users
  if (path == "/api/auth/profile" || path == "/api/auth/password") {
    return true;
  }

  // User management: admin only
  if (path.find("/api/users") == 0) {
    return false;
  }

  // System endpoints: all authenticated users for GET
  if (path.find("/api/system/") == 0) {
    if (role == UserRole::kOperator) return true;
    return method == "GET";
  }

  // Live streaming: all authenticated users
  if (path.find("/live/") == 0) {
    return true;
  }

  // ONVIF: operators and admin can control PTZ; all can discover/view
  if (path.find("/api/onvif/") == 0) {
    if (role == UserRole::kOperator) return true;
    return method == "GET";
  }

  // Events and recordings: all authenticated users (read)
  if (path == "/api/events" || path == "/api/recordings") {
    return method == "GET";
  }

  // Channels
  if (path.find("/api/channels") == 0) {
    if (role == UserRole::kOperator) {
      return true;  // Operators can manage channels
    }
    // Viewers can only read channel info
    return method == "GET";
  }

  // Default: deny
  return false;
}

bool AuthMiddleware::RequiresAuth(const std::string& path) {
  if (path == "/api/auth/login") return false;
  if (path == "/api/health") return false;
  if (path == "/metrics") return false;

  if (path.find("/live/") == 0) return true;

  return path.find("/api/") == 0;
}

std::string AuthMiddleware::ExtractBearerToken(const std::string& raw_request) {
  // Look for "Authorization: Bearer <token>" in headers
  const std::string kAuthHeader = "Authorization:";
  auto pos = raw_request.find(kAuthHeader);
  if (pos == std::string::npos) {
    // Try lowercase
    const std::string kAuthHeaderLower = "authorization:";
    pos = raw_request.find(kAuthHeaderLower);
    if (pos == std::string::npos) {
      return "";
    }
  }

  // Move past "Authorization:"
  pos += kAuthHeader.size();

  // Find end of line
  auto eol = raw_request.find("\r\n", pos);
  if (eol == std::string::npos) {
    eol = raw_request.size();
  }

  std::string value = raw_request.substr(pos, eol - pos);

  // Trim leading whitespace
  auto start = value.find_first_not_of(' ');
  if (start == std::string::npos) return "";
  value = value.substr(start);

  // Check "Bearer " prefix
  const std::string kBearer = "Bearer ";
  if (value.find(kBearer) != 0) return "";

  return value.substr(kBearer.size());
}

}  // namespace loong::system
