// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_STORAGE_CLOUD_BACKUP_CLOUD_BACKUP_SERVICE_H_
#define LOONG_STORAGE_CLOUD_BACKUP_CLOUD_BACKUP_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace loong::storage {

/// Cloud backup provider types.
enum class CloudProvider {
  kS3,  // AWS S3 compatible (MinIO, Aliyun OSS, etc.)
  kNone,
};

/// Cloud storage connection configuration.
struct CloudConfig {
  bool enabled = false;
  CloudProvider provider = CloudProvider::kS3;
  std::string endpoint;  // e.g. "s3.amazonaws.com" or "minio.local:9000"
  std::string region = "us-east-1";
  std::string bucket;
  std::string access_key;
  std::string secret_key;
  bool use_ssl = true;
  std::string prefix = "loong-nvr/";  // S3 key prefix
};

/// Backup policy: what to back up and how long to keep.
enum class BackupMode {
  kEventOnly,  // Only event-triggered recordings/snapshots
  kFull,       // All recordings
  kCustom,     // Specific channels
};

struct BackupPolicy {
  BackupMode mode = BackupMode::kEventOnly;
  int retention_days = 30;        // Cloud retention (0 = forever)
  int upload_interval_sec = 300;  // Check interval for new files
  std::vector<int> channel_ids;   // For kCustom mode
};

/// Status of a single backup operation.
struct BackupTask {
  std::string local_path;
  std::string remote_key;
  int64_t file_size = 0;
  bool completed = false;
  bool success = false;
  std::string error_message;
  int64_t started_at = 0;
  int64_t finished_at = 0;
};

/// Overall backup service status.
struct BackupStatus {
  bool running = false;
  int64_t total_uploaded = 0;
  int64_t total_failed = 0;
  int64_t bytes_uploaded = 0;
  int64_t last_upload_time = 0;
  std::string last_error;
};

/// Abstract interface for cloud storage operations.
class CloudStorageBackend {
 public:
  virtual ~CloudStorageBackend() = default;

  /// Initialize the backend with configuration.
  virtual bool Initialize(const CloudConfig& config) = 0;

  /// Upload a file to cloud storage.
  virtual bool Upload(const std::string& local_path,
                      const std::string& remote_key) = 0;

  /// Delete a remote object.
  virtual bool Delete(const std::string& remote_key) = 0;

  /// List remote objects with a prefix.
  virtual std::vector<std::string> List(const std::string& prefix) = 0;

  /// Test the connection.
  virtual bool TestConnection() = 0;

  /// Get used storage in bytes.
  virtual int64_t GetUsedStorage() = 0;
};

/// Cloud backup service that periodically uploads recordings/snapshots
/// to an S3-compatible cloud storage backend.
class CloudBackupService {
 public:
  CloudBackupService();
  ~CloudBackupService();

  /// Configure cloud storage connection.
  void Configure(const CloudConfig& config);
  CloudConfig GetConfig() const;

  /// Configure backup policy.
  void SetPolicy(const BackupPolicy& policy);
  BackupPolicy GetPolicy() const;

  /// Start the backup service (background thread).
  void Start(const std::string& recordings_dir,
             const std::string& snapshots_dir);

  /// Stop the backup service.
  void Stop();

  /// Manually trigger a backup scan.
  void TriggerBackup();

  /// Test cloud connection.
  bool TestConnection();

  /// Get current status.
  BackupStatus GetStatus() const;

  /// Get recent backup tasks.
  std::vector<BackupTask> GetRecentTasks(int limit = 20) const;

  // Non-copyable
  CloudBackupService(const CloudBackupService&) = delete;
  CloudBackupService& operator=(const CloudBackupService&) = delete;

 private:
  void BackupLoop();
  void ScanAndUpload();
  std::string MakeRemoteKey(const std::string& local_path) const;
  bool UploadFile(const std::string& local_path, const std::string& remote_key);

  CloudConfig config_;
  BackupPolicy policy_;
  std::string recordings_dir_;
  std::string snapshots_dir_;

  std::unique_ptr<CloudStorageBackend> backend_;
  mutable std::mutex mutex_;
  std::atomic<bool> running_{false};
  std::thread worker_;

  BackupStatus status_;
  std::vector<BackupTask> recent_tasks_;
  static constexpr size_t kMaxRecentTasks = 100;
};

}  // namespace loong::storage

#endif  // LOONG_STORAGE_CLOUD_BACKUP_CLOUD_BACKUP_SERVICE_H_
