// Copyright 2026 Loong AI NVR Project
// Unit tests for Cloud Backup Service (FEAT-6.3)

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "storage/cloud_backup/cloud_backup_service.h"
#include "storage/cloud_backup/s3_backend.h"

namespace loong::storage {
namespace {

// ============================================================
// CloudConfig Tests
// ============================================================

TEST(CloudConfigTest, Defaults) {
  CloudConfig config;
  EXPECT_FALSE(config.enabled);
  EXPECT_EQ(config.provider, CloudProvider::kS3);
  EXPECT_TRUE(config.endpoint.empty());
  EXPECT_EQ(config.region, "us-east-1");
  EXPECT_TRUE(config.bucket.empty());
  EXPECT_TRUE(config.use_ssl);
  EXPECT_EQ(config.prefix, "loong-nvr/");
}

TEST(BackupPolicyTest, Defaults) {
  BackupPolicy policy;
  EXPECT_EQ(policy.mode, BackupMode::kEventOnly);
  EXPECT_EQ(policy.retention_days, 30);
  EXPECT_EQ(policy.upload_interval_sec, 300);
  EXPECT_TRUE(policy.channel_ids.empty());
}

// ============================================================
// S3Backend Tests
// ============================================================

class S3BackendTest : public ::testing::Test {
 protected:
  S3Backend backend_;
};

TEST_F(S3BackendTest, InitializeWithEmptyEndpoint) {
  CloudConfig config;
  config.bucket = "test-bucket";
  EXPECT_FALSE(backend_.Initialize(config));
}

TEST_F(S3BackendTest, InitializeWithEmptyBucket) {
  CloudConfig config;
  config.endpoint = "s3.amazonaws.com";
  EXPECT_FALSE(backend_.Initialize(config));
}

TEST_F(S3BackendTest, InitializeOk) {
  CloudConfig config;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "test";
  config.secret_key = "secret";
  EXPECT_TRUE(backend_.Initialize(config));
}

TEST_F(S3BackendTest, UploadBeforeInit) {
  EXPECT_FALSE(backend_.Upload("/tmp/test.mp4", "test.mp4"));
}

TEST_F(S3BackendTest, DeleteBeforeInit) {
  EXPECT_FALSE(backend_.Delete("test.mp4"));
}

TEST_F(S3BackendTest, ListBeforeInit) {
  auto list = backend_.List("prefix/");
  EXPECT_TRUE(list.empty());
}

TEST_F(S3BackendTest, TestConnectionBeforeInit) {
  EXPECT_FALSE(backend_.TestConnection());
}

TEST_F(S3BackendTest, TestConnectionAfterInit) {
  CloudConfig config;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "test";
  config.secret_key = "secret";
  backend_.Initialize(config);
  EXPECT_TRUE(backend_.TestConnection());
}

TEST_F(S3BackendTest, UploadNonexistentFile) {
  CloudConfig config;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "test";
  config.secret_key = "secret";
  backend_.Initialize(config);
  EXPECT_FALSE(backend_.Upload("/tmp/does_not_exist_xyz.mp4", "test.mp4"));
}

TEST_F(S3BackendTest, UploadExistingFile) {
  CloudConfig config;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "test";
  config.secret_key = "secret";
  backend_.Initialize(config);

  std::string tmp_file = "/tmp/loong_s3_test_upload.txt";
  std::ofstream ofs(tmp_file);
  ofs << "test content";
  ofs.close();

  EXPECT_TRUE(backend_.Upload(tmp_file, "test/upload.txt"));
  std::filesystem::remove(tmp_file);
}

TEST_F(S3BackendTest, GetUsedStorage) {
  CloudConfig config;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  backend_.Initialize(config);
  EXPECT_EQ(backend_.GetUsedStorage(), 0);
}

// ============================================================
// CloudBackupService Tests
// ============================================================

class CloudBackupServiceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test_dir_ = "/tmp/loong_backup_test_" +
                std::to_string(std::hash<std::thread::id>{}(
                    std::this_thread::get_id()));
    std::filesystem::create_directories(test_dir_ + "/recordings");
    std::filesystem::create_directories(test_dir_ + "/snapshots");
  }

  void TearDown() override { std::filesystem::remove_all(test_dir_); }

  std::string test_dir_;
};

TEST_F(CloudBackupServiceTest, DefaultStatus) {
  CloudBackupService svc;
  auto status = svc.GetStatus();
  EXPECT_FALSE(status.running);
  EXPECT_EQ(status.total_uploaded, 0);
  EXPECT_EQ(status.total_failed, 0);
}

TEST_F(CloudBackupServiceTest, ConfigureAndGet) {
  CloudBackupService svc;
  CloudConfig config;
  config.enabled = true;
  config.endpoint = "s3.test.com";
  config.bucket = "my-bucket";
  svc.Configure(config);

  auto got = svc.GetConfig();
  EXPECT_TRUE(got.enabled);
  EXPECT_EQ(got.endpoint, "s3.test.com");
  EXPECT_EQ(got.bucket, "my-bucket");
}

TEST_F(CloudBackupServiceTest, SetPolicyAndGet) {
  CloudBackupService svc;
  BackupPolicy policy;
  policy.mode = BackupMode::kFull;
  policy.retention_days = 60;
  policy.upload_interval_sec = 600;
  svc.SetPolicy(policy);

  auto got = svc.GetPolicy();
  EXPECT_EQ(got.mode, BackupMode::kFull);
  EXPECT_EQ(got.retention_days, 60);
  EXPECT_EQ(got.upload_interval_sec, 600);
}

TEST_F(CloudBackupServiceTest, TestConnectionUnconfigured) {
  CloudBackupService svc;
  EXPECT_FALSE(svc.TestConnection());
}

TEST_F(CloudBackupServiceTest, TestConnectionConfigured) {
  CloudBackupService svc;
  CloudConfig config;
  config.enabled = true;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "key";
  config.secret_key = "secret";
  svc.Configure(config);
  EXPECT_TRUE(svc.TestConnection());
}

TEST_F(CloudBackupServiceTest, RecentTasksEmpty) {
  CloudBackupService svc;
  auto tasks = svc.GetRecentTasks();
  EXPECT_TRUE(tasks.empty());
}

TEST_F(CloudBackupServiceTest, TriggerBackupNoFiles) {
  CloudBackupService svc;
  CloudConfig config;
  config.enabled = true;
  config.endpoint = "minio.local:9000";
  config.bucket = "test-bucket";
  config.access_key = "key";
  config.secret_key = "secret";
  svc.Configure(config);

  BackupPolicy policy;
  policy.mode = BackupMode::kFull;
  svc.SetPolicy(policy);

  svc.Start(test_dir_ + "/recordings", test_dir_ + "/snapshots");
  auto status = svc.GetStatus();
  EXPECT_TRUE(status.running);
  svc.Stop();
  EXPECT_FALSE(svc.GetStatus().running);
}

}  // namespace
}  // namespace loong::storage
