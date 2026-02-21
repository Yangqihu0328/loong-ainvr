// Copyright 2026 Loong AI NVR Project

#include "ai_engine/face/face_store.h"
#include "ai_engine/lpr/plate_store.h"
#include "ai_engine/model_manager/model_manager.h"
#include "ai_engine/plugin/plugin_manager.h"
#include "analytics/analytics_aggregator.h"
#include "analytics/analytics_store.h"
#include "channel/channel_manager/channel_manager.h"
#include "channel/channel_pipeline/channel_orchestrator.h"
#include "channel/channel_store/channel_store.h"
#include "core/config/config_manager.h"
#include "core/config/hot_reload_manager.h"
#include "core/event_bus/event_bus.h"
#include "core/logger/logger.h"
#include "core/thread_pool/thread_pool.h"
#include "network/flv_stream/http_flv_service.h"
#include "network/hls_stream/hls_service.h"
#include "network/http_api/api_routes.h"
#include "network/http_server/http_server.h"
#include "network/media_stream/ws_media_service.h"
#include "network/rtsp_server/rtsp_server.h"
#include "network/webrtc/webrtc_service.h"
#include "network/websocket/websocket_server.h"
#include "rules/rule_engine/rule_engine.h"
#include "rules/rule_store/rule_store.h"
#include "spdlog/spdlog.h"
#include "storage/cloud_backup/cloud_backup_service.h"
#include "storage/record_index/record_index.h"
#include "storage/storage_cleaner/storage_cleaner.h"
#include "system/alarm/alarm_manager.h"
#include "system/auth/auth_middleware.h"
#include "system/auth/jwt_helper.h"
#include "system/auth/user_store.h"
#include "system/monitor/system_monitor.h"
#include "system/notification/notification_manager.h"

#include <signal.h>  // POSIX sigaction

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace {

constexpr const char* kVersion = "0.3.0";
constexpr int kDefaultHttpPort = 8080;
constexpr int kMaxChannels = 64;

std::atomic<bool> g_running{true};

void SignalHandler(int /*sig*/) {
  g_running.store(false, std::memory_order_relaxed);
}

void PrintBanner() {
  spdlog::info("====================================");
  spdlog::info("  Loong AI NVR v{}", kVersion);
  spdlog::info("  Embedded Video Analysis System");
  spdlog::info("  Max Channels: {}", kMaxChannels);
  spdlog::info("====================================");
}

}  // namespace

int main(int argc, char* argv[]) {
  // Ignore SIGPIPE — writing to disconnected sockets must not crash the
  // process.
  signal(SIGPIPE, SIG_IGN);

  // Register signal handlers via sigaction for reliable behavior.
  struct sigaction sa {};
  sa.sa_handler = SignalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);

  // ========================================
  // Step 1: Initialize Core Framework
  // ========================================
  // Pre-parse config to check for JSON log format setting
  auto& config = loong::core::ConfigManager::Instance();
  if (argc > 1) {
    if (!config.LoadFromFile(argv[1])) {
      std::cerr << "Failed to load config: " << argv[1] << std::endl;
      return EXIT_FAILURE;
    }
  }

  bool json_log = config.Get<bool>("logging.json_format", false);
  loong::core::InitLogger("loong-ainvr", spdlog::level::info, json_log);
  PrintBanner();

  if (argc <= 1) {
    spdlog::info("No config file specified, using defaults");
  }

  loong::core::EventBus::Instance();
  spdlog::info("EventBus initialized");

  auto& thread_pool = loong::core::ThreadPool::Instance();
  spdlog::info("ThreadPool initialized ({} workers)",
               thread_pool.WorkerCount());

  // ========================================
  // Step 2: Initialize Storage Engine
  // ========================================
  auto record_index = std::make_shared<loong::storage::RecordIndex>();
  auto db_path =
      config.Get<std::string>("storage.index_db", "/recordings/index.db");
  if (!record_index->Open(db_path)) {
    spdlog::warn("RecordIndex: failed to open '{}', recording index disabled",
                 db_path);
  } else {
    spdlog::info("RecordIndex: opened '{}'", db_path);
  }

  auto storage_cleaner =
      std::make_shared<loong::storage::StorageCleaner>(record_index);
  storage_cleaner->Start();
  spdlog::info("StorageCleaner: started");

  // ========================================
  // Step 3: Initialize User Auth System
  // ========================================
  auto user_store = std::make_shared<loong::system::UserStore>();
  auto user_db_path = config.Get<std::string>("auth.user_db", "users.db");
  if (!user_store->Open(user_db_path)) {
    spdlog::warn("UserStore: failed to open '{}', auth disabled", user_db_path);
  } else {
    spdlog::info("UserStore: opened '{}'", user_db_path);
  }

  // JWT secret: env var > config file > default (with warning)
  std::string jwt_secret;
  const char* env_secret = std::getenv("LOONG_JWT_SECRET");
  if (env_secret != nullptr && env_secret[0] != '\0') {
    jwt_secret = env_secret;
    spdlog::info("JwtHelper: secret loaded from LOONG_JWT_SECRET env var");
  } else {
    jwt_secret = config.Get<std::string>(
        "auth.jwt_secret", "loong-ainvr-default-secret-change-me");
    if (jwt_secret == "loong-ainvr-default-secret-change-me") {
      spdlog::warn(
          "JwtHelper: using DEFAULT secret — set LOONG_JWT_SECRET "
          "env var or auth.jwt_secret in config for production!");
    }
  }
  auto jwt_expiry = config.Get<int64_t>("auth.jwt_expiry_seconds", 86400);
  auto jwt_helper =
      std::make_shared<loong::system::JwtHelper>(jwt_secret, jwt_expiry);
  spdlog::info("JwtHelper: configured (expiry={}s)", jwt_expiry);

  auto auth_middleware =
      std::make_shared<loong::system::AuthMiddleware>(jwt_helper, user_store);
  spdlog::info("AuthMiddleware: initialized");

  // ========================================
  // Step 4: Initialize System Monitor
  // ========================================
  auto system_monitor = std::make_shared<loong::system::SystemMonitor>();
  auto rec_path = config.Get<std::string>("storage.base_path", "/recordings");
  system_monitor->AddDiskPath(rec_path);
  int monitor_interval = config.Get<int>("monitor.interval_seconds", 5);
  system_monitor->Start(monitor_interval);
  spdlog::info("SystemMonitor: started");

  // ========================================
  // Step 4b: Initialize Notification Manager
  // ========================================
  auto notification_mgr =
      std::make_shared<loong::system::NotificationManager>();

  // Load notification config if present
  loong::system::SmtpConfig smtp_config;
  smtp_config.enabled = config.Get<bool>("notifications.smtp.enabled", false);
  smtp_config.host = config.Get<std::string>("notifications.smtp.host", "");
  smtp_config.port = config.Get<int>("notifications.smtp.port", 587);
  smtp_config.use_tls = config.Get<bool>("notifications.smtp.use_tls", true);
  smtp_config.username =
      config.Get<std::string>("notifications.smtp.username", "");
  smtp_config.password =
      config.Get<std::string>("notifications.smtp.password", "");
  smtp_config.from_address =
      config.Get<std::string>("notifications.smtp.from_address", "");
  notification_mgr->ConfigureSmtp(smtp_config);

  loong::system::WebhookConfig webhook_config;
  webhook_config.enabled =
      config.Get<bool>("notifications.webhook.enabled", false);
  webhook_config.url = config.Get<std::string>("notifications.webhook.url", "");
  webhook_config.secret =
      config.Get<std::string>("notifications.webhook.secret", "");
  notification_mgr->ConfigureWebhook(webhook_config);

  // MQTT notification channel
  loong::system::MqttConfig mqtt_config;
  mqtt_config.enabled = config.Get<bool>("mqtt.enabled", false);
  mqtt_config.broker_host = config.Get<std::string>("mqtt.broker", "");
  mqtt_config.broker_port = config.Get<int>("mqtt.port", 1883);
  mqtt_config.use_tls = config.Get<bool>("mqtt.use_tls", false);
  mqtt_config.username = config.Get<std::string>("mqtt.username", "");
  mqtt_config.password = config.Get<std::string>("mqtt.password", "");
  mqtt_config.event_topic =
      config.Get<std::string>("mqtt.event_topic", "loong/events");
  mqtt_config.command_topic =
      config.Get<std::string>("mqtt.command_topic", "loong/commands");
  notification_mgr->ConfigureMqtt(mqtt_config);

  notification_mgr->Start();
  spdlog::info(
      "NotificationManager: initialized (smtp={}, webhook={}, mqtt={})",
      smtp_config.enabled, webhook_config.enabled, mqtt_config.enabled);

  // ========================================
  // Step 4c: Initialize Rule Engine
  // ========================================
  auto rule_store = std::make_shared<loong::rules::RuleStore>();
  auto rules_db_path =
      config.Get<std::string>("rules.db_path", "/recordings/rules.db");
  bool rules_enabled = config.Get<bool>("rules.enabled", true);

  if (rules_enabled && rule_store->Open(rules_db_path)) {
    spdlog::info("RuleStore: opened '{}'", rules_db_path);
  } else if (rules_enabled) {
    spdlog::warn("RuleStore: failed to open '{}', rules disabled",
                 rules_db_path);
    rules_enabled = false;
  }

  auto rule_engine = std::make_shared<loong::rules::RuleEngine>();
  if (rules_enabled) {
    rule_engine->SetRuleStore(rule_store);
    rule_engine->LoadRules();
    spdlog::info("RuleEngine: initialized");
  }

  // ========================================
  // Step 4d: Initialize Alarm Manager
  // ========================================
  auto alarm_mgr = std::make_shared<loong::system::AlarmManager>();
  auto alarm_db_path =
      config.Get<std::string>("alarm.db_path", "/recordings/alarms.db");
  if (alarm_mgr->Open(alarm_db_path)) {
    alarm_mgr->Start();
    spdlog::info("AlarmManager: opened '{}' and started", alarm_db_path);
  } else {
    spdlog::warn("AlarmManager: failed to open '{}', alarm storage disabled",
                 alarm_db_path);
  }

  // ========================================
  // Step 4e: Initialize Plate Store (LPR)
  // ========================================
  auto plate_store = std::make_shared<loong::ai_engine::PlateStore>();
  auto plates_db_path =
      config.Get<std::string>("lpr.db_path", "/recordings/plates.db");
  if (plate_store->Open(plates_db_path)) {
    spdlog::info("PlateStore: opened '{}'", plates_db_path);
  } else {
    spdlog::warn("PlateStore: failed to open '{}', LPR storage disabled",
                 plates_db_path);
  }

  // ========================================
  // Step 4f: Initialize Face Store
  // ========================================
  auto face_store = std::make_shared<loong::ai_engine::FaceStore>();
  auto faces_db_path =
      config.Get<std::string>("face.db_path", "/recordings/faces.db");
  if (face_store->Open(faces_db_path)) {
    spdlog::info("FaceStore: opened '{}'", faces_db_path);
  } else {
    spdlog::warn("FaceStore: failed to open '{}', face storage disabled",
                 faces_db_path);
  }

  // ========================================
  // Step 4g: Initialize Analytics Aggregator
  // ========================================
  auto analytics_store = std::make_shared<loong::analytics::AnalyticsStore>();
  auto analytics_db_path =
      config.Get<std::string>("analytics.db_path", "/recordings/analytics.db");
  if (analytics_store->Open(analytics_db_path)) {
    spdlog::info("AnalyticsStore: opened '{}'", analytics_db_path);
  } else {
    spdlog::warn("AnalyticsStore: failed to open '{}'", analytics_db_path);
  }

  auto analytics_aggregator =
      std::make_shared<loong::analytics::AnalyticsAggregator>();
  analytics_aggregator->SetAnalyticsStore(analytics_store);
  analytics_aggregator->SetRuleStore(rule_store);
  loong::analytics::AggregatorConfig agg_config;
  agg_config.retention_days = config.Get<int>("analytics.retention_days", 90);
  analytics_aggregator->SetConfig(agg_config);
  analytics_aggregator->Start();
  spdlog::info("AnalyticsAggregator: started (retention={}d)",
               agg_config.retention_days);

  // ========================================
  // Step 4h: Initialize Plugin Manager
  // ========================================
  auto plugin_mgr = std::make_shared<loong::ai_engine::PluginManager>();
  auto plugin_dir = config.Get<std::string>("plugins.dir", "plugins/");
  plugin_mgr->SetPluginDir(plugin_dir);
  int plugins_loaded = plugin_mgr->ScanAndLoad();
  spdlog::info("PluginManager: scanned '{}', loaded {} plugins", plugin_dir,
               plugins_loaded);

  // ========================================
  // Step 4i: Initialize Cloud Backup Service
  // ========================================
  auto cloud_backup = std::make_shared<loong::storage::CloudBackupService>();
  bool backup_enabled = config.Get<bool>("backup.enabled", false);
  if (backup_enabled) {
    loong::storage::CloudConfig cloud_cfg;
    cloud_cfg.endpoint = config.Get<std::string>("backup.s3.endpoint", "");
    cloud_cfg.bucket = config.Get<std::string>("backup.s3.bucket", "");
    cloud_cfg.access_key = config.Get<std::string>("backup.s3.access_key", "");
    cloud_cfg.secret_key = config.Get<std::string>("backup.s3.secret_key", "");
    cloud_cfg.region = config.Get<std::string>("backup.s3.region", "");
    cloud_backup->Configure(cloud_cfg);

    loong::storage::BackupPolicy policy;
    auto policy_str = config.Get<std::string>("backup.policy", "event_only");
    if (policy_str == "full") {
      policy.mode = loong::storage::BackupMode::kFull;
    } else if (policy_str == "custom") {
      policy.mode = loong::storage::BackupMode::kCustom;
    } else {
      policy.mode = loong::storage::BackupMode::kEventOnly;
    }
    policy.retention_days = config.Get<int>("backup.retention_days", 30);
    cloud_backup->SetPolicy(policy);

    auto snapshots_dir = config.Get<std::string>("storage.snapshots_path",
                                                 "/recordings/snapshots");
    cloud_backup->Start(rec_path, snapshots_dir);
    spdlog::info("CloudBackupService: started (policy={})", policy_str);
  } else {
    spdlog::info("CloudBackupService: disabled");
  }

  // ========================================
  // Step 5: Initialize Streaming Services (FLV + HLS)
  // ========================================
  auto flv_service = std::make_shared<loong::network::HttpFlvService>();
  spdlog::info("HttpFlvService: initialized");

  loong::network::HlsConfig hls_config;
  hls_config.segment_duration_ms =
      config.Get<int>("hls.segment_duration_ms", 2000);
  hls_config.max_segments = config.Get<int>("hls.max_segments", 5);
  hls_config.max_segment_size_kb =
      config.Get<int>("hls.max_segment_size_kb", 4096);
  auto hls_service = std::make_shared<loong::network::HlsService>(hls_config);
  spdlog::info("HlsService: initialized (segment={}ms, window={})",
               hls_config.segment_duration_ms, hls_config.max_segments);

  // ========================================
  // Step 6: Initialize Channel Manager + Streaming Services
  // ========================================
  auto http_host = config.Get<std::string>("network.http_host", "0.0.0.0");

  int ws_media_port = config.Get<int>("network.ws_media_port", 8082);
  auto ws_media_service = std::make_shared<loong::network::WsMediaService>(
      http_host, ws_media_port);
  spdlog::info("WsMediaService: initialized");

  int rtsp_port = config.Get<int>("network.rtsp_port", 554);
  auto rtsp_server =
      std::make_shared<loong::network::RtspServer>(http_host, rtsp_port);
  spdlog::info("RtspServer: initialized");

  // Initialize WebRTC before wiring to ChannelManager.
  loong::network::WebRtcConfig webrtc_config;
  webrtc_config.stun_server = config.Get<std::string>(
      "webrtc.stun_server", "stun:stun.l.google.com:19302");
  webrtc_config.max_sessions = config.Get<int>("webrtc.max_sessions", 32);
  webrtc_config.ice_timeout_sec = config.Get<int>("webrtc.ice_timeout_sec", 10);

  auto webrtc_service = std::make_shared<loong::network::WebRtcService>();
  if (webrtc_service->Initialize(webrtc_config)) {
    webrtc_service->StartCleanupTimer();
    spdlog::info("WebRtcService: initialized (stun={}, max_sessions={})",
                 webrtc_config.stun_server, webrtc_config.max_sessions);
  } else {
    spdlog::warn("WebRtcService: initialization failed, WebRTC disabled");
  }

  auto channel_mgr = std::make_shared<loong::channel::ChannelManager>();
  channel_mgr->EnableAutoRecovery(
      config.Get<bool>("channel.auto_recovery", true));

  channel_mgr->SetStreamingServices(flv_service, hls_service, rtsp_server,
                                    ws_media_service, webrtc_service);

  if (rules_enabled) {
    channel_mgr->SetRuleEngine(rule_engine);
  }

  // Wire cascade config if enabled (e.g., YOLO → LPR-det → LPR-OCR).
  bool cascade_enabled = config.Get<bool>("ai.cascade.enabled", false);
  if (cascade_enabled) {
    auto& orch = channel_mgr->GetOrchestrator();
    orch.SetCascadeEnabled(true);
    std::vector<std::pair<std::string, std::string>> cascade_steps;
    auto steps_cfg = config.Get<std::string>("ai.cascade.steps", "");
    if (!steps_cfg.empty()) {
      try {
        auto j = nlohmann::json::parse(steps_cfg);
        for (const auto& step : j) {
          cascade_steps.emplace_back(step.value("model_name", ""),
                                     step.value("mode", "crop"));
        }
      } catch (const std::exception& e) {
        spdlog::warn("Failed to parse ai.cascade.steps: {}", e.what());
      }
    }
    orch.SetCascadeSteps(cascade_steps);
    spdlog::info("ChannelManager: cascade enabled ({} steps)",
                 cascade_steps.size());
  }

  // AI Model Manager — scans model directory and creates inference engines.
  auto model_manager = std::make_shared<loong::ai_engine::ModelManager>();
  auto models_dir = config.Get<std::string>("ai.models_dir", "./models");
  int model_count = model_manager->ScanDirectory(models_dir);
  if (model_count > 0) {
    spdlog::info("ModelManager: {} model(s) available", model_count);
  } else {
    spdlog::info(
        "ModelManager: no models found in '{}' — AI analysis "
        "disabled (place .onnx files in this directory)",
        models_dir);
  }
  channel_mgr->GetOrchestrator().SetModelManager(model_manager);

  // Persistent channel store — channels survive restarts.
  auto channel_store = std::make_shared<loong::channel::ChannelStore>();
  auto channels_db_path =
      config.Get<std::string>("channel.db_path", "./data/channels.db");
  if (channel_store->Open(channels_db_path)) {
    channel_mgr->SetChannelStore(channel_store);
  } else {
    spdlog::warn("ChannelStore: failed to open '{}', channels will NOT persist",
                 channels_db_path);
  }

  spdlog::info(
      "ChannelManager: initialized (max {} channels, "
      "streaming={}, rules={}, cascade={})",
      kMaxChannels, "FLV+HLS+RTSP+WS+WebRTC", rules_enabled ? "ON" : "OFF",
      cascade_enabled ? "ON" : "OFF");

  // ========================================
  // Step 7: Initialize HTTP Server + API Routes
  // ========================================
  int http_port = config.Get<int>("network.http_port", kDefaultHttpPort);

  // TLS configuration
  loong::network::TlsConfig tls_config;
  tls_config.enabled = config.Get<bool>("tls.enabled", false);
  tls_config.cert_path = config.Get<std::string>("tls.cert_path", "");
  tls_config.key_path = config.Get<std::string>("tls.key_path", "");

  // Create the HTTP(S) server (infrastructure)
  auto http_server = std::make_unique<loong::network::HttpServer>(
      http_host, http_port, tls_config);
  http_server->SetAuthMiddleware(auth_middleware);

  // Create and register API routes (business logic)
  auto api_routes = std::make_unique<loong::network::ApiRoutes>();
  api_routes->SetChannelManager(channel_mgr);
  api_routes->SetRecordIndex(record_index);
  api_routes->SetUserStore(user_store);
  api_routes->SetJwtHelper(jwt_helper);
  api_routes->SetFlvService(flv_service);
  api_routes->SetHlsService(hls_service);
  api_routes->SetSystemMonitor(system_monitor);
  api_routes->SetNotificationManager(notification_mgr);
  api_routes->SetRuleEngine(rule_engine);
  api_routes->SetPlateStore(plate_store);
  api_routes->SetFaceStore(face_store);
  api_routes->SetAnalyticsStore(analytics_store);
  api_routes->SetWebRtcService(webrtc_service);
  api_routes->SetPluginManager(plugin_mgr);
  api_routes->SetCloudBackup(cloud_backup);
  api_routes->SetAlarmManager(alarm_mgr);

  // ========================================
  // Step 7b: Initialize Hot-Reload Manager
  // ========================================
  auto hot_reload_mgr = std::make_shared<loong::core::HotReloadManager>();
  api_routes->SetHotReloadManager(hot_reload_mgr);

  api_routes->Register(*http_server);

  // Mount frontend static files (web/ directory relative to working dir)
  auto web_dir = config.Get<std::string>("web.static_dir", "web");
  http_server->SetStaticDir("/", web_dir);

  // Start RTSP and WsMedia BEFORE HTTP, so they are ready when API
  // requests create/start channels that push frames.
  if (rtsp_server->Start()) {
    spdlog::info("RTSP server: listening on rtsp://{}:{}", http_host,
                 rtsp_port);
  } else {
    spdlog::warn("RTSP server: failed to start (may need root for port {})",
                 rtsp_port);
  }

  if (ws_media_service->Start()) {
    spdlog::info("WsMedia server: listening on ws://{}:{}", http_host,
                 ws_media_port);
  } else {
    spdlog::warn("WsMedia server: failed to start on port {}", ws_media_port);
  }

  if (http_server->Start()) {
    spdlog::info("HTTP server: listening on {}:{}", http_host, http_port);
  } else {
    spdlog::error("HTTP server: FATAL — failed to start on {}:{}", http_host,
                  http_port);
    return EXIT_FAILURE;
  }

  // ========================================
  // Step 8: Initialize WebSocket Server
  // ========================================
  int ws_port = config.Get<int>("network.ws_port", 8081);
  auto ws_server =
      std::make_unique<loong::network::WebSocketServer>(http_host, ws_port);

  if (ws_server->Start()) {
    spdlog::info("WebSocket server: listening on ws://{}:{}", http_host,
                 ws_port);
  } else {
    spdlog::error("WebSocket server: failed to start");
  }

  // Bridge EventBus events to WebSocket broadcast.
  // Keep subscription IDs for cleanup during shutdown.
  auto& event_bus = loong::core::EventBus::Instance();
  auto* ws_ptr = ws_server.get();
  std::vector<uint64_t> event_sub_ids;

  event_sub_ids.push_back(event_bus.Subscribe(
      "system.metrics", [ws_ptr, system_monitor](const std::any&) {
        if (!system_monitor) return;
        auto m = system_monitor->GetLatest();
        nlohmann::json data = {
            {"cpu_usage_percent", m.cpu_usage_percent},
            {"memory_usage_percent", m.memory_usage_percent},
            {"memory_used_mb", m.memory_used_mb},
            {"memory_total_mb", m.memory_total_mb},
            {"uptime_seconds", m.uptime_seconds},
            {"process_threads", m.process_threads},
        };
        ws_ptr->Broadcast("system_status", data);
      }));

  event_sub_ids.push_back(event_bus.Subscribe(
      "ai.detection", [plate_store, face_store](const std::any& data) {
        try {
          auto json_str = std::any_cast<std::string>(data);
          auto j = nlohmann::json::parse(json_str);
          if (!j.contains("secondary_results")) return;
          int channel_id = j.value("channel_id", -1);

          for (const auto& sr : j["secondary_results"]) {
            std::string model = sr.value("model_name", "");

            if ((model == "lpr_ocr" || model == "plate_ocr" ||
                 model == "crnn") &&
                plate_store) {
              loong::ai_engine::PlateRecord plate;
              plate.plate_number = sr.value("plate_text", "");
              plate.plate_color = sr.value("class_name", "");
              plate.confidence = sr.value("confidence", 0.0F);
              plate.channel_id = channel_id;
              plate.timestamp =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
              if (!plate.plate_number.empty()) {
                plate_store->Insert(plate);
              }
            }

            if ((model == "face_attribute" || model == "arcface" ||
                 model == "insightface") &&
                face_store) {
              loong::ai_engine::FaceRecord face;
              face.channel_id = channel_id;
              face.gender = sr.value("gender", "");
              face.age = sr.value("age", 0);
              face.confidence = sr.value("confidence", 0.0F);
              face.timestamp =
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
              face_store->Insert(face);
            }
          }
        } catch (const std::exception& e) {
          spdlog::debug("ai.detection cascade handler: {}", e.what());
        }
      }));

  event_sub_ids.push_back(
      event_bus.Subscribe("alarm.triggered", [ws_ptr](const std::any& data) {
        try {
          auto record = std::any_cast<loong::system::AlarmRecord>(data);
          nlohmann::json alarm_data = {
              {"event", "alarm"},
              {"alarm_id", record.id},
              {"rule_id", record.rule_id},
              {"channel_id", record.channel_id},
              {"event_type", record.event_type},
              {"confidence", record.confidence},
              {"severity",
               loong::system::AlarmSeverityToString(record.severity)},
              {"triggered_at", record.triggered_at},
          };
          ws_ptr->Broadcast("alarm", alarm_data);
        } catch (const std::exception& e) {
          spdlog::debug("alarm.triggered handler: {}", e.what());
        }
      }));

  event_sub_ids.push_back(event_bus.Subscribe(
      "rule.triggered", [ws_ptr, analytics_aggregator](const std::any& data) {
        try {
          auto json_str = std::any_cast<std::string>(data);
          auto rule_data = nlohmann::json::parse(json_str);
          ws_ptr->Broadcast("rule_event", rule_data);

          if (analytics_aggregator) {
            loong::rules::RuleEvent ev;
            ev.rule_id = rule_data.value("rule_id", 0);
            ev.rule_name = rule_data.value("rule_name", "");
            ev.channel_id = rule_data.value("channel_id", 0);
            ev.timestamp =
                rule_data.value("timestamp", static_cast<int64_t>(0));
            auto type_str = rule_data.value("rule_type", "");
            if (type_str == "cross_line") {
              ev.rule_type = loong::rules::RuleType::kCrossLine;
            } else if (type_str == "region_intrusion") {
              ev.rule_type = loong::rules::RuleType::kRegionIntrusion;
            } else if (type_str == "object_counting") {
              ev.rule_type = loong::rules::RuleType::kObjectCounting;
            } else if (type_str == "loitering") {
              ev.rule_type = loong::rules::RuleType::kLoitering;
            }
            analytics_aggregator->IngestEvent(ev);
          }
        } catch (const std::exception& e) {
          spdlog::debug("rule.triggered handler: {}", e.what());
        }
      }));

  // Wire config hot-reload: broadcast changes via WebSocket
  hot_reload_mgr->SetBroadcastCallback(
      [ws_ptr](const std::string& section, const nlohmann::json& cfg) {
        nlohmann::json data = {{"section", section}, {"config", cfg}};
        ws_ptr->Broadcast("config_change", data);
      });

  // Register section handlers for runtime-reloadable config
  hot_reload_mgr->RegisterSection(
      "hls",
      [hls_service](const std::string& /*section*/, const nlohmann::json& cfg) {
        loong::network::HlsConfig hls_cfg;
        hls_cfg.segment_duration_ms = cfg.value("segment_duration_ms", 2000);
        hls_cfg.max_segments = cfg.value("max_segments", 5);
        hls_cfg.max_segment_size_kb = cfg.value("max_segment_size_kb", 4096);
        hls_service->UpdateConfig(hls_cfg);
        spdlog::info("HotReload: HLS config applied (segment={}ms, window={})",
                     hls_cfg.segment_duration_ms, hls_cfg.max_segments);
      });

  hot_reload_mgr->RegisterSection(
      "notifications", [notification_mgr](const std::string& /*section*/,
                                          const nlohmann::json& cfg) {
        if (cfg.contains("smtp")) {
          loong::system::SmtpConfig smtp;
          const auto& s = cfg["smtp"];
          smtp.enabled = s.value("enabled", false);
          smtp.host = s.value("host", "");
          smtp.port = s.value("port", 587);
          smtp.use_tls = s.value("use_tls", true);
          smtp.username = s.value("username", "");
          smtp.password = s.value("password", "");
          smtp.from_address = s.value("from_address", "");
          notification_mgr->ConfigureSmtp(smtp);
        }
        if (cfg.contains("webhook")) {
          loong::system::WebhookConfig wh;
          const auto& w = cfg["webhook"];
          wh.enabled = w.value("enabled", false);
          wh.url = w.value("url", "");
          wh.secret = w.value("secret", "");
          notification_mgr->ConfigureWebhook(wh);
        }
        spdlog::info("HotReload: notification config updated");
      });

  // Start watching the config file
  if (argc > 1) {
    hot_reload_mgr->Start(argv[1]);
  }

  // ========================================
  // Step 9: System Ready
  // ========================================
  const char* http_proto = http_server->IsTlsEnabled() ? "https" : "http";
  spdlog::info("");
  spdlog::info("======== System Ready ========");
  spdlog::info("  HTTP API:   {}://{}:{}/api/", http_proto, http_host,
               http_port);
  spdlog::info("  Health:     {}://{}:{}/api/health", http_proto, http_host,
               http_port);
  spdlog::info("  Metrics:    {}://{}:{}/metrics", http_proto, http_host,
               http_port);
  spdlog::info("  WebSocket:  ws://{}:{}", http_host, ws_port);
  spdlog::info("  RTSP:       rtsp://{}:{}/live/ch{{id}}", http_host,
               rtsp_port);
  spdlog::info("  FLV Live:   {}://{}:{}/live/ch{{id}}.flv", http_proto,
               http_host, http_port);
  spdlog::info("  HLS Live:   {}://{}:{}/live/ch{{id}}/index.m3u8", http_proto,
               http_host, http_port);
  spdlog::info("  WS fMP4:    ws://{}:{}/ws/live/ch{{id}}", http_host,
               ws_media_port);
  spdlog::info("  WebRTC:     {}://{}:{}/api/webrtc/offer", http_proto,
               http_host, http_port);
  spdlog::info("  Web UI:     {}://{}:{}/", http_proto, http_host, http_port);
  spdlog::info("  TLS:        {}", http_server->IsTlsEnabled() ? "ON" : "OFF");
  spdlog::info("  Auth:       JWT (default admin/admin123)");
  spdlog::info("  Rules:      {}", rules_enabled ? "ON" : "OFF");
  spdlog::info("  Analytics:  ON");
  spdlog::info("  WebRTC:     ON (max {} sessions)",
               webrtc_config.max_sessions);
  spdlog::info("==============================");
  spdlog::info("");
  spdlog::info("Press Ctrl+C to stop");

  // Restore persisted channels (create pipelines + start).
  int restored = channel_mgr->LoadChannels();
  if (restored > 0) {
    spdlog::info("Restored {} channel(s) from database", restored);
  }

  // ========================================
  // Main Loop — wait for shutdown signal
  // ========================================
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }

  // ========================================
  // Graceful Shutdown
  // ========================================
  spdlog::info("Shutting down...");

  // Unsubscribe EventBus callbacks first to prevent use-after-free
  // on pointers captured in closures during shutdown.
  for (auto sub_id : event_sub_ids) {
    event_bus.Unsubscribe(sub_id);
  }
  event_sub_ids.clear();
  spdlog::info("EventBus subscriptions cleared");

  channel_mgr->StopAll();
  spdlog::info("All channels stopped");

  rtsp_server->Stop();
  spdlog::info("RTSP server stopped");

  ws_media_service->Stop();
  spdlog::info("WsMedia server stopped");

  ws_server->Stop();
  spdlog::info("WebSocket server stopped");

  http_server->Stop();
  spdlog::info("HTTP server stopped");

  flv_service->StopAll();
  spdlog::info("FLV service stopped");

  hls_service->StopAll();
  spdlog::info("HLS service stopped");

  hot_reload_mgr->Stop();
  spdlog::info("HotReloadManager stopped");

  webrtc_service->StopCleanupTimer();
  webrtc_service->Shutdown();
  spdlog::info("WebRtcService stopped");

  analytics_aggregator->Stop();
  spdlog::info("AnalyticsAggregator stopped");

  if (backup_enabled) {
    cloud_backup->Stop();
    spdlog::info("CloudBackupService stopped");
  }

  // Unload all dynamic plugins before shutdown.
  plugin_mgr->UnloadAll();
  spdlog::info("PluginManager: all plugins unloaded");

  notification_mgr->Stop();
  spdlog::info("NotificationManager stopped");

  // Stop alarm manager
  alarm_mgr->Stop();
  alarm_mgr->Close();
  spdlog::info("AlarmManager stopped");

  rule_store->Close();
  spdlog::info("RuleStore closed");

  // Close analytics store
  analytics_store->Close();
  spdlog::info("AnalyticsStore closed");

  // Close LPR and Face stores
  plate_store->Close();
  spdlog::info("PlateStore closed");

  face_store->Close();
  spdlog::info("FaceStore closed");

  system_monitor->Stop();
  spdlog::info("SystemMonitor stopped");

  storage_cleaner->Stop();
  spdlog::info("StorageCleaner stopped");

  record_index->Close();
  spdlog::info("RecordIndex closed");

  user_store->Close();
  spdlog::info("UserStore closed");

  spdlog::info("Loong AI NVR shutdown complete");
  return EXIT_SUCCESS;
}
