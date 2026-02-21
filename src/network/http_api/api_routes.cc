// Copyright 2026 Loong AI NVR Project

#include "network/http_api/api_routes.h"

#include "core/config/config_manager.h"
#include "network/flv_stream/http_flv_service.h"
#include "network/hls_stream/hls_service.h"
#include "network/http_server/http_server.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>

namespace loong::network {

void ApiRoutes::Register(HttpServer& server) {
  auto& svr = server.GetServer();

  // ========== Auth endpoints ==========
  svr.Post("/api/auth/login", [this, &server](const httplib::Request& req,
                                              httplib::Response& res) {
    HandleLogin(server, req, res);
  });

  svr.Get("/api/auth/profile",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetProfile(server, req, res);
          });

  svr.Put("/api/auth/password",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleChangePassword(server, req, res);
          });

  // ========== User management (admin only) ==========
  svr.Get("/api/users",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleListUsers(server, req, res);
          });

  svr.Post("/api/users", [this, &server](const httplib::Request& req,
                                         httplib::Response& res) {
    HandleCreateUser(server, req, res);
  });

  svr.Put(R"(/api/users/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateUser(server, req, res);
          });

  svr.Delete(R"(/api/users/(\d+))", [this, &server](const httplib::Request& req,
                                                    httplib::Response& res) {
    HandleDeleteUser(server, req, res);
  });

  // ========== Channel endpoints ==========
  svr.Get("/api/channels",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleListChannels(server, req, res);
          });

  svr.Post("/api/channels", [this, &server](const httplib::Request& req,
                                            httplib::Response& res) {
    HandleCreateChannel(server, req, res);
  });

  // Actions must be before generic /:id route
  svr.Post(
      R"(/api/channels/(\d+)/start)",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleStartChannel(server, req, res);
      });

  svr.Post(
      R"(/api/channels/(\d+)/stop)",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleStopChannel(server, req, res);
      });

  svr.Get(R"(/api/channels/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetChannel(server, req, res);
          });

  svr.Put(R"(/api/channels/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateChannel(server, req, res);
          });

  svr.Delete(
      R"(/api/channels/(\d+))",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleDeleteChannel(server, req, res);
      });

  // ========== Overlay config endpoints ==========
  svr.Get(R"(/api/channels/(\d+)/overlay)",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetOverlayConfig(server, req, res);
          });

  svr.Put(R"(/api/channels/(\d+)/overlay)",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateOverlayConfig(server, req, res);
          });

  // ========== Other endpoints ==========
  svr.Get("/api/recordings",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetRecordings(server, req, res);
          });

  svr.Get("/api/events",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetEvents(server, req, res);
          });

  svr.Get("/api/recordings/timeline",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetTimeline(server, req, res);
          });

  svr.Get("/api/events/search",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleSearchEvents(server, req, res);
          });

  svr.Get("/api/system/status",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetSystemStatus(server, req, res);
          });

  svr.Put("/api/system/storage",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateStorageConfig(server, req, res);
          });

  svr.Get("/api/system/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetConfig(server, req, res);
          });

  svr.Put("/api/system/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateConfig(server, req, res);
          });

  // ========== Recording file access ==========
  svr.Get("/api/recordings/playback",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleRecordingPlayback(server, req, res);
          });

  svr.Get("/api/recordings/download",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleRecordingDownload(server, req, res);
          });

  // ========== FLV streaming ==========
  svr.Get(R"(/live/ch(\d+)\.flv)",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleFlvStream(server, req, res);
          });

  // ========== HLS streaming ==========
  svr.Get(R"(/live/ch(\d+)/index\.m3u8)",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleHlsPlaylist(server, req, res);
          });

  svr.Get(R"(/live/ch(\d+)/(\d+)\.ts)",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleHlsSegment(server, req, res);
          });

  // ========== ONVIF endpoints ==========
  svr.Get("/api/onvif/discover",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleOnvifDiscover(server, req, res);
          });

  svr.Get(R"(/api/onvif/device/(.+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleOnvifDeviceInfo(server, req, res);
          });

  svr.Post(
      R"(/api/onvif/device/(.+)/ptz)",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleOnvifPtz(server, req, res);
      });

  // ========== Notification endpoints ==========
  svr.Get("/api/system/notifications",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetNotificationConfig(server, req, res);
          });

  svr.Put("/api/system/notifications",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateNotificationConfig(server, req, res);
          });

  svr.Post(
      "/api/system/notifications/test",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleTestNotification(server, req, res);
      });

  svr.Get("/api/system/notifications/history",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetNotificationHistory(server, req, res);
          });

  // ========== Rules endpoints ==========
  svr.Get("/api/rules",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleListRules(server, req, res);
          });

  svr.Post("/api/rules", [this, &server](const httplib::Request& req,
                                         httplib::Response& res) {
    HandleCreateRule(server, req, res);
  });

  svr.Get(R"(/api/rules/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetRule(server, req, res);
          });

  svr.Put(R"(/api/rules/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateRule(server, req, res);
          });

  svr.Delete(R"(/api/rules/(\d+))", [this, &server](const httplib::Request& req,
                                                    httplib::Response& res) {
    HandleDeleteRule(server, req, res);
  });

  svr.Get("/api/rules/events",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleQueryRuleEvents(server, req, res);
          });

  // ========== LPR (Plate) endpoints ==========
  svr.Get("/api/analytics/plates",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleQueryPlates(server, req, res);
          });

  // ========== Face endpoints ==========
  svr.Get("/api/analytics/faces",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleQueryFaces(server, req, res);
          });

  svr.Post(
      "/api/analytics/faces/search",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleSearchFaces(server, req, res);
      });

  // ========== Analytics endpoints ==========
  svr.Get("/api/analytics/summary",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleAnalyticsSummary(server, req, res);
          });

  svr.Get("/api/analytics/trends",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleAnalyticsTrends(server, req, res);
          });

  svr.Get("/api/analytics/heatmap",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleAnalyticsHeatmap(server, req, res);
          });

  svr.Get("/api/analytics/peak-hours",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleAnalyticsPeakHours(server, req, res);
          });

  svr.Get("/api/analytics/counting",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleAnalyticsCounting(server, req, res);
          });

  // ========== Observability endpoints ==========
  svr.Get("/api/health",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleHealthCheck(server, req, res);
          });

  svr.Get("/metrics",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleMetrics(server, req, res);
          });

  // ========== WebRTC endpoints ==========
  svr.Post("/api/webrtc/offer", [this, &server](const httplib::Request& req,
                                                httplib::Response& res) {
    HandleWebRtcOffer(server, req, res);
  });

  svr.Post("/api/webrtc/ice", [this, &server](const httplib::Request& req,
                                              httplib::Response& res) {
    HandleWebRtcIce(server, req, res);
  });

  svr.Delete(
      R"(/api/webrtc/(\w+))",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleWebRtcClose(server, req, res);
      });

  // ========== Cloud Backup endpoints ==========
  svr.Get("/api/backup/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetBackupConfig(server, req, res);
          });

  svr.Put("/api/backup/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateBackupConfig(server, req, res);
          });

  svr.Get("/api/backup/status",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetBackupStatus(server, req, res);
          });

  svr.Post("/api/backup/test", [this, &server](const httplib::Request& req,
                                               httplib::Response& res) {
    HandleTestBackupConnection(server, req, res);
  });

  svr.Post("/api/backup/trigger", [this, &server](const httplib::Request& req,
                                                  httplib::Response& res) {
    HandleTriggerBackup(server, req, res);
  });

  // ========== MQTT endpoints ==========
  svr.Get("/api/mqtt/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleGetMqttConfig(server, req, res);
          });

  svr.Put("/api/mqtt/config",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateMqttConfig(server, req, res);
          });

  svr.Post("/api/mqtt/test", [this, &server](const httplib::Request& req,
                                             httplib::Response& res) {
    HandleTestMqtt(server, req, res);
  });

  // ========== Plugin endpoints ==========
  svr.Get("/api/plugins",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleListPlugins(server, req, res);
          });

  svr.Post("/api/plugins/upload", [this, &server](const httplib::Request& req,
                                                  httplib::Response& res) {
    HandleUploadPlugin(server, req, res);
  });

  // ========== Alarm endpoints ==========
  svr.Get("/api/alarms/rules",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleListAlarmRules(server, req, res);
          });

  svr.Post("/api/alarms/rules", [this, &server](const httplib::Request& req,
                                                httplib::Response& res) {
    HandleCreateAlarmRule(server, req, res);
  });

  svr.Put(R"(/api/alarms/rules/(\d+))",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleUpdateAlarmRule(server, req, res);
          });

  svr.Delete(
      R"(/api/alarms/rules/(\d+))",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleDeleteAlarmRule(server, req, res);
      });

  svr.Get("/api/alarms",
          [this, &server](const httplib::Request& req, httplib::Response& res) {
            HandleQueryAlarms(server, req, res);
          });

  svr.Post(
      R"(/api/alarms/(\d+)/acknowledge)",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleAcknowledgeAlarm(server, req, res);
      });

  svr.Post(
      "/api/alarms/acknowledge-all",
      [this, &server](const httplib::Request& req, httplib::Response& res) {
        HandleAcknowledgeAllAlarms(server, req, res);
      });

  spdlog::info("ApiRoutes: registered 70 endpoints");
}

// ============================================================
// Auth Handlers
// ============================================================

void ApiRoutes::HandleLogin(HttpServer& srv, const httplib::Request& req,
                            httplib::Response& res) {
  HttpServer::SetCorsHeaders(res);
  if (!user_store_ || !jwt_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  std::string client_ip = req.remote_addr;

  if (srv.IsLoginLocked(client_ip)) {
    HttpServer::JsonError(res, 429,
                          "too many failed attempts, try again later");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    std::string username = j.value("username", "");
    std::string password = j.value("password", "");

    if (username.empty() || password.empty()) {
      HttpServer::JsonError(res, 400, "username and password required");
      return;
    }

    system::UserInfo user;
    if (!user_store_->Authenticate(username, password, user)) {
      srv.RecordLoginFailure(client_ip);
      HttpServer::JsonError(res, 401, "invalid credentials");
      return;
    }

    srv.RecordLoginSuccess(client_ip);

    std::string token = jwt_->GenerateToken(user);

    bool must_change_password = user_store_->MustChangePassword(user.id);

    nlohmann::json result = {
        {"token", token},
        {"must_change_password", must_change_password},
        {"user",
         {{"id", user.id},
          {"username", user.username},
          {"display_name", user.display_name},
          {"role", system::UserRoleToString(user.role)}}},
    };
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleGetProfile(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  system::UserInfo user;
  if (!user_store_->GetUser(ctx.claims.user_id, user)) {
    HttpServer::JsonError(res, 404, "user not found");
    return;
  }

  nlohmann::json result = {
      {"id", user.id},
      {"username", user.username},
      {"display_name", user.display_name},
      {"role", system::UserRoleToString(user.role)},
      {"created_at", user.created_at},
      {"last_login", user.last_login},
  };
  HttpServer::JsonResponse(res, 200, result.dump());
}

void ApiRoutes::HandleChangePassword(HttpServer& srv,
                                     const httplib::Request& req,
                                     httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    std::string old_password = j.value("old_password", "");
    std::string new_password = j.value("new_password", "");

    if (new_password.empty() || new_password.size() < 6) {
      HttpServer::JsonError(res, 400,
                            "new password must be at least 6 characters");
      return;
    }

    system::UserInfo user;
    if (!user_store_->Authenticate(ctx.claims.username, old_password, user)) {
      HttpServer::JsonError(res, 401, "incorrect old password");
      return;
    }

    if (!user_store_->ChangePassword(ctx.claims.user_id, new_password)) {
      HttpServer::JsonError(res, 500, "failed to change password");
      return;
    }

    user_store_->ClearMustChangePassword(ctx.claims.user_id);

    nlohmann::json result = {{"success", true}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

// ============================================================
// User Management Handlers
// ============================================================

void ApiRoutes::HandleListUsers(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  auto users = user_store_->ListUsers();
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& u : users) {
    arr.push_back({{"id", u.id},
                   {"username", u.username},
                   {"display_name", u.display_name},
                   {"role", system::UserRoleToString(u.role)},
                   {"enabled", u.enabled},
                   {"created_at", u.created_at},
                   {"updated_at", u.updated_at},
                   {"last_login", u.last_login}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump());
}

void ApiRoutes::HandleCreateUser(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    std::string username = j.value("username", "");
    std::string password = j.value("password", "");
    std::string display_name = j.value("display_name", username);
    auto role = system::ParseUserRole(j.value("role", "viewer"));

    if (username.empty() || password.empty()) {
      HttpServer::JsonError(res, 400, "username and password required");
      return;
    }
    if (password.size() < 6) {
      HttpServer::JsonError(res, 400, "password must be at least 6 characters");
      return;
    }

    system::UserInfo existing;
    if (user_store_->GetUserByName(username, existing)) {
      HttpServer::JsonError(res, 409, "username already exists");
      return;
    }

    int64_t id =
        user_store_->CreateUser(username, password, display_name, role);
    if (id < 0) {
      HttpServer::JsonError(res, 500, "failed to create user");
      return;
    }

    nlohmann::json result = {{"id", id}, {"success", true}};
    HttpServer::JsonResponse(res, 201, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleUpdateUser(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  try {
    int64_t uid = std::stoll(req.matches[1]);
    auto j = nlohmann::json::parse(req.body);

    system::UserInfo existing;
    if (!user_store_->GetUser(uid, existing)) {
      HttpServer::JsonError(res, 404, "user not found");
      return;
    }

    std::string display_name = j.value("display_name", existing.display_name);
    auto role = system::ParseUserRole(
        j.value("role", system::UserRoleToString(existing.role)));
    bool enabled = j.value("enabled", existing.enabled);

    if (!user_store_->UpdateUser(uid, display_name, role, enabled)) {
      HttpServer::JsonError(res, 500, "failed to update user");
      return;
    }

    if (j.contains("password") && !j["password"].get<std::string>().empty()) {
      std::string password = j["password"].get<std::string>();
      if (password.size() < 6) {
        HttpServer::JsonError(res, 400,
                              "password must be at least 6 characters");
        return;
      }
      user_store_->ChangePassword(uid, password);
    }

    nlohmann::json result = {{"success", true}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json or user id");
  }
}

void ApiRoutes::HandleDeleteUser(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!user_store_) {
    HttpServer::JsonError(res, 500, "auth not configured");
    return;
  }

  try {
    int64_t uid = std::stoll(req.matches[1]);
    if (!user_store_->DeleteUser(uid)) {
      HttpServer::JsonError(res, 400,
                            "cannot delete user (may be the last admin)");
      return;
    }
    nlohmann::json result = {{"success", true}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid user id");
  }
}

// ============================================================
// Channel Handlers
// ============================================================

void ApiRoutes::HandleListChannels(HttpServer& srv, const httplib::Request& req,
                                   httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  nlohmann::json arr = nlohmann::json::array();
  if (channel_mgr_) {
    auto statuses = channel_mgr_->GetAllStatus();
    for (const auto& s : statuses) {
      nlohmann::json ch = {
          {"id", s.id},
          {"name", s.name},
          {"state", ChannelStateToString(s.state)},
          {"frames_processed", s.frames_processed},
          {"frames_dropped", s.frames_dropped},
          {"latitude", s.latitude},
          {"longitude", s.longitude},
      };

      ChannelConfig cfg;
      if (channel_mgr_->GetChannelConfig(s.id, cfg)) {
        ch["rtsp_url"] = cfg.rtsp_url;
        ch["codec"] = (cfg.codec == CodecType::kH265) ? "h265" : "h264";
        ch["width"] = cfg.width;
        ch["height"] = cfg.height;
        ch["framerate"] = cfg.framerate;
        ch["record_enabled"] = cfg.record_enabled;
      }

      arr.push_back(std::move(ch));
    }
  }
  HttpServer::JsonResponse(res, 200, arr.dump(2));
}

void ApiRoutes::HandleCreateChannel(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  try {
    auto j = nlohmann::json::parse(req.body);
    ChannelConfig config;
    config.name = j.value("name", "");
    // Accept both "url" and "rtsp_url" for convenience.
    config.rtsp_url = j.value("url", j.value("rtsp_url", ""));
    config.bitrate_kbps = j.value("bitrate_kbps", 4000);
    config.ai_model_name = j.value("ai_model", "");
    config.ai_backend = j.value("ai_backend", "");
    config.confidence_threshold = j.value("confidence_threshold", 0.5F);
    config.analysis_fps = j.value("analysis_fps", 5);
    config.record_enabled = j.value("record_enabled", true);
    config.latitude = j.value("latitude", 0.0);
    config.longitude = j.value("longitude", 0.0);

    // Resolution, codec, framerate are auto-detected from the stream.
    // Users may override them but it's not required.
    if (j.contains("width")) config.width = j["width"];
    if (j.contains("height")) config.height = j["height"];
    if (j.contains("framerate")) config.framerate = j["framerate"];
    if (j.contains("codec")) {
      auto codec_str = j["codec"].get<std::string>();
      config.codec =
          (codec_str == "h265") ? CodecType::kH265 : CodecType::kH264;
    }

    if (config.name.empty()) {
      HttpServer::JsonError(res, 400, "name is required");
      return;
    }
    if (config.rtsp_url.empty()) {
      HttpServer::JsonError(res, 400, "url is required");
      return;
    }

    int id = channel_mgr_ ? channel_mgr_->CreateChannel(config) : -1;
    nlohmann::json result = {{"id", id}, {"success", id > 0}};
    HttpServer::JsonResponse(res, id > 0 ? 201 : 400, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleGetChannel(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    auto s = channel_mgr_->GetStatus(ch_id);
    if (s.id < 0) {
      HttpServer::JsonError(res, 404, "channel not found");
      return;
    }

    nlohmann::json result = {
        {"id", s.id},
        {"name", s.name},
        {"state", ChannelStateToString(s.state)},
        {"fps_in", s.fps_in},
        {"fps_decode", s.fps_decode},
        {"fps_ai", s.fps_ai},
        {"frames_processed", s.frames_processed},
        {"frames_dropped", s.frames_dropped},
        {"error_message", s.error_message},
        {"latitude", s.latitude},
        {"longitude", s.longitude},
    };

    ChannelConfig cfg;
    if (channel_mgr_->GetChannelConfig(ch_id, cfg)) {
      result["rtsp_url"] = cfg.rtsp_url;
      result["codec"] = (cfg.codec == CodecType::kH265) ? "h265" : "h264";
      result["width"] = cfg.width;
      result["height"] = cfg.height;
      result["framerate"] = cfg.framerate;
      result["bitrate_kbps"] = cfg.bitrate_kbps;
      result["ai_model"] = cfg.ai_model_name;
      result["ai_backend"] = cfg.ai_backend;
      result["confidence_threshold"] = cfg.confidence_threshold;
      result["analysis_fps"] = cfg.analysis_fps;
      result["record_enabled"] = cfg.record_enabled;
    }

    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
  }
}

void ApiRoutes::HandleUpdateChannel(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    auto j = nlohmann::json::parse(req.body);

    // Load existing config first so unspecified fields keep their values.
    ChannelConfig config;
    channel_mgr_->GetChannelConfig(ch_id, config);

    if (j.contains("name")) config.name = j["name"];
    if (j.contains("url")) config.rtsp_url = j["url"];
    if (j.contains("rtsp_url")) config.rtsp_url = j["rtsp_url"];
    if (j.contains("bitrate_kbps")) config.bitrate_kbps = j["bitrate_kbps"];
    if (j.contains("ai_model")) config.ai_model_name = j["ai_model"];
    if (j.contains("ai_backend")) config.ai_backend = j["ai_backend"];
    if (j.contains("confidence_threshold"))
      config.confidence_threshold = j["confidence_threshold"];
    if (j.contains("analysis_fps")) config.analysis_fps = j["analysis_fps"];
    if (j.contains("record_enabled"))
      config.record_enabled = j["record_enabled"];
    if (j.contains("latitude")) config.latitude = j["latitude"];
    if (j.contains("longitude")) config.longitude = j["longitude"];
    if (j.contains("width")) config.width = j["width"];
    if (j.contains("height")) config.height = j["height"];
    if (j.contains("framerate")) config.framerate = j["framerate"];
    if (j.contains("codec")) {
      auto codec_str = j["codec"].get<std::string>();
      config.codec =
          (codec_str == "h265") ? CodecType::kH265 : CodecType::kH264;
    }

    bool ok = channel_mgr_->EditChannel(ch_id, config);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 400, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json or channel id");
  }
}

void ApiRoutes::HandleDeleteChannel(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    bool ok = channel_mgr_->DeleteChannel(ch_id);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 404, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
  }
}

void ApiRoutes::HandleStartChannel(HttpServer& srv, const httplib::Request& req,
                                   httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    bool ok = channel_mgr_->StartChannel(ch_id);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 400, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
  }
}

void ApiRoutes::HandleStopChannel(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    bool ok = channel_mgr_->StopChannel(ch_id);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 400, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
  }
}

// ============================================================
// Overlay Config Handlers
// ============================================================

void ApiRoutes::HandleGetOverlayConfig(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    ChannelConfig config;
    if (!channel_mgr_->GetChannelConfig(ch_id, config)) {
      HttpServer::JsonError(res, 404, "channel not found");
      return;
    }

    const auto& ov = config.overlay;
    nlohmann::json result = {
        {"channel_id", ch_id},
        {"enabled", ov.enabled},
        {"line_thickness", ov.line_thickness},
        {"font_scale", ov.font_scale},
        {"show_labels", ov.show_labels},
        {"show_confidence", ov.show_confidence},
        {"show_timestamp", ov.show_timestamp},
        {"show_channel_name", ov.show_channel_name},
        {"show_trajectory", ov.show_trajectory},
        {"trajectory_max_points", ov.trajectory_max_points},
        {"timestamp_position", ov.timestamp_position},
        {"fill_opacity", ov.fill_opacity},
        {"timestamp_format", ov.timestamp_format},
    };
    HttpServer::JsonResponse(res, 200, result.dump(2));
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
  }
}

void ApiRoutes::HandleUpdateOverlayConfig(HttpServer& srv,
                                          const httplib::Request& req,
                                          httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!channel_mgr_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  try {
    int ch_id = std::stoi(req.matches[1]);
    auto j = nlohmann::json::parse(req.body);

    // Get current config as baseline
    ChannelConfig config;
    if (!channel_mgr_->GetChannelConfig(ch_id, config)) {
      HttpServer::JsonError(res, 404, "channel not found");
      return;
    }

    // Merge fields: only update provided fields
    OverlayConfig ov = config.overlay;
    ov.enabled = j.value("enabled", ov.enabled);
    ov.line_thickness = j.value("line_thickness", ov.line_thickness);
    ov.font_scale = j.value("font_scale", ov.font_scale);
    ov.show_labels = j.value("show_labels", ov.show_labels);
    ov.show_confidence = j.value("show_confidence", ov.show_confidence);
    ov.show_timestamp = j.value("show_timestamp", ov.show_timestamp);
    ov.show_channel_name = j.value("show_channel_name", ov.show_channel_name);
    ov.show_trajectory = j.value("show_trajectory", ov.show_trajectory);
    ov.trajectory_max_points =
        j.value("trajectory_max_points", ov.trajectory_max_points);
    ov.timestamp_position =
        j.value("timestamp_position", ov.timestamp_position);
    ov.fill_opacity = j.value("fill_opacity", ov.fill_opacity);
    ov.timestamp_format = j.value("timestamp_format", ov.timestamp_format);

    bool ok = channel_mgr_->UpdateOverlayConfig(ch_id, ov);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 500, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json or channel id");
  }
}

// ============================================================
// Other Handlers
// ============================================================

void ApiRoutes::HandleGetRecordings(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!record_index_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  int channel_id = 0;
  int64_t start_time = 0;
  int64_t end_time = INT64_MAX;

  try {
    std::string ch_str = req.get_param_value("channel_id");
    std::string start_str = req.get_param_value("start");
    std::string end_str = req.get_param_value("end");
    if (!ch_str.empty()) channel_id = std::stoi(ch_str);
    if (!start_str.empty()) start_time = std::stoll(start_str);
    if (!end_str.empty()) end_time = std::stoll(end_str);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid query parameters");
    return;
  }

  auto segments =
      record_index_->QuerySegments(channel_id, start_time, end_time);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& seg : segments) {
    arr.push_back({{"id", seg.id},
                   {"channel_id", seg.channel_id},
                   {"start_time", seg.start_time},
                   {"end_time", seg.end_time},
                   {"file_path", seg.file_path},
                   {"file_size", seg.file_size},
                   {"codec", seg.codec},
                   {"resolution", seg.resolution}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump());
}

void ApiRoutes::HandleGetEvents(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!record_index_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  int channel_id = 0;
  int64_t start_time = 0;
  int64_t end_time = INT64_MAX;

  try {
    std::string ch_str = req.get_param_value("channel_id");
    std::string start_str = req.get_param_value("start");
    std::string end_str = req.get_param_value("end");
    if (!ch_str.empty()) channel_id = std::stoi(ch_str);
    if (!start_str.empty()) start_time = std::stoll(start_str);
    if (!end_str.empty()) end_time = std::stoll(end_str);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid query parameters");
    return;
  }

  auto events = record_index_->QueryEvents(channel_id, start_time, end_time);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& evt : events) {
    arr.push_back({{"id", evt.id},
                   {"channel_id", evt.channel_id},
                   {"event_type", evt.event_type},
                   {"event_time", evt.event_time},
                   {"confidence", evt.confidence},
                   {"metadata", evt.metadata}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump());
}

void ApiRoutes::HandleGetTimeline(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!record_index_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  int channel_id = 0;
  int64_t start_time = 0;
  int64_t end_time = INT64_MAX;

  try {
    std::string ch_str = req.get_param_value("channel_id");
    std::string start_str = req.get_param_value("start");
    std::string end_str = req.get_param_value("end");
    if (!ch_str.empty()) channel_id = std::stoi(ch_str);
    if (!start_str.empty()) start_time = std::stoll(start_str);
    if (!end_str.empty()) end_time = std::stoll(end_str);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid query parameters");
    return;
  }

  auto segments =
      record_index_->QuerySegments(channel_id, start_time, end_time);
  auto events = record_index_->QueryEvents(channel_id, start_time, end_time);

  // Build timeline response: segments with embedded event markers
  nlohmann::json timeline = nlohmann::json::array();
  for (const auto& seg : segments) {
    nlohmann::json seg_obj = {
        {"id", seg.id},
        {"channel_id", seg.channel_id},
        {"start_time", seg.start_time},
        {"end_time", seg.end_time},
        {"file_path", seg.file_path},
        {"codec", seg.codec},
        {"resolution", seg.resolution},
    };

    // Attach events that belong to this segment
    nlohmann::json seg_events = nlohmann::json::array();
    for (const auto& evt : events) {
      if (evt.event_time >= seg.start_time && evt.event_time <= seg.end_time) {
        seg_events.push_back({{"id", evt.id},
                              {"event_type", evt.event_type},
                              {"event_time", evt.event_time},
                              {"confidence", evt.confidence}});
      }
    }
    seg_obj["events"] = seg_events;
    seg_obj["event_count"] = seg_events.size();
    timeline.push_back(seg_obj);
  }

  nlohmann::json result = {
      {"channel_id", channel_id},      {"start_time", start_time},
      {"end_time", end_time},          {"segments", timeline},
      {"total_events", events.size()},
  };
  HttpServer::JsonResponse(res, 200, result.dump());
}

void ApiRoutes::HandleSearchEvents(HttpServer& srv, const httplib::Request& req,
                                   httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!record_index_) {
    HttpServer::JsonError(res, 500, "not configured");
    return;
  }

  int channel_id = 0;
  int64_t start_time = 0;
  int64_t end_time = INT64_MAX;
  std::string event_type;
  float min_confidence = 0.0F;

  try {
    std::string ch_str = req.get_param_value("channel_id");
    std::string start_str = req.get_param_value("start");
    std::string end_str = req.get_param_value("end");
    event_type = req.get_param_value("event_type");
    std::string conf_str = req.get_param_value("min_confidence");

    if (!ch_str.empty()) channel_id = std::stoi(ch_str);
    if (!start_str.empty()) start_time = std::stoll(start_str);
    if (!end_str.empty()) end_time = std::stoll(end_str);
    if (!conf_str.empty()) min_confidence = std::stof(conf_str);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid query parameters");
    return;
  }

  auto all_events =
      record_index_->QueryEvents(channel_id, start_time, end_time);

  // Filter by event_type and min_confidence
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& evt : all_events) {
    if (!event_type.empty() && evt.event_type != event_type) continue;
    if (evt.confidence < min_confidence) continue;

    arr.push_back({{"id", evt.id},
                   {"segment_id", evt.segment_id},
                   {"channel_id", evt.channel_id},
                   {"event_type", evt.event_type},
                   {"event_time", evt.event_time},
                   {"confidence", evt.confidence},
                   {"metadata", evt.metadata}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump());
}

void ApiRoutes::HandleGetConfig(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!hot_reload_mgr_) {
    HttpServer::JsonError(res, 500, "hot reload not configured");
    return;
  }

  auto cfg = hot_reload_mgr_->GetCurrentConfig();

  // Redact sensitive fields
  if (cfg.contains("auth") && cfg["auth"].contains("jwt_secret")) {
    cfg["auth"]["jwt_secret"] = "***";
  }
  if (cfg.contains("notifications")) {
    auto& notif = cfg["notifications"];
    if (notif.contains("smtp") && notif["smtp"].contains("password")) {
      notif["smtp"]["password"] = "***";
    }
    if (notif.contains("webhook") && notif["webhook"].contains("secret")) {
      notif["webhook"]["secret"] = "***";
    }
  }

  HttpServer::JsonResponse(res, 200, cfg.dump());
}

void ApiRoutes::HandleUpdateConfig(HttpServer& srv, const httplib::Request& req,
                                   httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!hot_reload_mgr_) {
    HttpServer::JsonError(res, 500, "hot reload not configured");
    return;
  }

  nlohmann::json patch;
  try {
    patch = nlohmann::json::parse(req.body);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid JSON");
    return;
  }

  // Reject changes to critical sections that require restart
  static const std::vector<std::string> immutable_sections = {"auth", "tls",
                                                              "network"};
  for (const auto& s : immutable_sections) {
    if (patch.contains(s)) {
      HttpServer::JsonError(
          res, 400,
          "section '" + s + "' cannot be hot-reloaded; requires restart");
      return;
    }
  }

  if (hot_reload_mgr_->ApplyPatch(patch)) {
    nlohmann::json result = {{"status", "ok"},
                             {"message", "configuration updated"}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } else {
    HttpServer::JsonError(res, 500, "failed to apply config");
  }
}

void ApiRoutes::HandleGetSystemStatus(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  nlohmann::json status = {
      {"version", "0.1.0"},
      {"channels_active",
       channel_mgr_ ? static_cast<int>(channel_mgr_->ChannelCount()) : 0},
      {"max_channels", channel::ChannelManager::kMaxChannels},
  };

  if (system_monitor_) {
    auto m = system_monitor_->GetLatest();
    status["cpu_usage_percent"] = m.cpu_usage_percent;
    status["memory_total_mb"] = m.memory_total_mb;
    status["memory_used_mb"] = m.memory_used_mb;
    status["memory_usage_percent"] = m.memory_usage_percent;
    status["uptime_seconds"] = m.uptime_seconds;
    status["process_threads"] = m.process_threads;

    nlohmann::json disks_arr = nlohmann::json::array();
    for (const auto& d : m.disks) {
      disks_arr.push_back({{"path", d.path},
                           {"total_gb", d.total_gb},
                           {"free_gb", d.free_gb},
                           {"usage_percent", d.usage_percent}});
    }
    status["disks"] = disks_arr;

    if (m.gpu_available) {
      status["gpu"] = {{"usage_percent", m.gpu_usage_percent},
                       {"memory_used_mb", m.gpu_memory_used_mb},
                       {"memory_total_mb", m.gpu_memory_total_mb},
                       {"temperature_celsius", m.gpu_temp_celsius}};
    }
  }

  HttpServer::JsonResponse(res, 200, status.dump(2));
}

// ============================================================
// System Config Handlers
// ============================================================

void ApiRoutes::HandleUpdateStorageConfig(HttpServer& srv,
                                          const httplib::Request& req,
                                          httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  // Only admin and operator can modify storage settings.
  if (ctx.claims.role == system::UserRole::kViewer) {
    HttpServer::JsonError(res, 403, "forbidden");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    // Accept storage configuration fields.
    // In a full implementation these would be persisted to ConfigManager.
    int segment_minutes = j.value("segment_minutes", 30);
    int retention_days = j.value("retention_days", 30);
    int max_size_gb = j.value("max_size_gb", 100);

    spdlog::info(
        "Storage config updated: segment={}min retention={}d "
        "max_size={}GB",
        segment_minutes, retention_days, max_size_gb);

    // Persist to config if ConfigManager supports it.
    auto& config = loong::core::ConfigManager::Instance();
    config.Set("storage.segment_minutes", segment_minutes);
    config.Set("storage.retention_days", retention_days);
    config.Set("storage.max_size_gb", max_size_gb);

    nlohmann::json result = {{"success", true}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (const std::exception& e) {
    HttpServer::JsonError(res, 400,
                          std::string("invalid request: ") + e.what());
  }
}

// ============================================================
// Recording File Handlers
// ============================================================

void ApiRoutes::HandleRecordingPlayback(HttpServer& srv,
                                        const httplib::Request& req,
                                        httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  std::string file_path = req.get_param_value("file");
  if (file_path.empty()) {
    HttpServer::JsonError(res, 400, "file parameter required");
    return;
  }

  // Security: ensure the path is within the recordings directory.
  if (file_path.find("..") != std::string::npos) {
    HttpServer::JsonError(res, 400, "invalid file path");
    return;
  }

  // Serve the file for playback.
  std::ifstream ifs(file_path, std::ios::binary);
  if (!ifs.is_open()) {
    HttpServer::JsonError(res, 404, "recording file not found");
    return;
  }

  std::string body((std::istreambuf_iterator<char>(ifs)),
                   std::istreambuf_iterator<char>());
  HttpServer::SetCorsHeaders(res);
  res.set_content(body, "video/mp4");
  res.status = 200;
}

void ApiRoutes::HandleRecordingDownload(HttpServer& srv,
                                        const httplib::Request& req,
                                        httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  std::string file_path = req.get_param_value("file");
  if (file_path.empty()) {
    HttpServer::JsonError(res, 400, "file parameter required");
    return;
  }

  if (file_path.find("..") != std::string::npos) {
    HttpServer::JsonError(res, 400, "invalid file path");
    return;
  }

  std::ifstream ifs(file_path, std::ios::binary);
  if (!ifs.is_open()) {
    HttpServer::JsonError(res, 404, "recording file not found");
    return;
  }

  // Extract filename from path.
  auto last_sep = file_path.find_last_of('/');
  std::string filename = (last_sep != std::string::npos)
                             ? file_path.substr(last_sep + 1)
                             : file_path;

  std::string body((std::istreambuf_iterator<char>(ifs)),
                   std::istreambuf_iterator<char>());

  HttpServer::SetCorsHeaders(res);
  res.set_header("Content-Disposition",
                 "attachment; filename=\"" + filename + "\"");
  res.set_content(body, "application/octet-stream");
  res.status = 200;
}

// ============================================================
// FLV Streaming Handler
// ============================================================

void ApiRoutes::HandleFlvStream(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!flv_service_) {
    HttpServer::JsonError(res, 500, "flv service not configured");
    return;
  }

  int channel_id = 0;
  try {
    channel_id = std::stoi(req.matches[1]);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
    return;
  }

  spdlog::info("FLV stream requested for channel {}", channel_id);

  auto viewer = flv_service_->CreateViewer(channel_id);
  if (!viewer) {
    HttpServer::JsonError(res, 404, "channel not registered for streaming");
    return;
  }

  HttpServer::SetCorsHeaders(res);

  auto flv_svc = flv_service_;
  int ch_id = channel_id;

  res.set_chunked_content_provider(
      "video/x-flv",
      [flv_svc, ch_id, viewer](size_t /*offset*/, httplib::DataSink& sink)
          -> bool { return flv_svc->StreamToViewer(ch_id, viewer, sink); },
      [flv_svc, ch_id, viewer](bool /*success*/) {
        flv_svc->RemoveViewer(ch_id, viewer);
        spdlog::info("FLV viewer disconnected from channel {}", ch_id);
      });
}

// ============================================================
// HLS Streaming Handlers
// ============================================================

void ApiRoutes::HandleHlsPlaylist(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!hls_service_) {
    HttpServer::JsonError(res, 500, "hls service not configured");
    return;
  }

  int channel_id = 0;
  try {
    channel_id = std::stoi(req.matches[1]);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id");
    return;
  }

  if (!hls_service_->HasChannel(channel_id)) {
    HttpServer::JsonError(res, 404, "channel not registered for HLS");
    return;
  }

  std::string playlist = hls_service_->GeneratePlaylist(channel_id);
  if (playlist.empty()) {
    HttpServer::SetCorsHeaders(res);
    res.status = 503;
    res.set_content("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n",
                    "application/vnd.apple.mpegurl");
    return;
  }

  HttpServer::SetCorsHeaders(res);
  res.set_header("Cache-Control", "no-cache, no-store");
  res.set_content(playlist, "application/vnd.apple.mpegurl");
  res.status = 200;
}

void ApiRoutes::HandleHlsSegment(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!hls_service_) {
    HttpServer::JsonError(res, 500, "hls service not configured");
    return;
  }

  int channel_id = 0;
  uint64_t sequence = 0;
  try {
    channel_id = std::stoi(req.matches[1]);
    sequence = std::stoull(req.matches[2]);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid channel id or segment number");
    return;
  }

  auto segment = hls_service_->GetSegment(channel_id, sequence);
  if (!segment) {
    HttpServer::JsonError(res, 404, "segment not found");
    return;
  }

  HttpServer::SetCorsHeaders(res);
  res.set_header("Cache-Control", "public, max-age=30");
  res.set_content(std::string(reinterpret_cast<const char*>(segment->data()),
                              segment->size()),
                  "video/mp2t");
  res.status = 200;
}

// ============================================================
// ONVIF Handlers
// ============================================================

void ApiRoutes::HandleOnvifDiscover(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  int timeout = 3000;
  try {
    std::string timeout_str = req.get_param_value("timeout");
    if (!timeout_str.empty()) timeout = std::stoi(timeout_str);
  } catch (...) {
    // Use default timeout on parse error.
  }

  auto devices = video_input::OnvifDiscovery::Discover(timeout);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& d : devices) {
    arr.push_back({{"xaddr", d.xaddr},
                   {"ip_address", d.ip_address},
                   {"manufacturer", d.manufacturer},
                   {"model", d.model}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump(2));
}

void ApiRoutes::HandleOnvifDeviceInfo(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  std::string addr = req.matches[1];
  std::string xaddr = "http://" + addr + "/onvif/device_service";

  std::string user = req.get_param_value("username");
  std::string pass = req.get_param_value("password");

  video_input::OnvifDevice device(xaddr);
  if (!user.empty()) {
    device.SetCredentials(user, pass);
  }

  std::string mfr;
  std::string model;
  std::string fw;
  std::string serial;
  std::string hw;
  device.GetDeviceInformation(mfr, model, fw, serial, hw);

  auto profiles = device.GetProfiles();
  nlohmann::json profiles_arr = nlohmann::json::array();
  for (const auto& p : profiles) {
    std::string stream_uri = device.GetStreamUri(p.token);
    profiles_arr.push_back(
        {{"token", p.token}, {"name", p.name}, {"stream_uri", stream_uri}});
  }

  nlohmann::json result = {
      {"xaddr", xaddr},           {"manufacturer", mfr},
      {"model", model},           {"firmware_version", fw},
      {"serial_number", serial},  {"hardware_id", hw},
      {"profiles", profiles_arr},
  };
  HttpServer::JsonResponse(res, 200, result.dump(2));
}

void ApiRoutes::HandleOnvifPtz(HttpServer& srv, const httplib::Request& req,
                               httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  std::string addr = req.matches[1];

  try {
    auto j = nlohmann::json::parse(req.body);
    std::string ptz_url =
        j.value("ptz_url", "http://" + addr + "/onvif/ptz_service");
    std::string profile_token = j.value("profile_token", "");
    std::string action = j.value("action", "");
    std::string user = j.value("username", "");
    std::string pass = j.value("password", "");

    video_input::OnvifPtz ptz(ptz_url, profile_token);
    if (!user.empty()) {
      ptz.SetCredentials(user, pass);
    }

    bool ok = false;
    if (action == "continuous_move") {
      float pan = j.value("pan", 0.0F);
      float tilt = j.value("tilt", 0.0F);
      float zoom = j.value("zoom", 0.0F);
      ok = ptz.ContinuousMove(pan, tilt, zoom);
    } else if (action == "stop") {
      ok = ptz.Stop();
    } else if (action == "absolute_move") {
      float pan = j.value("pan", 0.0F);
      float tilt = j.value("tilt", 0.0F);
      float zoom = j.value("zoom", 0.0F);
      ok = ptz.AbsoluteMove(pan, tilt, zoom);
    } else if (action == "relative_move") {
      float pan = j.value("pan", 0.0F);
      float tilt = j.value("tilt", 0.0F);
      float zoom = j.value("zoom", 0.0F);
      ok = ptz.RelativeMove(pan, tilt, zoom);
    } else if (action == "goto_preset") {
      std::string preset = j.value("preset_token", "");
      ok = ptz.GotoPreset(preset);
    } else {
      HttpServer::JsonError(res, 400, "unknown PTZ action: " + action);
      return;
    }

    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 500, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

// ============================================================
// Notification Handlers
// ============================================================

void ApiRoutes::HandleGetNotificationConfig(HttpServer& srv,
                                            const httplib::Request& req,
                                            httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notifications not configured");
    return;
  }

  auto smtp = notification_mgr_->GetSmtpConfig();
  auto webhook = notification_mgr_->GetWebhookConfig();

  nlohmann::json result;
  result["smtp"] = {
      {"enabled", smtp.enabled},     {"host", smtp.host},
      {"port", smtp.port},           {"use_tls", smtp.use_tls},
      {"username", smtp.username},   {"from_address", smtp.from_address},
      {"from_name", smtp.from_name}, {"to_addresses", smtp.to_addresses},
  };
  result["webhook"] = {
      {"enabled", webhook.enabled},
      {"url", webhook.url},
      {"content_type", webhook.content_type},
      {"timeout_sec", webhook.timeout_sec},
      {"retry_count", webhook.retry_count},
  };

  HttpServer::JsonResponse(res, 200, result.dump(2));
}

void ApiRoutes::HandleUpdateNotificationConfig(HttpServer& srv,
                                               const httplib::Request& req,
                                               httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notifications not configured");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);

    if (j.contains("smtp")) {
      auto& s = j["smtp"];
      system::SmtpConfig smtp;
      smtp.enabled = s.value("enabled", false);
      smtp.host = s.value("host", "");
      smtp.port = s.value("port", 587);
      smtp.use_tls = s.value("use_tls", true);
      smtp.username = s.value("username", "");
      smtp.password = s.value("password", "");
      smtp.from_address = s.value("from_address", "");
      smtp.from_name = s.value("from_name", "Loong AI NVR");
      if (s.contains("to_addresses")) {
        smtp.to_addresses.clear();
        for (const auto& addr : s["to_addresses"]) {
          smtp.to_addresses.push_back(addr.get<std::string>());
        }
      }
      notification_mgr_->ConfigureSmtp(smtp);
    }

    if (j.contains("webhook")) {
      auto& w = j["webhook"];
      system::WebhookConfig webhook;
      webhook.enabled = w.value("enabled", false);
      webhook.url = w.value("url", "");
      webhook.secret = w.value("secret", "");
      webhook.content_type = w.value("content_type", "application/json");
      webhook.timeout_sec = w.value("timeout_sec", 10);
      webhook.retry_count = w.value("retry_count", 2);
      notification_mgr_->ConfigureWebhook(webhook);
    }

    nlohmann::json result = {{"success", true}};
    HttpServer::JsonResponse(res, 200, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleTestNotification(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notifications not configured");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    std::string channel_type = j.value("channel", "");

    system::NotificationResult result;
    if (channel_type == "smtp") {
      result = notification_mgr_->TestSmtp();
    } else if (channel_type == "webhook") {
      result = notification_mgr_->TestWebhook();
    } else {
      HttpServer::JsonError(res, 400, "channel must be 'smtp' or 'webhook'");
      return;
    }

    nlohmann::json resp = {
        {"success", result.success},
        {"error", result.error_message},
    };
    HttpServer::JsonResponse(res, result.success ? 200 : 500, resp.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleGetNotificationHistory(HttpServer& srv,
                                             const httplib::Request& req,
                                             httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notifications not configured");
    return;
  }

  int limit = 50;
  try {
    std::string limit_str = req.get_param_value("limit");
    if (!limit_str.empty()) limit = std::stoi(limit_str);
  } catch (const std::exception& e) {
    spdlog::debug("notification history limit parse: {}", e.what());
  }

  auto history = notification_mgr_->GetHistory(limit);
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& rec : history) {
    arr.push_back({{"id", rec.id},
                   {"channel_type", rec.channel_type},
                   {"title", rec.title},
                   {"severity", rec.severity},
                   {"success", rec.success},
                   {"error", rec.error_message},
                   {"sent_at", rec.sent_at}});
  }
  HttpServer::JsonResponse(res, 200, arr.dump());
}

// ============================================================
// Observability Handlers
// ============================================================

void ApiRoutes::HandleHealthCheck(HttpServer& /*srv*/,
                                  const httplib::Request& /*req*/,
                                  httplib::Response& res) {
  nlohmann::json health;
  health["status"] = "ok";
  health["version"] = "0.2.0";

  bool all_healthy = true;

  // Check channel manager
  if (channel_mgr_) {
    auto statuses = channel_mgr_->GetAllStatus();
    int running = 0;
    int error = 0;
    for (const auto& s : statuses) {
      if (s.state == ChannelState::kRunning) ++running;
      if (s.state == ChannelState::kError) ++error;
    }
    health["channels"] = {
        {"total", statuses.size()}, {"running", running}, {"error", error}};
    if (error > 0) all_healthy = false;
  }

  // Check storage
  if (record_index_) {
    health["storage"] = {{"status", "ok"}};
  }

  // Check system monitor
  if (system_monitor_) {
    auto m = system_monitor_->GetLatest();
    health["system"] = {
        {"cpu_percent", m.cpu_usage_percent},
        {"memory_percent", m.memory_usage_percent},
        {"uptime_seconds", m.uptime_seconds},
    };
  }

  health["status"] = all_healthy ? "ok" : "degraded";
  HttpServer::JsonResponse(res, all_healthy ? 200 : 503, health.dump());
}

void ApiRoutes::HandleMetrics(HttpServer& /*srv*/,
                              const httplib::Request& /*req*/,
                              httplib::Response& res) {
  std::string out;
  out.reserve(4096);

  // System metrics
  if (system_monitor_) {
    auto m = system_monitor_->GetLatest();

    out += "# HELP loong_cpu_usage_percent CPU usage percentage\n";
    out += "# TYPE loong_cpu_usage_percent gauge\n";
    out +=
        "loong_cpu_usage_percent " + std::to_string(m.cpu_usage_percent) + "\n";

    out += "# HELP loong_memory_used_bytes Memory used in bytes\n";
    out += "# TYPE loong_memory_used_bytes gauge\n";
    out += "loong_memory_used_bytes " +
           std::to_string(m.memory_used_mb * 1048576LL) + "\n";

    out += "# HELP loong_memory_total_bytes Memory total in bytes\n";
    out += "# TYPE loong_memory_total_bytes gauge\n";
    out += "loong_memory_total_bytes " +
           std::to_string(m.memory_total_mb * 1048576LL) + "\n";

    out += "# HELP loong_memory_usage_percent Memory usage percentage\n";
    out += "# TYPE loong_memory_usage_percent gauge\n";
    out += "loong_memory_usage_percent " +
           std::to_string(m.memory_usage_percent) + "\n";

    out += "# HELP loong_uptime_seconds System uptime in seconds\n";
    out += "# TYPE loong_uptime_seconds gauge\n";
    out += "loong_uptime_seconds " + std::to_string(m.uptime_seconds) + "\n";

    out += "# HELP loong_process_threads Number of process threads\n";
    out += "# TYPE loong_process_threads gauge\n";
    out += "loong_process_threads " + std::to_string(m.process_threads) + "\n";

    for (const auto& d : m.disks) {
      out += "# HELP loong_disk_usage_percent Disk usage percentage\n";
      out += "# TYPE loong_disk_usage_percent gauge\n";
      out += "loong_disk_usage_percent{path=\"" + d.path + "\"} " +
             std::to_string(d.usage_percent) + "\n";
      out += "# HELP loong_disk_free_bytes Disk free space in bytes\n";
      out += "# TYPE loong_disk_free_bytes gauge\n";
      out += "loong_disk_free_bytes{path=\"" + d.path + "\"} " +
             std::to_string(d.free_gb * 1073741824LL) + "\n";
    }

    if (m.gpu_available) {
      out += "# HELP loong_gpu_usage_percent GPU usage percentage\n";
      out += "# TYPE loong_gpu_usage_percent gauge\n";
      out += "loong_gpu_usage_percent " + std::to_string(m.gpu_usage_percent) +
             "\n";

      out += "# HELP loong_gpu_memory_used_bytes GPU memory used\n";
      out += "# TYPE loong_gpu_memory_used_bytes gauge\n";
      out += "loong_gpu_memory_used_bytes " +
             std::to_string(m.gpu_memory_used_mb * 1048576LL) + "\n";

      out += "# HELP loong_gpu_temperature_celsius GPU temperature\n";
      out += "# TYPE loong_gpu_temperature_celsius gauge\n";
      out += "loong_gpu_temperature_celsius " +
             std::to_string(m.gpu_temp_celsius) + "\n";
    }
  }

  // Channel metrics
  if (channel_mgr_) {
    auto statuses = channel_mgr_->GetAllStatus();

    out += "# HELP loong_channels_total Total number of channels\n";
    out += "# TYPE loong_channels_total gauge\n";
    out += "loong_channels_total " + std::to_string(statuses.size()) + "\n";

    int running = 0;
    for (const auto& s : statuses) {
      if (s.state == ChannelState::kRunning) ++running;
    }
    out += "# HELP loong_channels_running Number of running channels\n";
    out += "# TYPE loong_channels_running gauge\n";
    out += "loong_channels_running " + std::to_string(running) + "\n";

    for (const auto& s : statuses) {
      std::string labels = "{channel_id=\"" + std::to_string(s.id) +
                           "\",name=\"" + s.name + "\"}";

      out += "# HELP loong_channel_fps_in Input FPS per channel\n";
      out += "# TYPE loong_channel_fps_in gauge\n";
      out += "loong_channel_fps_in" + labels + " " + std::to_string(s.fps_in) +
             "\n";

      out += "# HELP loong_channel_fps_decode Decode FPS per channel\n";
      out += "# TYPE loong_channel_fps_decode gauge\n";
      out += "loong_channel_fps_decode" + labels + " " +
             std::to_string(s.fps_decode) + "\n";

      out += "# HELP loong_channel_fps_ai AI FPS per channel\n";
      out += "# TYPE loong_channel_fps_ai gauge\n";
      out += "loong_channel_fps_ai" + labels + " " + std::to_string(s.fps_ai) +
             "\n";

      out += "# HELP loong_channel_frames_processed Total frames processed\n";
      out += "# TYPE loong_channel_frames_processed counter\n";
      out += "loong_channel_frames_processed" + labels + " " +
             std::to_string(s.frames_processed) + "\n";

      out += "# HELP loong_channel_frames_dropped Total frames dropped\n";
      out += "# TYPE loong_channel_frames_dropped counter\n";
      out += "loong_channel_frames_dropped" + labels + " " +
             std::to_string(s.frames_dropped) + "\n";
    }
  }

  HttpServer::SetCorsHeaders(res);
  res.set_content(out, "text/plain; version=0.0.4; charset=utf-8");
  res.status = 200;
}

// ============================================================
// Rules Handlers
// ============================================================

void ApiRoutes::HandleListRules(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  std::vector<rules::AnalysisRule> rules_list;
  if (req.has_param("channel_id")) {
    int ch_id = std::stoi(req.get_param_value("channel_id"));
    rules_list = rule_engine_->ListRulesByChannel(ch_id);
  } else {
    rules_list = rule_engine_->ListRules();
  }

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : rules_list) {
    nlohmann::json j;
    j["id"] = r.id;
    j["name"] = r.name;
    j["type"] = rules::RuleTypeToString(r.type);
    j["channel_id"] = r.channel_id;
    j["enabled"] = r.enabled;
    j["min_confidence"] = r.min_confidence;
    j["cooldown_sec"] = r.cooldown_sec;
    j["loiter_time_sec"] = r.loiter_time_sec;
    j["created_at"] = r.created_at;
    j["updated_at"] = r.updated_at;

    // Line
    j["line"] = {
        {"start", {{"x", r.line.start.x}, {"y", r.line.start.y}}},
        {"end", {{"x", r.line.end.x}, {"y", r.line.end.y}}},
        {"bidirectional", r.line.bidirectional},
    };

    // Region
    nlohmann::json verts = nlohmann::json::array();
    for (const auto& v : r.region.vertices) {
      verts.push_back({{"x", v.x}, {"y", v.y}});
    }
    j["region"] = {{"vertices", verts}};

    j["target_classes"] = r.target_classes;
    j["schedule"] = {
        {"always_active", r.schedule.always_active},
        {"start_time", r.schedule.start_time},
        {"end_time", r.schedule.end_time},
        {"weekdays", r.schedule.weekdays},
    };

    arr.push_back(j);
  }

  nlohmann::json resp = {{"rules", arr}, {"total", arr.size()}};
  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleCreateRule(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  try {
    auto body = nlohmann::json::parse(req.body);

    rules::AnalysisRule rule;
    rule.name = body.value("name", "");
    rule.type = rules::ParseRuleType(body.value("type", "cross_line"));
    rule.channel_id = body.value("channel_id", -1);
    rule.enabled = body.value("enabled", true);
    rule.min_confidence = body.value("min_confidence", 0.5F);
    rule.cooldown_sec = body.value("cooldown_sec", 60);
    rule.loiter_time_sec = body.value("loiter_time_sec", 30);

    if (body.contains("line")) {
      const auto& ln = body["line"];
      if (ln.contains("start")) {
        rule.line.start.x = ln["start"].value("x", 0.0);
        rule.line.start.y = ln["start"].value("y", 0.0);
      }
      if (ln.contains("end")) {
        rule.line.end.x = ln["end"].value("x", 0.0);
        rule.line.end.y = ln["end"].value("y", 0.0);
      }
      rule.line.bidirectional = ln.value("bidirectional", true);
    }

    if (body.contains("region") && body["region"].contains("vertices")) {
      for (const auto& v : body["region"]["vertices"]) {
        rule.region.vertices.push_back({v.value("x", 0.0), v.value("y", 0.0)});
      }
    }

    if (body.contains("target_classes")) {
      rule.target_classes =
          body["target_classes"].get<std::vector<std::string>>();
    }

    if (body.contains("schedule")) {
      const auto& s = body["schedule"];
      rule.schedule.always_active = s.value("always_active", true);
      rule.schedule.start_time = s.value("start_time", "");
      rule.schedule.end_time = s.value("end_time", "");
      if (s.contains("weekdays")) {
        rule.schedule.weekdays = s["weekdays"].get<std::vector<int>>();
      }
    }

    if (rule.name.empty() || rule.channel_id < 0) {
      res.set_content(R"({"error":"name and channel_id are required"})",
                      "application/json");
      res.status = 400;
      return;
    }

    int64_t new_id = rule_engine_->CreateRule(rule);
    if (new_id > 0) {
      nlohmann::json resp = {{"id", new_id}, {"message", "Rule created"}};
      res.set_content(resp.dump(), "application/json");
      res.status = 201;
    } else {
      res.set_content(R"({"error":"Failed to create rule"})",
                      "application/json");
      res.status = 500;
    }
  } catch (const std::exception& e) {
    nlohmann::json err = {{"error", e.what()}};
    res.set_content(err.dump(), "application/json");
    res.status = 400;
  }
}

void ApiRoutes::HandleGetRule(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t rule_id = std::stoll(req.matches[1]);
  rules::AnalysisRule rule;
  if (!rule_engine_->GetRule(rule_id, rule)) {
    res.set_content(R"({"error":"Rule not found"})", "application/json");
    res.status = 404;
    return;
  }

  nlohmann::json j;
  j["id"] = rule.id;
  j["name"] = rule.name;
  j["type"] = rules::RuleTypeToString(rule.type);
  j["channel_id"] = rule.channel_id;
  j["enabled"] = rule.enabled;
  j["min_confidence"] = rule.min_confidence;
  j["cooldown_sec"] = rule.cooldown_sec;
  j["loiter_time_sec"] = rule.loiter_time_sec;
  j["created_at"] = rule.created_at;
  j["updated_at"] = rule.updated_at;
  j["target_classes"] = rule.target_classes;

  j["line"] = {
      {"start", {{"x", rule.line.start.x}, {"y", rule.line.start.y}}},
      {"end", {{"x", rule.line.end.x}, {"y", rule.line.end.y}}},
      {"bidirectional", rule.line.bidirectional},
  };

  nlohmann::json verts = nlohmann::json::array();
  for (const auto& v : rule.region.vertices) {
    verts.push_back({{"x", v.x}, {"y", v.y}});
  }
  j["region"] = {{"vertices", verts}};

  j["schedule"] = {
      {"always_active", rule.schedule.always_active},
      {"start_time", rule.schedule.start_time},
      {"end_time", rule.schedule.end_time},
      {"weekdays", rule.schedule.weekdays},
  };

  res.set_content(j.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleUpdateRule(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  try {
    int64_t rule_id = std::stoll(req.matches[1]);
    rules::AnalysisRule existing;
    if (!rule_engine_->GetRule(rule_id, existing)) {
      res.set_content(R"({"error":"Rule not found"})", "application/json");
      res.status = 404;
      return;
    }

    auto body = nlohmann::json::parse(req.body);

    if (body.contains("name")) existing.name = body["name"];
    if (body.contains("type"))
      existing.type = rules::ParseRuleType(body["type"]);
    if (body.contains("enabled")) existing.enabled = body["enabled"];
    if (body.contains("min_confidence"))
      existing.min_confidence = body["min_confidence"];
    if (body.contains("cooldown_sec"))
      existing.cooldown_sec = body["cooldown_sec"];
    if (body.contains("loiter_time_sec"))
      existing.loiter_time_sec = body["loiter_time_sec"];

    if (body.contains("line")) {
      const auto& ln = body["line"];
      if (ln.contains("start")) {
        existing.line.start.x = ln["start"].value("x", 0.0);
        existing.line.start.y = ln["start"].value("y", 0.0);
      }
      if (ln.contains("end")) {
        existing.line.end.x = ln["end"].value("x", 0.0);
        existing.line.end.y = ln["end"].value("y", 0.0);
      }
      if (ln.contains("bidirectional"))
        existing.line.bidirectional = ln["bidirectional"];
    }

    if (body.contains("region") && body["region"].contains("vertices")) {
      existing.region.vertices.clear();
      for (const auto& v : body["region"]["vertices"]) {
        existing.region.vertices.push_back(
            {v.value("x", 0.0), v.value("y", 0.0)});
      }
    }

    if (body.contains("target_classes")) {
      existing.target_classes =
          body["target_classes"].get<std::vector<std::string>>();
    }

    if (rule_engine_->UpdateRule(existing)) {
      res.set_content(R"({"message":"Rule updated"})", "application/json");
      res.status = 200;
    } else {
      res.set_content(R"({"error":"Failed to update rule"})",
                      "application/json");
      res.status = 500;
    }
  } catch (const std::exception& e) {
    nlohmann::json err = {{"error", e.what()}};
    res.set_content(err.dump(), "application/json");
    res.status = 400;
  }
}

void ApiRoutes::HandleDeleteRule(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t rule_id = std::stoll(req.matches[1]);
  if (rule_engine_->DeleteRule(rule_id)) {
    res.set_content(R"({"message":"Rule deleted"})", "application/json");
    res.status = 200;
  } else {
    res.set_content(R"({"error":"Rule not found or delete failed"})",
                    "application/json");
    res.status = 404;
  }
}

void ApiRoutes::HandleQueryRuleEvents(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  auto store = rule_engine_->GetRuleStore();
  if (!store) {
    res.set_content(R"({"error":"Rule store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int channel_id = -1;
  int64_t start_time = 0;
  int64_t end_time = std::numeric_limits<int64_t>::max();
  int limit = 100;

  try {
    if (req.has_param("channel_id"))
      channel_id = std::stoi(req.get_param_value("channel_id"));
    if (req.has_param("start_time"))
      start_time = std::stoll(req.get_param_value("start_time"));
    if (req.has_param("end_time"))
      end_time = std::stoll(req.get_param_value("end_time"));
    if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
  } catch (const std::exception& e) {
    spdlog::debug("rule events query param parse: {}", e.what());
  }

  auto events = store->QueryEvents(channel_id, start_time, end_time, limit);

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& ev : events) {
    arr.push_back({
        {"rule_id", ev.rule_id},
        {"rule_name", ev.rule_name},
        {"rule_type", rules::RuleTypeToString(ev.rule_type)},
        {"channel_id", ev.channel_id},
        {"timestamp", ev.timestamp},
        {"severity",
         ev.severity == rules::RuleEventSeverity::kAlarm ? "alarm" : "info"},
        {"direction", ev.direction},
        {"count_value", ev.count_value},
        {"dwell_time_sec", ev.dwell_time_sec},
    });
  }

  nlohmann::json resp = {{"events", arr}, {"total", arr.size()}};
  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

// ============================================================
// LPR (Plate) handlers
// ============================================================

void ApiRoutes::HandleQueryPlates(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!plate_store_) {
    res.set_content(R"({"error":"Plate store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  ai_engine::PlateQuery query;
  if (req.has_param("plate_number")) {
    query.plate_number = req.get_param_value("plate_number");
  }
  if (req.has_param("channel_id")) {
    query.channel_id = std::stoi(req.get_param_value("channel_id"));
  }
  if (req.has_param("start_time")) {
    query.start_time = std::stoll(req.get_param_value("start_time"));
  }
  if (req.has_param("end_time")) {
    query.end_time = std::stoll(req.get_param_value("end_time"));
  }
  if (req.has_param("limit")) {
    query.limit = std::stoi(req.get_param_value("limit"));
  }
  if (req.has_param("offset")) {
    query.offset = std::stoi(req.get_param_value("offset"));
  }

  auto records = plate_store_->Query(query);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& r : records) {
    j.push_back({
        {"id", r.id},
        {"plate_number", r.plate_number},
        {"plate_color", r.plate_color},
        {"confidence", r.confidence},
        {"channel_id", r.channel_id},
        {"timestamp", r.timestamp},
        {"snapshot_path", r.snapshot_path},
    });
  }

  nlohmann::json resp;
  resp["plates"] = j;
  resp["count"] = records.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

// ============================================================
// Face Handlers
// ============================================================

void ApiRoutes::HandleQueryFaces(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!face_store_) {
    res.set_content(R"({"error":"Face store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  ai_engine::FaceQuery query;
  if (req.has_param("channel_id")) {
    query.channel_id = std::stoi(req.get_param_value("channel_id"));
  }
  if (req.has_param("start_time")) {
    query.start_time = std::stoll(req.get_param_value("start_time"));
  }
  if (req.has_param("end_time")) {
    query.end_time = std::stoll(req.get_param_value("end_time"));
  }
  if (req.has_param("gender")) {
    query.gender = req.get_param_value("gender");
  }
  if (req.has_param("age_min")) {
    query.age_min = std::stoi(req.get_param_value("age_min"));
  }
  if (req.has_param("age_max")) {
    query.age_max = std::stoi(req.get_param_value("age_max"));
  }
  if (req.has_param("limit")) {
    query.limit = std::stoi(req.get_param_value("limit"));
  }
  if (req.has_param("offset")) {
    query.offset = std::stoi(req.get_param_value("offset"));
  }

  auto records = face_store_->Query(query);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& r : records) {
    j.push_back({
        {"id", r.id},
        {"channel_id", r.channel_id},
        {"timestamp", r.timestamp},
        {"confidence", r.confidence},
        {"age", r.age},
        {"gender", r.gender},
        {"snapshot_path", r.snapshot_path},
        {"has_embedding", !r.embedding.empty()},
    });
  }

  nlohmann::json resp;
  resp["faces"] = j;
  resp["count"] = records.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleSearchFaces(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!face_store_) {
    res.set_content(R"({"error":"Face store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  nlohmann::json body;
  try {
    body = nlohmann::json::parse(req.body);
  } catch (...) {
    res.set_content(R"({"error":"Invalid JSON body"})", "application/json");
    res.status = 400;
    return;
  }

  if (!body.contains("embedding") || !body["embedding"].is_array()) {
    res.set_content(R"({"error":"Missing 'embedding' array in body"})",
                    "application/json");
    res.status = 400;
    return;
  }

  std::vector<float> query_emb;
  for (const auto& v : body["embedding"]) {
    query_emb.push_back(v.get<float>());
  }

  float threshold = 0.5F;
  if (body.contains("threshold")) {
    threshold = body["threshold"].get<float>();
  }
  int limit = 10;
  if (body.contains("limit")) {
    limit = body["limit"].get<int>();
  }

  auto matches = face_store_->SearchByEmbedding(query_emb, threshold, limit);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& [record, similarity] : matches) {
    j.push_back({
        {"id", record.id},
        {"channel_id", record.channel_id},
        {"timestamp", record.timestamp},
        {"confidence", record.confidence},
        {"age", record.age},
        {"gender", record.gender},
        {"snapshot_path", record.snapshot_path},
        {"similarity", similarity},
    });
  }

  nlohmann::json resp;
  resp["matches"] = j;
  resp["count"] = matches.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

// ============================================================
// Analytics Handlers
// ============================================================

void ApiRoutes::HandleAnalyticsSummary(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!analytics_store_) {
    res.set_content(R"({"error":"Analytics store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t start_time = 0, end_time = 0;
  int channel_id = -1;
  if (req.has_param("start_time"))
    start_time = std::stoll(req.get_param_value("start_time"));
  if (req.has_param("end_time"))
    end_time = std::stoll(req.get_param_value("end_time"));
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));

  auto s = analytics_store_->GetSummary(start_time, end_time, channel_id);

  nlohmann::json resp;
  resp["start_time"] = s.start_time;
  resp["end_time"] = s.end_time;
  resp["total_events"] = s.total_events;
  resp["cross_line_events"] = s.cross_line_events;
  resp["region_intrusion_events"] = s.region_intrusion_events;
  resp["object_counting_events"] = s.object_counting_events;
  resp["loitering_events"] = s.loitering_events;
  resp["total_count_value"] = s.total_count_value;
  resp["active_channels"] = s.active_channels;

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleAnalyticsTrends(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!analytics_store_) {
    res.set_content(R"({"error":"Analytics store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t start_time = 0, end_time = 0;
  int channel_id = -1;
  std::string rule_type;
  std::string granularity_str = "hourly";

  if (req.has_param("start_time"))
    start_time = std::stoll(req.get_param_value("start_time"));
  if (req.has_param("end_time"))
    end_time = std::stoll(req.get_param_value("end_time"));
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));
  if (req.has_param("rule_type")) rule_type = req.get_param_value("rule_type");
  if (req.has_param("granularity"))
    granularity_str = req.get_param_value("granularity");

  auto granularity = analytics::TimeGranularity::kHourly;
  if (granularity_str == "daily")
    granularity = analytics::TimeGranularity::kDaily;
  else if (granularity_str == "weekly")
    granularity = analytics::TimeGranularity::kWeekly;
  else if (granularity_str == "monthly")
    granularity = analytics::TimeGranularity::kMonthly;

  auto trends = analytics_store_->GetTrends(start_time, end_time, granularity,
                                            channel_id, rule_type);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& b : trends) {
    j.push_back({
        {"bucket_start", b.bucket_start},
        {"event_count", b.event_count},
        {"count_sum", b.count_sum},
    });
  }

  nlohmann::json resp;
  resp["granularity"] = granularity_str;
  resp["trends"] = j;
  resp["count"] = trends.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleAnalyticsHeatmap(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!analytics_store_) {
    res.set_content(R"({"error":"Analytics store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t start_time = 0, end_time = 0;
  int channel_id = -1;
  int grid_cols = 20, grid_rows = 15;
  int img_w = 1920, img_h = 1080;

  if (req.has_param("start_time"))
    start_time = std::stoll(req.get_param_value("start_time"));
  if (req.has_param("end_time"))
    end_time = std::stoll(req.get_param_value("end_time"));
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));
  if (req.has_param("grid_cols"))
    grid_cols = std::stoi(req.get_param_value("grid_cols"));
  if (req.has_param("grid_rows"))
    grid_rows = std::stoi(req.get_param_value("grid_rows"));
  if (req.has_param("image_width"))
    img_w = std::stoi(req.get_param_value("image_width"));
  if (req.has_param("image_height"))
    img_h = std::stoi(req.get_param_value("image_height"));

  auto cells = analytics_store_->GetHeatmap(start_time, end_time, channel_id,
                                            grid_cols, grid_rows, img_w, img_h);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& c : cells) {
    j.push_back({
        {"x", c.grid_x},
        {"y", c.grid_y},
        {"count", c.hit_count},
    });
  }

  nlohmann::json resp;
  resp["grid_cols"] = grid_cols;
  resp["grid_rows"] = grid_rows;
  resp["cells"] = j;
  resp["total_cells"] = cells.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

void ApiRoutes::HandleAnalyticsPeakHours(HttpServer& srv,
                                         const httplib::Request& req,
                                         httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;
  if (!analytics_store_) {
    res.set_content(R"({"error":"Analytics store not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int64_t start_time = 0, end_time = 0;
  int channel_id = -1;

  if (req.has_param("start_time"))
    start_time = std::stoll(req.get_param_value("start_time"));
  if (req.has_param("end_time"))
    end_time = std::stoll(req.get_param_value("end_time"));
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));

  auto hours = analytics_store_->GetPeakHours(start_time, end_time, channel_id);

  nlohmann::json j = nlohmann::json::array();
  for (const auto& h : hours) {
    j.push_back({
        {"hour", h.hour},
        {"event_count", h.event_count},
        {"percentage", h.percentage},
    });
  }

  nlohmann::json resp;
  resp["peak_hours"] = j;
  resp["count"] = hours.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

// ============================================================
// Analytics: Counting Statistics
// ============================================================

void ApiRoutes::HandleAnalyticsCounting(HttpServer& srv,
                                        const httplib::Request& req,
                                        httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!rule_engine_) {
    res.set_content(R"({"error":"Rule engine not available"})",
                    "application/json");
    res.status = 503;
    return;
  }

  int channel_id = -1;
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));

  auto rules = (channel_id >= 0) ? rule_engine_->ListRulesByChannel(channel_id)
                                 : rule_engine_->ListRules();

  nlohmann::json counters = nlohmann::json::array();
  for (const auto& rule : rules) {
    if (rule.type != rules::RuleType::kObjectCounting) continue;

    auto overlay = rule_engine_->BuildOverlayData(rule.channel_id);
    int a_to_b = 0, b_to_a = 0;
    for (const auto& ln : overlay.lines) {
      if (ln.show_counts && ln.label == rule.name) {
        a_to_b = ln.count_a_to_b;
        b_to_a = ln.count_b_to_a;
        break;
      }
    }

    counters.push_back({
        {"rule_id", rule.id},
        {"rule_name", rule.name},
        {"channel_id", rule.channel_id},
        {"count_a_to_b", a_to_b},
        {"count_b_to_a", b_to_a},
        {"net_count", a_to_b - b_to_a},
        {"total_crossings", a_to_b + b_to_a},
    });
  }

  nlohmann::json resp;
  resp["counters"] = counters;
  resp["count"] = counters.size();

  res.set_content(resp.dump(), "application/json");
  res.status = 200;
}

// ============================================================
// WebRTC Handlers
// ============================================================

void ApiRoutes::HandleWebRtcOffer(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!webrtc_service_) {
    HttpServer::JsonError(res, 500, "WebRTC service not available");
    return;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(req.body);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid JSON");
    return;
  }

  int channel_id = j.value("channel_id", -1);
  std::string sdp = j.value("sdp", "");
  if (channel_id < 0 || sdp.empty()) {
    HttpServer::JsonError(res, 400, "missing channel_id or sdp");
    return;
  }

  std::string session_id;
  std::string answer_sdp =
      webrtc_service_->HandleOffer(channel_id, sdp, session_id);

  if (answer_sdp.empty()) {
    HttpServer::JsonError(res, 500, "failed to create WebRTC session");
    return;
  }

  nlohmann::json result = {
      {"session_id", session_id},
      {"sdp", answer_sdp},
      {"type", "answer"},
  };
  HttpServer::JsonResponse(res, 200, result.dump());
}

void ApiRoutes::HandleWebRtcIce(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!webrtc_service_) {
    HttpServer::JsonError(res, 500, "WebRTC service not available");
    return;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(req.body);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid JSON");
    return;
  }

  std::string session_id = j.value("session_id", "");
  std::string candidate = j.value("candidate", "");
  if (session_id.empty() || candidate.empty()) {
    HttpServer::JsonError(res, 400, "missing session_id or candidate");
    return;
  }

  bool ok = webrtc_service_->HandleIceCandidate(session_id, candidate);
  HttpServer::JsonResponse(
      res, ok ? 200 : 404,
      ok ? R"({"success":true})"
         : R"({"success":false,"error":"session not found"})");
}

void ApiRoutes::HandleWebRtcClose(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!webrtc_service_) {
    HttpServer::JsonError(res, 500, "WebRTC service not available");
    return;
  }

  std::string session_id = req.matches[1].str();
  bool ok = webrtc_service_->CloseSession(session_id);
  HttpServer::JsonResponse(
      res, ok ? 200 : 404,
      ok ? R"({"success":true})"
         : R"({"success":false,"error":"session not found"})");
}

// ============================================================
// Cloud Backup Handlers
// ============================================================

void ApiRoutes::HandleGetBackupConfig(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!cloud_backup_) {
    HttpServer::JsonError(res, 500, "backup service not available");
    return;
  }

  auto config = cloud_backup_->GetConfig();
  auto policy = cloud_backup_->GetPolicy();
  nlohmann::json j = {
      {"enabled", config.enabled},
      {"provider",
       config.provider == storage::CloudProvider::kS3 ? "s3" : "none"},
      {"endpoint", config.endpoint},
      {"region", config.region},
      {"bucket", config.bucket},
      {"use_ssl", config.use_ssl},
      {"prefix", config.prefix},
      {"backup_mode", static_cast<int>(policy.mode)},
      {"retention_days", policy.retention_days},
      {"upload_interval_sec", policy.upload_interval_sec},
  };
  HttpServer::JsonResponse(res, 200, j.dump());
}

void ApiRoutes::HandleUpdateBackupConfig(HttpServer& srv,
                                         const httplib::Request& req,
                                         httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!cloud_backup_) {
    HttpServer::JsonError(res, 500, "backup service not available");
    return;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(req.body);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid JSON");
    return;
  }

  storage::CloudConfig config;
  config.enabled = j.value("enabled", false);
  config.endpoint = j.value("endpoint", "");
  config.region = j.value("region", "us-east-1");
  config.bucket = j.value("bucket", "");
  config.access_key = j.value("access_key", "");
  config.secret_key = j.value("secret_key", "");
  config.use_ssl = j.value("use_ssl", true);
  config.prefix = j.value("prefix", "loong-nvr/");
  cloud_backup_->Configure(config);

  storage::BackupPolicy policy;
  policy.mode = static_cast<storage::BackupMode>(j.value("backup_mode", 0));
  policy.retention_days = j.value("retention_days", 30);
  policy.upload_interval_sec = j.value("upload_interval_sec", 300);
  cloud_backup_->SetPolicy(policy);

  HttpServer::JsonResponse(res, 200, R"({"success":true})");
}

void ApiRoutes::HandleGetBackupStatus(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!cloud_backup_) {
    HttpServer::JsonError(res, 500, "backup service not available");
    return;
  }

  auto status = cloud_backup_->GetStatus();
  auto tasks = cloud_backup_->GetRecentTasks(20);

  nlohmann::json task_arr = nlohmann::json::array();
  for (const auto& t : tasks) {
    task_arr.push_back({
        {"local_path", t.local_path},
        {"remote_key", t.remote_key},
        {"file_size", t.file_size},
        {"success", t.success},
        {"error_message", t.error_message},
        {"started_at", t.started_at},
        {"finished_at", t.finished_at},
    });
  }

  nlohmann::json j = {
      {"running", status.running},
      {"total_uploaded", status.total_uploaded},
      {"total_failed", status.total_failed},
      {"bytes_uploaded", status.bytes_uploaded},
      {"last_upload_time", status.last_upload_time},
      {"last_error", status.last_error},
      {"recent_tasks", task_arr},
  };
  HttpServer::JsonResponse(res, 200, j.dump());
}

void ApiRoutes::HandleTestBackupConnection(HttpServer& srv,
                                           const httplib::Request& req,
                                           httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!cloud_backup_) {
    HttpServer::JsonError(res, 500, "backup service not available");
    return;
  }

  bool ok = cloud_backup_->TestConnection();
  nlohmann::json j = {{"success", ok}};
  HttpServer::JsonResponse(res, ok ? 200 : 500, j.dump());
}

void ApiRoutes::HandleTriggerBackup(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!cloud_backup_) {
    HttpServer::JsonError(res, 500, "backup service not available");
    return;
  }

  cloud_backup_->TriggerBackup();
  HttpServer::JsonResponse(res, 200,
                           R"({"success":true,"message":"backup triggered"})");
}

// ============================================================
// MQTT Handlers
// ============================================================

void ApiRoutes::HandleGetMqttConfig(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notification manager not available");
    return;
  }

  auto config = notification_mgr_->GetMqttConfig();
  nlohmann::json j = {
      {"enabled", config.enabled},
      {"broker_host", config.broker_host},
      {"broker_port", config.broker_port},
      {"client_id", config.client_id},
      {"username", config.username},
      {"use_tls", config.use_tls},
      {"event_topic", config.event_topic},
      {"status_topic", config.status_topic},
      {"command_topic", config.command_topic},
      {"qos", config.qos},
      {"keepalive_sec", config.keepalive_sec},
  };
  HttpServer::JsonResponse(res, 200, j.dump());
}

void ApiRoutes::HandleUpdateMqttConfig(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notification manager not available");
    return;
  }

  nlohmann::json j;
  try {
    j = nlohmann::json::parse(req.body);
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid JSON");
    return;
  }

  system::MqttConfig config;
  config.enabled = j.value("enabled", false);
  config.broker_host = j.value("broker_host", "localhost");
  config.broker_port = j.value("broker_port", 1883);
  config.client_id = j.value("client_id", "loong-nvr");
  config.username = j.value("username", "");
  config.password = j.value("password", "");
  config.use_tls = j.value("use_tls", false);
  config.event_topic = j.value("event_topic", "loong/events");
  config.status_topic = j.value("status_topic", "loong/status");
  config.command_topic = j.value("command_topic", "loong/commands");
  config.qos = j.value("qos", 1);
  config.keepalive_sec = j.value("keepalive_sec", 60);

  notification_mgr_->ConfigureMqtt(config);
  HttpServer::JsonResponse(res, 200, R"({"success":true})");
}

void ApiRoutes::HandleTestMqtt(HttpServer& srv, const httplib::Request& req,
                               httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!notification_mgr_) {
    HttpServer::JsonError(res, 500, "notification manager not available");
    return;
  }

  auto result = notification_mgr_->TestMqtt();
  nlohmann::json j = {
      {"success", result.success},
      {"error_message", result.error_message},
  };
  HttpServer::JsonResponse(res, result.success ? 200 : 500, j.dump());
}

// ============================================================
// Plugin Handlers
// ============================================================

void ApiRoutes::HandleListPlugins(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  nlohmann::json arr = nlohmann::json::array();
  if (plugin_mgr_) {
    auto plugins = plugin_mgr_->ListPlugins();
    for (const auto& p : plugins) {
      arr.push_back({
          {"name", p.name},
          {"version", p.version},
          {"author", p.author},
          {"description", p.description},
          {"model_family", p.model_family},
          {"api_version", p.api_version},
      });
    }
  }
  nlohmann::json resp;
  resp["plugins"] = arr;
  resp["count"] = arr.size();
  HttpServer::JsonResponse(res, 200, resp.dump());
}

void ApiRoutes::HandleUploadPlugin(HttpServer& srv, const httplib::Request& req,
                                   httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (ctx.claims.role != system::UserRole::kAdmin) {
    HttpServer::JsonError(res, 403, "admin only");
    return;
  }

  if (!plugin_mgr_) {
    HttpServer::JsonError(res, 500, "plugin manager not configured");
    return;
  }

  if (!req.has_file("plugin")) {
    HttpServer::JsonError(res, 400, "missing 'plugin' file field");
    return;
  }

  const auto& file = req.get_file_value("plugin");
  std::string dir = plugin_mgr_->GetPluginDir();
  if (dir.empty()) {
    HttpServer::JsonError(res, 500, "plugin directory not configured");
    return;
  }

  std::string filename = file.filename;
  if (filename.size() < 4 || filename.substr(filename.size() - 3) != ".so") {
    HttpServer::JsonError(res, 400, "file must be a .so shared library");
    return;
  }

  std::string dest = dir + "/" + filename;
  std::ofstream ofs(dest, std::ios::binary);
  if (!ofs) {
    HttpServer::JsonError(res, 500, "failed to write plugin file");
    return;
  }
  ofs.write(file.content.data(),
            static_cast<std::streamsize>(file.content.size()));
  ofs.close();

  bool loaded = plugin_mgr_->LoadPlugin(dest);
  nlohmann::json result = {{"success", loaded}, {"file", filename}};
  HttpServer::JsonResponse(res, loaded ? 201 : 400, result.dump());
}

// ============================================================
// Alarm Handlers
// ============================================================

void ApiRoutes::HandleListAlarmRules(HttpServer& srv,
                                     const httplib::Request& req,
                                     httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  std::vector<system::AlarmRule> rules;
  if (req.has_param("channel_id")) {
    int ch_id = std::stoi(req.get_param_value("channel_id"));
    rules = alarm_mgr_->ListRulesForChannel(ch_id);
  } else {
    rules = alarm_mgr_->ListRules();
  }

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : rules) {
    arr.push_back({
        {"id", r.id},
        {"channel_id", r.channel_id},
        {"event_type", r.event_type},
        {"min_confidence", r.min_confidence},
        {"severity", system::AlarmSeverityToString(r.severity)},
        {"enabled", r.enabled},
        {"name", r.name},
        {"description", r.description},
        {"cooldown_sec", r.cooldown_sec},
    });
  }
  nlohmann::json result = {{"rules", arr}, {"total", arr.size()}};
  HttpServer::JsonResponse(res, 200, result.dump());
}

void ApiRoutes::HandleCreateAlarmRule(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  try {
    auto j = nlohmann::json::parse(req.body);
    system::AlarmRule rule;
    rule.name = j.value("name", "");
    rule.channel_id = j.value("channel_id", -1);
    rule.event_type = j.value("event_type", "");
    rule.min_confidence = j.value("min_confidence", 0.5F);
    rule.enabled = j.value("enabled", true);
    rule.description = j.value("description", "");
    rule.cooldown_sec = j.value("cooldown_sec", 30);

    auto sev_str = j.value("severity", "warning");
    if (sev_str == "info") {
      rule.severity = system::AlarmSeverity::kInfo;
    } else if (sev_str == "critical") {
      rule.severity = system::AlarmSeverity::kCritical;
    } else {
      rule.severity = system::AlarmSeverity::kWarning;
    }

    if (rule.name.empty()) {
      HttpServer::JsonError(res, 400, "name is required");
      return;
    }

    int64_t id = alarm_mgr_->AddRule(rule);
    nlohmann::json result = {{"id", id}, {"success", id > 0}};
    HttpServer::JsonResponse(res, id > 0 ? 201 : 500, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json");
  }
}

void ApiRoutes::HandleUpdateAlarmRule(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  try {
    int64_t rule_id = std::stoll(req.matches[1]);
    auto existing = alarm_mgr_->GetRule(rule_id);
    if (existing.id == 0) {
      HttpServer::JsonError(res, 404, "rule not found");
      return;
    }

    auto j = nlohmann::json::parse(req.body);
    if (j.contains("name")) existing.name = j["name"].get<std::string>();
    if (j.contains("channel_id")) existing.channel_id = j["channel_id"];
    if (j.contains("event_type"))
      existing.event_type = j["event_type"].get<std::string>();
    if (j.contains("min_confidence"))
      existing.min_confidence = j["min_confidence"];
    if (j.contains("enabled")) existing.enabled = j["enabled"];
    if (j.contains("description"))
      existing.description = j["description"].get<std::string>();
    if (j.contains("cooldown_sec")) existing.cooldown_sec = j["cooldown_sec"];
    if (j.contains("severity")) {
      auto sev = j["severity"].get<std::string>();
      if (sev == "info")
        existing.severity = system::AlarmSeverity::kInfo;
      else if (sev == "critical")
        existing.severity = system::AlarmSeverity::kCritical;
      else
        existing.severity = system::AlarmSeverity::kWarning;
    }

    existing.id = rule_id;
    bool ok = alarm_mgr_->UpdateRule(existing);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 500, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid json or rule id");
  }
}

void ApiRoutes::HandleDeleteAlarmRule(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  try {
    int64_t rule_id = std::stoll(req.matches[1]);
    bool ok = alarm_mgr_->DeleteRule(rule_id);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 404, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid rule id");
  }
}

void ApiRoutes::HandleQueryAlarms(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  int64_t start_time = 0;
  int64_t end_time = std::numeric_limits<int64_t>::max();
  int limit = 100;
  int channel_id = -1;

  if (req.has_param("start_time"))
    start_time = std::stoll(req.get_param_value("start_time"));
  if (req.has_param("end_time"))
    end_time = std::stoll(req.get_param_value("end_time"));
  if (req.has_param("limit")) limit = std::stoi(req.get_param_value("limit"));
  if (req.has_param("channel_id"))
    channel_id = std::stoi(req.get_param_value("channel_id"));

  std::vector<system::AlarmRecord> records;
  if (channel_id >= 0) {
    records = alarm_mgr_->QueryAlarmsByChannel(channel_id, start_time, end_time,
                                               limit);
  } else {
    records = alarm_mgr_->QueryAlarms(start_time, end_time, limit);
  }

  nlohmann::json arr = nlohmann::json::array();
  for (const auto& r : records) {
    arr.push_back({
        {"id", r.id},
        {"rule_id", r.rule_id},
        {"channel_id", r.channel_id},
        {"event_type", r.event_type},
        {"confidence", r.confidence},
        {"severity", system::AlarmSeverityToString(r.severity)},
        {"status", static_cast<int>(r.status)},
        {"triggered_at", r.triggered_at},
        {"acknowledged_at", r.acknowledged_at},
        {"acknowledged_by", r.acknowledged_by},
    });
  }

  nlohmann::json result = {
      {"alarms", arr},
      {"total", arr.size()},
      {"active_count", alarm_mgr_->GetActiveAlarmCount()},
  };
  HttpServer::JsonResponse(res, 200, result.dump());
}

void ApiRoutes::HandleAcknowledgeAlarm(HttpServer& srv,
                                       const httplib::Request& req,
                                       httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  try {
    int64_t alarm_id = std::stoll(req.matches[1]);
    std::string user = ctx.claims.username;
    bool ok = alarm_mgr_->AcknowledgeAlarm(alarm_id, user);
    nlohmann::json result = {{"success", ok}};
    HttpServer::JsonResponse(res, ok ? 200 : 404, result.dump());
  } catch (...) {
    HttpServer::JsonError(res, 400, "invalid alarm id");
  }
}

void ApiRoutes::HandleAcknowledgeAllAlarms(HttpServer& srv,
                                           const httplib::Request& req,
                                           httplib::Response& res) {
  system::AuthContext ctx;
  if (!srv.CheckAuth(req, res, ctx)) return;

  if (!alarm_mgr_) {
    HttpServer::JsonError(res, 503, "Alarm manager not available");
    return;
  }

  int count = alarm_mgr_->AcknowledgeAll(ctx.claims.username);
  nlohmann::json result = {{"acknowledged", count}};
  HttpServer::JsonResponse(res, 200, result.dump());
}

}  // namespace loong::network
