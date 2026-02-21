// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_CLOUD_BACKUP_S3_BACKEND_H_
#define LOONG_STORAGE_CLOUD_BACKUP_S3_BACKEND_H_

#include "storage/cloud_backup/cloud_backup_service.h"

namespace loong::storage {

/// S3-compatible cloud storage backend.
///
/// Uses HTTP multipart upload to communicate with S3/MinIO/OSS.
/// Implements AWS Signature V4 for authentication.
/// Falls back to a stub implementation when AWS SDK is not available.
class S3Backend : public CloudStorageBackend {
 public:
  S3Backend();
  ~S3Backend() override;

  bool Initialize(const CloudConfig& config) override;
  bool Upload(const std::string& local_path,
              const std::string& remote_key) override;
  bool Delete(const std::string& remote_key) override;
  std::vector<std::string> List(const std::string& prefix) override;
  bool TestConnection() override;
  int64_t GetUsedStorage() override;

 private:
  std::string SignV4(const std::string& method, const std::string& path,
                     const std::string& payload_hash) const;
  std::string BuildEndpointUrl(const std::string& key) const;

  CloudConfig config_;
  bool initialized_ = false;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_CLOUD_BACKUP_S3_BACKEND_H_
