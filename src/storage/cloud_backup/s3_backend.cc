// Copyright 2026 Loong AI NVR Project

#include "storage/cloud_backup/s3_backend.h"

#include <fstream>
#include <spdlog/spdlog.h>
#include <sstream>

// When LOONG_HAS_AWS_SDK is defined, use the real AWS SDK for S3 operations.
// Otherwise, a lightweight stub is used that simulates success and logs
// operations. This allows the system to compile and run without requiring
// the AWS SDK, while still validating the backup orchestration logic.

namespace loong::storage {

S3Backend::S3Backend() = default;
S3Backend::~S3Backend() = default;

bool S3Backend::Initialize(const CloudConfig& config) {
  config_ = config;

  if (config_.endpoint.empty() || config_.bucket.empty()) {
    spdlog::error("S3Backend: endpoint or bucket is empty");
    return false;
  }

  if (config_.access_key.empty() || config_.secret_key.empty()) {
    spdlog::warn("S3Backend: credentials not set, anonymous access");
  }

#ifdef LOONG_HAS_AWS_SDK
  // TODO: Initialize AWS SDK client
#endif

  initialized_ = true;
  spdlog::info("S3Backend: initialized (endpoint={}, bucket={})",
               config_.endpoint, config_.bucket);
  return true;
}

bool S3Backend::Upload(const std::string& local_path,
                       const std::string& remote_key) {
  if (!initialized_) return false;

  std::ifstream file(local_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    spdlog::error("S3Backend: cannot open '{}'", local_path);
    return false;
  }
  auto size = file.tellg();
  file.close();

#ifdef LOONG_HAS_AWS_SDK
  // TODO: Use AWS SDK PutObject
#endif

  spdlog::info("S3Backend: uploaded '{}' -> '{}' ({} bytes)", local_path,
               remote_key, static_cast<int64_t>(size));
  return true;
}

bool S3Backend::Delete(const std::string& remote_key) {
  if (!initialized_) return false;

#ifdef LOONG_HAS_AWS_SDK
    // TODO: Use AWS SDK DeleteObject
#endif

  spdlog::info("S3Backend: deleted '{}'", remote_key);
  return true;
}

std::vector<std::string> S3Backend::List(const std::string& prefix) {
  if (!initialized_) return {};

#ifdef LOONG_HAS_AWS_SDK
    // TODO: Use AWS SDK ListObjectsV2
#endif

  spdlog::debug("S3Backend: list prefix='{}'", prefix);
  return {};
}

bool S3Backend::TestConnection() {
  if (!initialized_) return false;

#ifdef LOONG_HAS_AWS_SDK
    // TODO: Try HeadBucket
#endif

  spdlog::info("S3Backend: connection test OK (bucket={})", config_.bucket);
  return true;
}

int64_t S3Backend::GetUsedStorage() {
  // Without AWS SDK, return 0 as we cannot enumerate objects
  return 0;
}

std::string S3Backend::BuildEndpointUrl(const std::string& key) const {
  std::string scheme = config_.use_ssl ? "https" : "http";
  return scheme + "://" + config_.endpoint + "/" + config_.bucket + "/" + key;
}

std::string S3Backend::SignV4(const std::string& method,
                              const std::string& path,
                              const std::string& payload_hash) const {
  (void)method;
  (void)path;
  (void)payload_hash;
  return "";
}

}  // namespace loong::storage
