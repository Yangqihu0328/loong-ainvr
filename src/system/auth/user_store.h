// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_SYSTEM_AUTH_USER_STORE_H_
#define LOONG_SYSTEM_AUTH_USER_STORE_H_

#include <cstdint>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace loong::system {

/// User role for RBAC (Role-Based Access Control).
enum class UserRole {
  kAdmin = 0,     // Full system access
  kOperator = 1,  // Channel & recording management
  kViewer = 2,    // View-only access (live/recordings)
};

/// Convert UserRole to string.
inline const char* UserRoleToString(UserRole role) {
  switch (role) {
    case UserRole::kAdmin:
      return "admin";
    case UserRole::kOperator:
      return "operator";
    case UserRole::kViewer:
      return "viewer";
  }
  return "viewer";
}

/// Parse string to UserRole.
inline UserRole ParseUserRole(const std::string& role_str) {
  if (role_str == "admin") return UserRole::kAdmin;
  if (role_str == "operator") return UserRole::kOperator;
  return UserRole::kViewer;
}

/// User account information.
struct UserInfo {
  int64_t id = 0;
  std::string username;
  std::string display_name;
  UserRole role = UserRole::kViewer;
  bool enabled = true;
  int64_t created_at = 0;  // Unix timestamp (seconds)
  int64_t updated_at = 0;
  int64_t last_login = 0;
};

/// SQLite-based user store for authentication.
/// Stores user accounts with hashed passwords (SHA-256 + salt).
class UserStore {
 public:
  UserStore();
  ~UserStore();

  /// Open/create the user database.
  /// Creates default admin account if no users exist.
  bool Open(const std::string& db_path);

  /// Close the database.
  void Close();

  /// Authenticate a user. Returns true and populates user_info on success.
  bool Authenticate(const std::string& username, const std::string& password,
                    UserInfo& user_info);

  /// Create a new user. Returns user ID, or -1 on failure.
  int64_t CreateUser(const std::string& username, const std::string& password,
                     const std::string& display_name, UserRole role);

  /// Update user info (display_name, role, enabled). Does NOT change password.
  bool UpdateUser(int64_t user_id, const std::string& display_name,
                  UserRole role, bool enabled);

  /// Change a user's password.
  bool ChangePassword(int64_t user_id, const std::string& new_password);

  /// Delete a user. Cannot delete the last admin.
  bool DeleteUser(int64_t user_id);

  /// Get user by ID.
  bool GetUser(int64_t user_id, UserInfo& user_info);

  /// Get user by username.
  bool GetUserByName(const std::string& username, UserInfo& user_info);

  /// List all users.
  std::vector<UserInfo> ListUsers();

  /// Get the total number of admin users.
  int AdminCount();

  /// Check if user must change their password (e.g., default admin password).
  bool MustChangePassword(int64_t user_id);

  /// Mark that the user has changed their password (clears the flag).
  void ClearMustChangePassword(int64_t user_id);

  // Non-copyable
  UserStore(const UserStore&) = delete;
  UserStore& operator=(const UserStore&) = delete;

 private:
  bool CreateTables();
  void CreateDefaultAdmin();

  /// Hash a password with the given salt using SHA-256.
  static std::string HashPassword(const std::string& password,
                                  const std::string& salt);

  /// Generate a random 16-byte salt (hex-encoded).
  static std::string GenerateSalt();

  /// Get current Unix timestamp in seconds.
  static int64_t Now();

  sqlite3* db_ = nullptr;
  mutable std::mutex mutex_;
};

}  // namespace loong::system

#endif  // LOONG_SYSTEM_AUTH_USER_STORE_H_
