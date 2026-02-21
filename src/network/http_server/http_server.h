// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_HTTP_SERVER_HTTP_SERVER_H_
#define LOONG_NETWORK_HTTP_SERVER_HTTP_SERVER_H_

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

// Suppress warnings from third-party header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <httplib.h>
#pragma GCC diagnostic pop

#include "system/auth/auth_middleware.h"

namespace loong::network {

/// TLS configuration for HTTPS support.
struct TlsConfig {
  bool enabled = false;
  std::string cert_path;
  std::string key_path;
};

/// Rate limiter entry for a single IP.
struct RateLimitEntry {
  int request_count = 0;
  std::chrono::steady_clock::time_point window_start;
};

/// Login attempt tracking for brute-force protection.
struct LoginAttemptEntry {
  int failed_count = 0;
  std::chrono::steady_clock::time_point first_failure;
  std::chrono::steady_clock::time_point locked_until;
};

/// Generic multi-threaded HTTP/HTTPS server built on cpp-httplib.
///
/// Features:
///   - Optional TLS via httplib::SSLServer
///   - Thread-pool configuration
///   - Global CORS preflight handling
///   - Security response headers
///   - Request body size limits
///   - IP-based rate limiting (sliding window)
///   - Login brute-force protection
///   - Authentication middleware integration
///   - JSON response helper utilities
class HttpServer {
 public:
  HttpServer(std::string host, int port,
             const TlsConfig& tls = TlsConfig{});
  ~HttpServer();

  /// Access the underlying httplib server for route registration.
  httplib::Server& GetServer() { return *server_; }

  /// Set authentication middleware (optional).
  void SetAuthMiddleware(std::shared_ptr<system::AuthMiddleware> auth) {
    auth_middleware_ = std::move(auth);
  }

  /// Mount a directory to serve static files (e.g. frontend).
  /// Must be called before Start().
  bool SetStaticDir(const std::string& mount_point,
                    const std::string& dir_path);

  /// Start the HTTP(S) server (non-blocking, spawns a listener thread).
  bool Start();

  /// Stop the HTTP(S) server and join the listener thread.
  void Stop();

  /// Whether TLS is enabled.
  bool IsTlsEnabled() const { return tls_.enabled; }

  // ---- Response helpers (static, usable from any handler) ----

  /// Add CORS headers to a response.
  static void SetCorsHeaders(httplib::Response& res);

  /// Add security response headers.
  static void SetSecurityHeaders(httplib::Response& res);

  /// Write a JSON error response.
  static void JsonError(httplib::Response& res, int code,
                        const std::string& message);

  /// Write a JSON success response.
  static void JsonResponse(httplib::Response& res, int code,
                           const std::string& body);

  /// Authenticate the request using the configured middleware.
  bool CheckAuth(const httplib::Request& req, httplib::Response& res,
                 system::AuthContext& ctx);

  // ---- Rate limiting ----

  /// Check if a request from the given IP should be rate-limited.
  /// Returns true if allowed, false if rate-limited (429 set on res).
  bool CheckRateLimit(const httplib::Request& req, httplib::Response& res);

  // ---- Login brute-force protection ----

  /// Record a failed login attempt for the given IP.
  void RecordLoginFailure(const std::string& ip);

  /// Record a successful login (clears failure count).
  void RecordLoginSuccess(const std::string& ip);

  /// Check if the IP is currently locked out. Returns true if locked.
  bool IsLoginLocked(const std::string& ip);

  // Non-copyable
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;

 private:
  void SetupCors();
  void SetupSecurityHeaders();
  void SetupPayloadLimits();

  std::string host_;
  int port_;
  TlsConfig tls_;
  std::unique_ptr<httplib::Server> server_;
  std::thread server_thread_;
  std::shared_ptr<system::AuthMiddleware> auth_middleware_;

  // Rate limiting state (IP → entry)
  std::mutex rate_mutex_;
  std::unordered_map<std::string, RateLimitEntry> rate_limits_;
  static constexpr int kRateLimitMaxRequests = 120;
  static constexpr int kRateLimitWindowSeconds = 60;

  // Login attempt tracking (IP → entry)
  std::mutex login_mutex_;
  std::unordered_map<std::string, LoginAttemptEntry> login_attempts_;
  static constexpr int kMaxLoginAttempts = 5;
  static constexpr int kLoginLockoutMinutes = 15;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_HTTP_SERVER_HTTP_SERVER_H_
