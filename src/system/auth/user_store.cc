// Copyright 2026 Loong AI NVR Project

#include "system/auth/user_store.h"

#include <chrono>
#include <random>

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "spdlog/spdlog.h"

namespace loong::system {

namespace {

constexpr const char* kDefaultAdminUser = "admin";
constexpr const char* kDefaultAdminPass = "admin123";
constexpr const char* kDefaultAdminName = "Administrator";

}  // namespace

UserStore::UserStore() = default;

UserStore::~UserStore() { Close(); }

bool UserStore::Open(const std::string& db_path) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }

  int rc = sqlite3_open(db_path.c_str(), &db_);
  if (rc != SQLITE_OK) {
    spdlog::error("UserStore: failed to open database '{}': {}", db_path,
                  sqlite3_errmsg(db_));
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // Enable WAL mode for better concurrency
  sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);

  if (!CreateTables()) {
    sqlite3_close(db_);
    db_ = nullptr;
    return false;
  }

  // Create default admin if no users exist
  CreateDefaultAdmin();

  spdlog::info("UserStore: opened '{}'", db_path);
  return true;
}

void UserStore::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

bool UserStore::CreateTables() {
  const char* sql = R"(
    CREATE TABLE IF NOT EXISTS users (
      id          INTEGER PRIMARY KEY AUTOINCREMENT,
      username    TEXT NOT NULL UNIQUE,
      password    TEXT NOT NULL,
      salt        TEXT NOT NULL,
      display_name TEXT NOT NULL DEFAULT '',
      role        TEXT NOT NULL DEFAULT 'viewer',
      enabled     INTEGER NOT NULL DEFAULT 1,
      must_change_password INTEGER NOT NULL DEFAULT 0,
      created_at  INTEGER NOT NULL,
      updated_at  INTEGER NOT NULL,
      last_login  INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);
  )";

  char* err_msg = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    spdlog::error("UserStore: failed to create tables: {}", err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  // Migration: add must_change_password column for existing databases
  sqlite3_exec(db_,
               "ALTER TABLE users ADD COLUMN must_change_password "
               "INTEGER NOT NULL DEFAULT 0;",
               nullptr, nullptr, nullptr);

  return true;
}

void UserStore::CreateDefaultAdmin() {
  // Check if any users exist
  const char* sql = "SELECT COUNT(*) FROM users;";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);

  if (count > 0) return;

  // Create default admin with must_change_password flag
  std::string salt = GenerateSalt();
  std::string hash = HashPassword(kDefaultAdminPass, salt);
  int64_t now = Now();

  const char* insert_sql =
      "INSERT INTO users (username, password, salt, display_name, role, "
      "enabled, must_change_password, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, 'admin', 1, 1, ?, ?);";

  sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, kDefaultAdminUser, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, kDefaultAdminName, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 5, now);
  sqlite3_bind_int64(stmt, 6, now);

  if (sqlite3_step(stmt) == SQLITE_DONE) {
    spdlog::info("UserStore: created default admin account (user: {}, pass: {})",
                 kDefaultAdminUser, kDefaultAdminPass);
  }
  sqlite3_finalize(stmt);
}

bool UserStore::Authenticate(const std::string& username,
                             const std::string& password,
                             UserInfo& user_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "SELECT id, username, password, salt, display_name, role, enabled, "
      "created_at, updated_at, last_login FROM users WHERE username = ?;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

  bool success = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    std::string stored_hash =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    std::string salt =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    bool enabled = sqlite3_column_int(stmt, 6) != 0;

    if (enabled && HashPassword(password, salt) == stored_hash) {
      user_info.id = sqlite3_column_int64(stmt, 0);
      user_info.username =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      user_info.display_name =
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
      user_info.role = ParseUserRole(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5)));
      user_info.enabled = enabled;
      user_info.created_at = sqlite3_column_int64(stmt, 7);
      user_info.updated_at = sqlite3_column_int64(stmt, 8);
      user_info.last_login = sqlite3_column_int64(stmt, 9);

      // Update last_login
      sqlite3_finalize(stmt);
      int64_t now = Now();
      const char* update_sql =
          "UPDATE users SET last_login = ? WHERE id = ?;";
      sqlite3_prepare_v2(db_, update_sql, -1, &stmt, nullptr);
      sqlite3_bind_int64(stmt, 1, now);
      sqlite3_bind_int64(stmt, 2, user_info.id);
      sqlite3_step(stmt);
      user_info.last_login = now;

      success = true;
    }
  }

  sqlite3_finalize(stmt);
  return success;
}

int64_t UserStore::CreateUser(const std::string& username,
                              const std::string& password,
                              const std::string& display_name, UserRole role) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return -1;

  std::string salt = GenerateSalt();
  std::string hash = HashPassword(password, salt);
  int64_t now = Now();

  const char* sql =
      "INSERT INTO users (username, password, salt, display_name, role, "
      "enabled, created_at, updated_at) "
      "VALUES (?, ?, ?, ?, ?, 1, ?, ?);";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, display_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, UserRoleToString(role), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, now);
  sqlite3_bind_int64(stmt, 7, now);

  int64_t user_id = -1;
  if (sqlite3_step(stmt) == SQLITE_DONE) {
    user_id = sqlite3_last_insert_rowid(db_);
    spdlog::info("UserStore: created user '{}' (id={}, role={})", username,
                 user_id, UserRoleToString(role));
  } else {
    spdlog::warn("UserStore: failed to create user '{}': {}", username,
                 sqlite3_errmsg(db_));
  }

  sqlite3_finalize(stmt);
  return user_id;
}

bool UserStore::UpdateUser(int64_t user_id, const std::string& display_name,
                           UserRole role, bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "UPDATE users SET display_name = ?, role = ?, enabled = ?, "
      "updated_at = ? WHERE id = ?;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, display_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, UserRoleToString(role), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, enabled ? 1 : 0);
  sqlite3_bind_int64(stmt, 4, Now());
  sqlite3_bind_int64(stmt, 5, user_id);

  bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
  sqlite3_finalize(stmt);
  return ok;
}

bool UserStore::ChangePassword(int64_t user_id,
                               const std::string& new_password) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  std::string salt = GenerateSalt();
  std::string hash = HashPassword(new_password, salt);

  const char* sql =
      "UPDATE users SET password = ?, salt = ?, updated_at = ? WHERE id = ?;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, Now());
  sqlite3_bind_int64(stmt, 4, user_id);

  bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
  sqlite3_finalize(stmt);
  return ok;
}

bool UserStore::DeleteUser(int64_t user_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  // Check: cannot delete the last admin
  UserInfo info;
  {
    const char* sql =
        "SELECT role FROM users WHERE id = ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, user_id);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      info.role = ParseUserRole(
          reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
  }

  if (info.role == UserRole::kAdmin) {
    const char* count_sql =
        "SELECT COUNT(*) FROM users WHERE role = 'admin';";
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, count_sql, -1, &stmt, nullptr);
    int admin_count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
      admin_count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (admin_count <= 1) {
      spdlog::warn("UserStore: cannot delete the last admin user");
      return false;
    }
  }

  const char* sql = "DELETE FROM users WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, user_id);

  bool ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(db_) > 0;
  sqlite3_finalize(stmt);
  return ok;
}

bool UserStore::GetUser(int64_t user_id, UserInfo& user_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "SELECT id, username, display_name, role, enabled, created_at, "
      "updated_at, last_login FROM users WHERE id = ?;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, user_id);

  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    user_info.id = sqlite3_column_int64(stmt, 0);
    user_info.username =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    user_info.display_name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    user_info.role = ParseUserRole(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    user_info.enabled = sqlite3_column_int(stmt, 4) != 0;
    user_info.created_at = sqlite3_column_int64(stmt, 5);
    user_info.updated_at = sqlite3_column_int64(stmt, 6);
    user_info.last_login = sqlite3_column_int64(stmt, 7);
    found = true;
  }

  sqlite3_finalize(stmt);
  return found;
}

bool UserStore::GetUserByName(const std::string& username,
                              UserInfo& user_info) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "SELECT id, username, display_name, role, enabled, created_at, "
      "updated_at, last_login FROM users WHERE username = ?;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

  bool found = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    user_info.id = sqlite3_column_int64(stmt, 0);
    user_info.username =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    user_info.display_name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    user_info.role = ParseUserRole(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    user_info.enabled = sqlite3_column_int(stmt, 4) != 0;
    user_info.created_at = sqlite3_column_int64(stmt, 5);
    user_info.updated_at = sqlite3_column_int64(stmt, 6);
    user_info.last_login = sqlite3_column_int64(stmt, 7);
    found = true;
  }

  sqlite3_finalize(stmt);
  return found;
}

std::vector<UserInfo> UserStore::ListUsers() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<UserInfo> users;
  if (!db_) return users;

  const char* sql =
      "SELECT id, username, display_name, role, enabled, created_at, "
      "updated_at, last_login FROM users ORDER BY id;";

  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    UserInfo info;
    info.id = sqlite3_column_int64(stmt, 0);
    info.username =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    info.display_name =
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    info.role = ParseUserRole(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
    info.enabled = sqlite3_column_int(stmt, 4) != 0;
    info.created_at = sqlite3_column_int64(stmt, 5);
    info.updated_at = sqlite3_column_int64(stmt, 6);
    info.last_login = sqlite3_column_int64(stmt, 7);
    users.push_back(std::move(info));
  }

  sqlite3_finalize(stmt);
  return users;
}

int UserStore::AdminCount() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return 0;

  const char* sql = "SELECT COUNT(*) FROM users WHERE role = 'admin';";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  }
  sqlite3_finalize(stmt);
  return count;
}

std::string UserStore::HashPassword(const std::string& password,
                                    const std::string& salt) {
  std::string input = salt + password;

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, input.data(), input.size());
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);

  // Convert to hex string
  std::string hex;
  hex.reserve(static_cast<size_t>(hash_len) * 2);
  static const char kHexChars[] = "0123456789abcdef";
  for (unsigned int i = 0; i < hash_len; ++i) {
    hex.push_back(kHexChars[(hash[i] >> 4) & 0x0f]);
    hex.push_back(kHexChars[hash[i] & 0x0f]);
  }
  return hex;
}

std::string UserStore::GenerateSalt() {
  unsigned char bytes[16];
  RAND_bytes(bytes, sizeof(bytes));

  std::string hex;
  hex.reserve(sizeof(bytes) * 2);
  static const char kHexChars[] = "0123456789abcdef";
  for (auto b : bytes) {
    hex.push_back(kHexChars[(b >> 4) & 0x0f]);
    hex.push_back(kHexChars[b & 0x0f]);
  }
  return hex;
}

bool UserStore::MustChangePassword(int64_t user_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return false;

  const char* sql =
      "SELECT must_change_password FROM users WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, user_id);

  bool must_change = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    must_change = sqlite3_column_int(stmt, 0) != 0;
  }
  sqlite3_finalize(stmt);
  return must_change;
}

void UserStore::ClearMustChangePassword(int64_t user_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_) return;

  const char* sql =
      "UPDATE users SET must_change_password = 0, updated_at = ? WHERE id = ?;";
  sqlite3_stmt* stmt = nullptr;
  sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
  sqlite3_bind_int64(stmt, 1, Now());
  sqlite3_bind_int64(stmt, 2, user_id);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

int64_t UserStore::Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace loong::system
