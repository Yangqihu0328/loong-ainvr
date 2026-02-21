// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_AUTH_AUTH_MIDDLEWARE_H_
#define LOONG_SYSTEM_AUTH_AUTH_MIDDLEWARE_H_

#include <memory>
#include <string>

#include "system/auth/jwt_helper.h"
#include "system/auth/user_store.h"

namespace loong::system {

/// HTTP request context populated by the auth middleware.
struct AuthContext {
  bool authenticated = false;
  JwtClaims claims;
};

/// Authentication middleware for the HTTP API.
/// Extracts and verifies JWT tokens from the Authorization header.
class AuthMiddleware {
 public:
  AuthMiddleware(std::shared_ptr<JwtHelper> jwt,
                 std::shared_ptr<UserStore> user_store);

  /// Authenticate a request from its raw HTTP headers.
  /// Extracts "Authorization: Bearer <token>" and verifies the JWT.
  /// Returns an AuthContext with authentication status and claims.
  AuthContext Authenticate(const std::string& raw_request);

  /// Check if the given role has permission for an API path.
  /// RBAC rules:
  ///   - Admin:    full access
  ///   - Operator: channels, recordings, events, system/status
  ///   - Viewer:   channels (read), live, recordings, events, system/status
  static bool HasPermission(UserRole role, const std::string& method,
                            const std::string& path);

  /// Check if a path requires authentication.
  /// Public paths: /api/auth/login
  static bool RequiresAuth(const std::string& path);

 private:
  /// Extract Bearer token from raw HTTP request headers.
  static std::string ExtractBearerToken(const std::string& raw_request);

  std::shared_ptr<JwtHelper> jwt_;
  std::shared_ptr<UserStore> user_store_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_AUTH_AUTH_MIDDLEWARE_H_
