// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_HTTP_API_API_ROUTES_H_
#define LOONG_NETWORK_HTTP_API_API_ROUTES_H_

#include <memory>

// Suppress warnings from third-party header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <httplib.h>
#pragma GCC diagnostic pop

#include "channel/channel_manager/channel_manager.h"
#include "storage/record_index/record_index.h"
#include "system/auth/jwt_helper.h"
#include "system/auth/user_store.h"
#include "system/monitor/system_monitor.h"
#include "core/config/hot_reload_manager.h"
#include "system/notification/notification_manager.h"
#include "ai_engine/lpr/plate_store.h"
#include "ai_engine/face/face_store.h"
#include "analytics/analytics_store.h"
#include "ai_engine/plugin/plugin_manager.h"
#include "storage/cloud_backup/cloud_backup_service.h"
#include "network/webrtc/webrtc_service.h"
#include "rules/rule_engine/rule_engine.h"
#include "system/alarm/alarm_manager.h"
#include "video_input/onvif/onvif_discovery.h"
#include "video_input/onvif/onvif_device.h"
#include "video_input/onvif/onvif_ptz.h"

namespace loong::network {

class HttpServer;
class HttpFlvService;
class HlsService;

/// REST API route definitions and business-logic handlers.
///
/// This class is purely about *what to do* when a request arrives;
/// it contains no HTTP server lifecycle or infrastructure code.
/// Call Register() to bind all routes onto an HttpServer.
///
/// Endpoints registered:
///
///   Auth:
///     POST /api/auth/login        — Login (returns JWT)
///     GET  /api/auth/profile      — Current user info
///     PUT  /api/auth/password     — Change own password
///
///   Users (admin):
///     GET    /api/users           — List users
///     POST   /api/users           — Create user
///     PUT    /api/users/:id       — Update user
///     DELETE /api/users/:id       — Delete user
///
///   Channels:
///     GET    /api/channels            — List channels
///     POST   /api/channels            — Create channel
///     GET    /api/channels/:id        — Get channel
///     PUT    /api/channels/:id        — Update channel
///     DELETE /api/channels/:id        — Delete channel
///     POST   /api/channels/:id/start  — Start channel
///     POST   /api/channels/:id/stop   — Stop channel
///
///   Other:
///     GET  /api/recordings        — Query recordings
///     GET  /api/events            — Query events
///     GET  /api/system/status     — System status
///
///   Overlay:
///     GET  /api/channels/:id/overlay — Get overlay config
///     PUT  /api/channels/:id/overlay — Update overlay config
///
///   Streaming:
///     GET  /live/ch:id.flv            — HTTP-FLV live stream
///     GET  /live/ch:id/index.m3u8     — HLS playlist
///     GET  /live/ch:id/:seq.ts        — HLS segment
class ApiRoutes {
 public:
  ApiRoutes() = default;
  ~ApiRoutes() = default;

  /// Inject business dependencies.
  void SetChannelManager(std::shared_ptr<channel::ChannelManager> mgr) {
    channel_mgr_ = std::move(mgr);
  }
  void SetRecordIndex(std::shared_ptr<storage::RecordIndex> index) {
    record_index_ = std::move(index);
  }
  void SetUserStore(std::shared_ptr<system::UserStore> store) {
    user_store_ = std::move(store);
  }
  void SetJwtHelper(std::shared_ptr<system::JwtHelper> jwt) {
    jwt_ = std::move(jwt);
  }
  void SetFlvService(std::shared_ptr<HttpFlvService> flv) {
    flv_service_ = std::move(flv);
  }
  void SetHlsService(std::shared_ptr<HlsService> hls) {
    hls_service_ = std::move(hls);
  }
  void SetSystemMonitor(std::shared_ptr<system::SystemMonitor> monitor) {
    system_monitor_ = std::move(monitor);
  }
  void SetNotificationManager(
      std::shared_ptr<system::NotificationManager> notif) {
    notification_mgr_ = std::move(notif);
  }
  void SetHotReloadManager(
      std::shared_ptr<core::HotReloadManager> hot_reload) {
    hot_reload_mgr_ = std::move(hot_reload);
  }
  void SetRuleEngine(std::shared_ptr<rules::RuleEngine> engine) {
    rule_engine_ = std::move(engine);
  }
  void SetPlateStore(std::shared_ptr<ai_engine::PlateStore> store) {
    plate_store_ = std::move(store);
  }
  void SetFaceStore(std::shared_ptr<ai_engine::FaceStore> store) {
    face_store_ = std::move(store);
  }
  void SetAnalyticsStore(std::shared_ptr<analytics::AnalyticsStore> store) {
    analytics_store_ = std::move(store);
  }
  void SetPluginManager(std::shared_ptr<ai_engine::PluginManager> mgr) {
    plugin_mgr_ = std::move(mgr);
  }
  void SetCloudBackup(std::shared_ptr<storage::CloudBackupService> backup) {
    cloud_backup_ = std::move(backup);
  }
  void SetWebRtcService(std::shared_ptr<WebRtcService> webrtc) {
    webrtc_service_ = std::move(webrtc);
  }
  void SetAlarmManager(std::shared_ptr<system::AlarmManager> mgr) {
    alarm_mgr_ = std::move(mgr);
  }

  /// Register all routes on the given HTTP server.
  void Register(HttpServer& server);

  // Non-copyable
  ApiRoutes(const ApiRoutes&) = delete;
  ApiRoutes& operator=(const ApiRoutes&) = delete;

 private:
  // ---- Auth handlers ----
  void HandleLogin(HttpServer& srv, const httplib::Request& req,
                   httplib::Response& res);
  void HandleGetProfile(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleChangePassword(HttpServer& srv, const httplib::Request& req,
                            httplib::Response& res);

  // ---- User management handlers ----
  void HandleListUsers(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleCreateUser(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleUpdateUser(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleDeleteUser(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);

  // ---- Channel handlers ----
  void HandleListChannels(HttpServer& srv, const httplib::Request& req,
                          httplib::Response& res);
  void HandleCreateChannel(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleGetChannel(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleUpdateChannel(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleDeleteChannel(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleStartChannel(HttpServer& srv, const httplib::Request& req,
                          httplib::Response& res);
  void HandleStopChannel(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);

  // ---- Overlay config handlers ----
  void HandleGetOverlayConfig(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleUpdateOverlayConfig(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res);

  // ---- Other handlers ----
  void HandleGetRecordings(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleGetEvents(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleGetTimeline(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleSearchEvents(HttpServer& srv, const httplib::Request& req,
                          httplib::Response& res);
  void HandleGetSystemStatus(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);

  // ---- System handlers ----
  void HandleUpdateStorageConfig(HttpServer& srv, const httplib::Request& req,
                                 httplib::Response& res);
  void HandleGetConfig(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleUpdateConfig(HttpServer& srv, const httplib::Request& req,
                          httplib::Response& res);

  // ---- Recording file handlers ----
  void HandleRecordingPlayback(HttpServer& srv, const httplib::Request& req,
                               httplib::Response& res);
  void HandleRecordingDownload(HttpServer& srv, const httplib::Request& req,
                               httplib::Response& res);

  // ---- Streaming handler ----
  void HandleFlvStream(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleHlsPlaylist(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleHlsSegment(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);

  // ---- ONVIF handlers ----
  void HandleOnvifDiscover(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleOnvifDeviceInfo(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleOnvifPtz(HttpServer& srv, const httplib::Request& req,
                      httplib::Response& res);

  // ---- Notification handlers ----
  void HandleGetNotificationConfig(HttpServer& srv,
                                   const httplib::Request& req,
                                   httplib::Response& res);
  void HandleUpdateNotificationConfig(HttpServer& srv,
                                      const httplib::Request& req,
                                      httplib::Response& res);
  void HandleTestNotification(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleGetNotificationHistory(HttpServer& srv,
                                    const httplib::Request& req,
                                    httplib::Response& res);

  // ---- Rules handlers ----
  void HandleListRules(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleCreateRule(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleGetRule(HttpServer& srv, const httplib::Request& req,
                     httplib::Response& res);
  void HandleUpdateRule(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleDeleteRule(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleQueryRuleEvents(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);

  // ---- LPR (Plate) handlers ----
  void HandleQueryPlates(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);

  // ---- Face handlers ----
  void HandleQueryFaces(HttpServer& srv, const httplib::Request& req,
                        httplib::Response& res);
  void HandleSearchFaces(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);

  // ---- Analytics handlers ----
  void HandleAnalyticsSummary(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleAnalyticsTrends(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleAnalyticsHeatmap(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleAnalyticsPeakHours(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res);
  void HandleAnalyticsCounting(HttpServer& srv, const httplib::Request& req,
                               httplib::Response& res);

  // ---- WebRTC handlers ----
  void HandleWebRtcOffer(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleWebRtcIce(HttpServer& srv, const httplib::Request& req,
                       httplib::Response& res);
  void HandleWebRtcClose(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);

  // ---- Cloud Backup handlers ----
  void HandleGetBackupConfig(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleUpdateBackupConfig(HttpServer& srv, const httplib::Request& req,
                                httplib::Response& res);
  void HandleGetBackupStatus(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleTestBackupConnection(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res);
  void HandleTriggerBackup(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);

  // ---- MQTT handlers ----
  void HandleGetMqttConfig(HttpServer& srv, const httplib::Request& req,
                           httplib::Response& res);
  void HandleUpdateMqttConfig(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleTestMqtt(HttpServer& srv, const httplib::Request& req,
                      httplib::Response& res);

  // ---- Plugin handlers ----
  void HandleListPlugins(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleUploadPlugin(HttpServer& srv, const httplib::Request& req,
                          httplib::Response& res);

  // ---- Alarm handlers ----
  void HandleListAlarmRules(HttpServer& srv, const httplib::Request& req,
                            httplib::Response& res);
  void HandleCreateAlarmRule(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleUpdateAlarmRule(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleDeleteAlarmRule(HttpServer& srv, const httplib::Request& req,
                             httplib::Response& res);
  void HandleQueryAlarms(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleAcknowledgeAlarm(HttpServer& srv, const httplib::Request& req,
                              httplib::Response& res);
  void HandleAcknowledgeAllAlarms(HttpServer& srv, const httplib::Request& req,
                                  httplib::Response& res);

  // ---- Observability handlers ----
  void HandleHealthCheck(HttpServer& srv, const httplib::Request& req,
                         httplib::Response& res);
  void HandleMetrics(HttpServer& srv, const httplib::Request& req,
                     httplib::Response& res);

  // Business dependencies (no server/infra deps here)
  std::shared_ptr<channel::ChannelManager> channel_mgr_;
  std::shared_ptr<storage::RecordIndex> record_index_;
  std::shared_ptr<system::UserStore> user_store_;
  std::shared_ptr<system::JwtHelper> jwt_;
  std::shared_ptr<HttpFlvService> flv_service_;
  std::shared_ptr<HlsService> hls_service_;
  std::shared_ptr<system::SystemMonitor> system_monitor_;
  std::shared_ptr<system::NotificationManager> notification_mgr_;
  std::shared_ptr<core::HotReloadManager> hot_reload_mgr_;
  std::shared_ptr<rules::RuleEngine> rule_engine_;
  std::shared_ptr<ai_engine::PlateStore> plate_store_;
  std::shared_ptr<ai_engine::FaceStore> face_store_;
  std::shared_ptr<analytics::AnalyticsStore> analytics_store_;
  std::shared_ptr<ai_engine::PluginManager> plugin_mgr_;
  std::shared_ptr<storage::CloudBackupService> cloud_backup_;
  std::shared_ptr<WebRtcService> webrtc_service_;
  std::shared_ptr<system::AlarmManager> alarm_mgr_;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_HTTP_API_API_ROUTES_H_
