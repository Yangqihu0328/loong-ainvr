// Copyright 2026 Loong AI NVR Project

#include "system/notification/webhook_channel.h"

#include <iomanip>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <sstream>

// Suppress warnings from third-party header
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include <httplib.h>
#pragma GCC diagnostic pop

#include "spdlog/spdlog.h"

namespace loong::system {

WebhookChannel::WebhookChannel(const WebhookConfig& config) : config_(config) {}

bool WebhookChannel::IsConfigured() const {
  return config_.enabled && !config_.url.empty();
}

void WebhookChannel::UpdateConfig(const WebhookConfig& config) {
  config_ = config;
}

NotificationResult WebhookChannel::Test() {
  NotificationMessage test_msg;
  test_msg.title = "Test Notification";
  test_msg.body = "This is a test notification from Loong AI NVR.";
  test_msg.severity = "info";
  test_msg.timestamp = 0;
  return Send(test_msg);
}

NotificationResult WebhookChannel::Send(const NotificationMessage& msg) {
  if (!IsConfigured()) {
    return {false, "webhook not configured"};
  }

  std::string payload = BuildJsonPayload(msg);
  return PostJson(payload);
}

NotificationResult WebhookChannel::PostJson(const std::string& json_body) {
  // Parse URL to extract scheme, host, port, path
  std::string url = config_.url;
  bool is_https = (url.substr(0, 8) == "https://");
  std::string scheme_stripped = url.substr(is_https ? 8 : 7);

  auto path_pos = scheme_stripped.find('/');
  std::string host_port = (path_pos != std::string::npos)
                              ? scheme_stripped.substr(0, path_pos)
                              : scheme_stripped;
  std::string path =
      (path_pos != std::string::npos) ? scheme_stripped.substr(path_pos) : "/";

  for (int attempt = 0; attempt <= config_.retry_count; ++attempt) {
    try {
      std::unique_ptr<httplib::Client> client;
      if (is_https) {
        client = std::make_unique<httplib::Client>("https://" + host_port);
      } else {
        client = std::make_unique<httplib::Client>("http://" + host_port);
      }

      client->set_connection_timeout(config_.timeout_sec, 0);
      client->set_read_timeout(config_.timeout_sec, 0);

      httplib::Headers headers;
      headers.emplace("Content-Type", config_.content_type);

      if (!config_.secret.empty()) {
        std::string sig = HmacSha256(config_.secret, json_body);
        headers.emplace("X-Signature", "sha256=" + sig);
      }

      auto res = client->Post(path, headers, json_body, config_.content_type);

      if (res && res->status >= 200 && res->status < 300) {
        spdlog::info("WebhookChannel: POST {} → {}", config_.url, res->status);
        return {true, ""};
      }

      std::string err_msg =
          res ? ("HTTP " + std::to_string(res->status)) : "connection failed";
      if (attempt < config_.retry_count) {
        spdlog::warn("WebhookChannel: attempt {}/{} failed: {}, retrying...",
                     attempt + 1, config_.retry_count + 1, err_msg);
        continue;
      }
      return {false, err_msg};

    } catch (const std::exception& e) {
      if (attempt < config_.retry_count) continue;
      return {false, std::string("exception: ") + e.what()};
    }
  }

  return {false, "max retries exceeded"};
}

std::string WebhookChannel::BuildJsonPayload(const NotificationMessage& msg) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"event\":\"alarm\",";
  oss << "\"title\":\"" << msg.title << "\",";
  oss << "\"body\":\"" << msg.body << "\",";
  oss << "\"severity\":\"" << msg.severity << "\",";
  oss << "\"channel_id\":" << msg.channel_id << ",";
  oss << "\"event_type\":\"" << msg.event_type << "\",";
  oss << "\"confidence\":" << msg.confidence << ",";
  oss << "\"timestamp\":" << msg.timestamp;
  if (!msg.screenshot_path.empty()) {
    oss << ",\"screenshot\":\"" << msg.screenshot_path << "\"";
  }
  oss << "}";
  return oss.str();
}

std::string WebhookChannel::HmacSha256(const std::string& key,
                                       const std::string& data) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.c_str()), data.size(), hash,
       &hash_len);

  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hash_len; ++i) {
    hex << std::setw(2) << static_cast<int>(hash[i]);
  }
  return hex.str();
}

}  // namespace loong::system
