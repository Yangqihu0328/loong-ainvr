// Copyright 2026 Loong AI NVR Project

#include "storage/cloud_backup/cloud_backup_service.h"

#include "storage/cloud_backup/s3_backend.h"

#include <chrono>
#include <filesystem>
#include <spdlog/spdlog.h>

namespace loong::storage {

namespace fs = std::filesystem;

static int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

CloudBackupService::CloudBackupService() = default;

CloudBackupService::~CloudBackupService() { Stop(); }

void CloudBackupService::Configure(const CloudConfig& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

CloudConfig CloudBackupService::GetConfig() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return config_;
}

void CloudBackupService::SetPolicy(const BackupPolicy& policy) {
  std::lock_guard<std::mutex> lock(mutex_);
  policy_ = policy;
}

BackupPolicy CloudBackupService::GetPolicy() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return policy_;
}

void CloudBackupService::Start(const std::string& recordings_dir,
                               const std::string& snapshots_dir) {
  if (running_.load()) return;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    recordings_dir_ = recordings_dir;
    snapshots_dir_ = snapshots_dir;

    backend_ = std::make_unique<S3Backend>();
    if (!backend_->Initialize(config_)) {
      spdlog::error("CloudBackupService: failed to initialize S3 backend");
      return;
    }
  }

  running_.store(true);
  worker_ = std::thread(&CloudBackupService::BackupLoop, this);
  spdlog::info("CloudBackupService: started (interval={}s)",
               policy_.upload_interval_sec);
}

void CloudBackupService::Stop() {
  if (!running_.load()) return;
  running_.store(false);
  if (worker_.joinable()) {
    worker_.join();
  }
  spdlog::info("CloudBackupService: stopped");
}

void CloudBackupService::TriggerBackup() { ScanAndUpload(); }

bool CloudBackupService::TestConnection() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!backend_) {
    backend_ = std::make_unique<S3Backend>();
    if (!backend_->Initialize(config_)) return false;
  }
  return backend_->TestConnection();
}

BackupStatus CloudBackupService::GetStatus() const {
  std::lock_guard<std::mutex> lock(mutex_);
  BackupStatus s = status_;
  s.running = running_.load();
  return s;
}

std::vector<BackupTask> CloudBackupService::GetRecentTasks(int limit) const {
  std::lock_guard<std::mutex> lock(mutex_);
  int count = std::min(limit, static_cast<int>(recent_tasks_.size()));
  return {recent_tasks_.end() - count, recent_tasks_.end()};
}

void CloudBackupService::BackupLoop() {
  while (running_.load()) {
    int interval;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      interval = policy_.upload_interval_sec;
    }

    ScanAndUpload();

    for (int i = 0; i < interval && running_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void CloudBackupService::ScanAndUpload() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!backend_) return;

  auto scan_dir = [this](const std::string& dir) {
    if (dir.empty() || !fs::exists(dir)) return;

    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
      if (!running_.load()) break;
      if (!entry.is_regular_file()) continue;

      std::string path = entry.path().string();
      std::string ext = entry.path().extension().string();

      bool should_backup = false;
      if (ext == ".mp4" || ext == ".ts" || ext == ".mkv") {
        should_backup = true;
      } else if (ext == ".jpg" || ext == ".png") {
        should_backup = true;
      }

      if (!should_backup) continue;

      std::string remote_key = MakeRemoteKey(path);
      UploadFile(path, remote_key);
    }
  };

  if (policy_.mode == BackupMode::kFull ||
      policy_.mode == BackupMode::kEventOnly) {
    scan_dir(recordings_dir_);
    scan_dir(snapshots_dir_);
  }
}

std::string CloudBackupService::MakeRemoteKey(
    const std::string& local_path) const {
  // Strip the local base directory to create a relative key
  std::string key = config_.prefix;

  std::string rel = local_path;
  if (!recordings_dir_.empty() && local_path.find(recordings_dir_) == 0) {
    rel = local_path.substr(recordings_dir_.size());
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    key += "recordings/" + rel;
  } else if (!snapshots_dir_.empty() && local_path.find(snapshots_dir_) == 0) {
    rel = local_path.substr(snapshots_dir_.size());
    if (!rel.empty() && rel[0] == '/') rel = rel.substr(1);
    key += "snapshots/" + rel;
  } else {
    key += rel;
  }

  return key;
}

bool CloudBackupService::UploadFile(const std::string& local_path,
                                    const std::string& remote_key) {
  BackupTask task;
  task.local_path = local_path;
  task.remote_key = remote_key;
  task.started_at = NowMs();

  std::error_code ec;
  task.file_size = static_cast<int64_t>(fs::file_size(local_path, ec));
  if (ec) {
    task.error_message = "cannot stat file";
    task.completed = true;
    recent_tasks_.push_back(task);
    return false;
  }

  bool ok = backend_->Upload(local_path, remote_key);
  task.completed = true;
  task.success = ok;
  task.finished_at = NowMs();

  if (ok) {
    status_.total_uploaded++;
    status_.bytes_uploaded += task.file_size;
    status_.last_upload_time = task.finished_at;
  } else {
    status_.total_failed++;
    task.error_message = "upload failed";
    status_.last_error = task.error_message;
  }

  recent_tasks_.push_back(task);
  if (recent_tasks_.size() > kMaxRecentTasks) {
    recent_tasks_.erase(recent_tasks_.begin());
  }

  return ok;
}

}  // namespace loong::storage
