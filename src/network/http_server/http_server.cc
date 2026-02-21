// Copyright 2026 Loong AI NVR Project

#include "network/http_server/http_server.h"

#include <nlohmann/json.hpp>
#include <utility>

#include "spdlog/spdlog.h"

namespace loong::network {

HttpServer::HttpServer(std::string host, int port, const TlsConfig& tls)
    : host_(std::move(host)), port_(port), tls_(tls) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  if (tls_.enabled && !tls_.cert_path.empty() && !tls_.key_path.empty()) {
    server_ = std::make_unique<httplib::SSLServer>(tls_.cert_path.c_str(),
                                                   tls_.key_path.c_str());
    spdlog::info("HttpServer: TLS enabled (cert={}, key={})", tls_.cert_path,
                 tls_.key_path);
  } else {
    server_ = std::make_unique<httplib::Server>();
    if (tls_.enabled) {
      spdlog::warn(
          "HttpServer: TLS requested but cert/key not provided, falling back "
          "to HTTP");
      tls_.enabled = false;
    }
  }
#else
  server_ = std::make_unique<httplib::Server>();
  if (tls_.enabled) {
    spdlog::warn(
        "HttpServer: TLS requested but CPPHTTPLIB_OPENSSL_SUPPORT not "
        "defined, falling back to HTTP");
    tls_.enabled = false;
  }
#endif
}

HttpServer::~HttpServer() { Stop(); }

bool HttpServer::SetStaticDir(const std::string& mount_point,
                              const std::string& dir_path) {
  if (!server_) return false;
  auto ret = server_->set_mount_point(mount_point, dir_path);
  if (ret) {
    spdlog::info("HttpServer: serving static files from '{}' at '{}'",
                 dir_path, mount_point);
  } else {
    spdlog::warn("HttpServer: failed to mount '{}' at '{}'", dir_path,
                 mount_point);
  }
  return ret;
}

bool HttpServer::Start() {
  if (server_->is_running()) {
    spdlog::warn("HttpServer: already running, ignoring Start()");
    return true;
  }

  server_->new_task_queue = [] {
    return new httplib::ThreadPool(128);
  };

  SetupCors();
  SetupSecurityHeaders();
  SetupPayloadLimits();

  server_thread_ = std::thread([this]() {
    const char* proto = tls_.enabled ? "HTTPS" : "HTTP";
    spdlog::info("HttpServer: listening on {}://{}:{}", proto, host_, port_);
    if (!server_->listen(host_, port_)) {
      spdlog::error("HttpServer: failed to listen on {}:{}", host_, port_);
    }
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return server_->is_running();
}

void HttpServer::Stop() {
  if (server_ && server_->is_running()) {
    server_->stop();
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
  spdlog::info("HttpServer: stopped");
}

void HttpServer::SetupCors() {
  server_->Options(R"(.*)",
                   [](const httplib::Request& /*req*/, httplib::Response& res) {
                     SetCorsHeaders(res);
                     res.status = 200;
                   });
}

void HttpServer::SetupSecurityHeaders() {
  server_->set_post_routing_handler(
      [](const httplib::Request& /*req*/, httplib::Response& res) {
        SetSecurityHeaders(res);
      });
}

void HttpServer::SetupPayloadLimits() {
  server_->set_payload_max_length(1024 * 1024 * 10);  // 10 MB
}

// ---- Static response helpers ----

void HttpServer::SetCorsHeaders(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Methods",
                 "GET, POST, PUT, DELETE, OPTIONS");
  res.set_header("Access-Control-Allow-Headers",
                 "Content-Type, Authorization");
}

void HttpServer::SetSecurityHeaders(httplib::Response& res) {
  res.set_header("X-Content-Type-Options", "nosniff");
  res.set_header("X-Frame-Options", "DENY");
  res.set_header("X-XSS-Protection", "1; mode=block");
  res.set_header("Referrer-Policy", "strict-origin-when-cross-origin");
  res.set_header("Cache-Control", "no-store");
  res.set_header("Permissions-Policy",
                 "camera=(), microphone=(), geolocation=()");
}

void HttpServer::JsonError(httplib::Response& res, int code,
                           const std::string& message) {
  nlohmann::json err = {{"error", message}};
  SetCorsHeaders(res);
  res.set_content(err.dump(), "application/json");
  res.status = code;
}

void HttpServer::JsonResponse(httplib::Response& res, int code,
                              const std::string& body) {
  SetCorsHeaders(res);
  res.set_content(body, "application/json");
  res.status = code;
}

bool HttpServer::CheckAuth(const httplib::Request& req,
                           httplib::Response& res, system::AuthContext& ctx) {
  if (!auth_middleware_) {
    ctx.authenticated = true;
    return true;
  }

  if (!system::AuthMiddleware::RequiresAuth(req.path)) {
    ctx.authenticated = true;
    return true;
  }

  std::string auth_header = req.get_header_value("Authorization");
  if (auth_header.empty()) {
    auth_header = req.get_header_value("authorization");
  }

  if (auth_header.empty()) {
    std::string token = req.get_param_value("token");
    if (!token.empty()) {
      auth_header = "Bearer " + token;
    }
  }

  std::string raw =
      "GET / HTTP/1.1\r\nAuthorization: " + auth_header + "\r\n\r\n";
  ctx = auth_middleware_->Authenticate(raw);

  if (!ctx.authenticated) {
    JsonError(res, 401, "unauthorized");
    return false;
  }

  if (!system::AuthMiddleware::HasPermission(ctx.claims.role, req.method,
                                             req.path)) {
    JsonError(res, 403, "forbidden");
    return false;
  }

  return true;
}

// ---- Rate limiting ----

bool HttpServer::CheckRateLimit(const httplib::Request& req,
                                httplib::Response& res) {
  std::string ip = req.remote_addr;
  auto now = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(rate_mutex_);
  auto& entry = rate_limits_[ip];

  auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
      now - entry.window_start);

  if (elapsed.count() >= kRateLimitWindowSeconds) {
    entry.request_count = 1;
    entry.window_start = now;
    return true;
  }

  ++entry.request_count;
  if (entry.request_count > kRateLimitMaxRequests) {
    int retry_after = kRateLimitWindowSeconds -
                      static_cast<int>(elapsed.count());
    res.set_header("Retry-After", std::to_string(retry_after));
    JsonError(res, 429, "rate limit exceeded");
    return false;
  }

  return true;
}

// ---- Login brute-force protection ----

void HttpServer::RecordLoginFailure(const std::string& ip) {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(login_mutex_);
  auto& entry = login_attempts_[ip];

  auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
      now - entry.first_failure);
  if (elapsed.count() >= kLoginLockoutMinutes) {
    entry.failed_count = 0;
  }

  if (entry.failed_count == 0) {
    entry.first_failure = now;
  }
  ++entry.failed_count;

  if (entry.failed_count >= kMaxLoginAttempts) {
    entry.locked_until =
        now + std::chrono::minutes(kLoginLockoutMinutes);
    spdlog::warn("HttpServer: IP {} locked for {} minutes after {} failed "
                 "login attempts",
                 ip, kLoginLockoutMinutes, entry.failed_count);
  }
}

void HttpServer::RecordLoginSuccess(const std::string& ip) {
  std::lock_guard<std::mutex> lock(login_mutex_);
  login_attempts_.erase(ip);
}

bool HttpServer::IsLoginLocked(const std::string& ip) {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> lock(login_mutex_);
  auto it = login_attempts_.find(ip);
  if (it == login_attempts_.end()) return false;

  if (it->second.failed_count >= kMaxLoginAttempts &&
      now < it->second.locked_until) {
    return true;
  }

  if (now >= it->second.locked_until && it->second.failed_count >= kMaxLoginAttempts) {
    login_attempts_.erase(it);
  }

  return false;
}

}  // namespace loong::network
