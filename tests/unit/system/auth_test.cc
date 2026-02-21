// Copyright 2026 Loong AI NVR Project

#include "gtest/gtest.h"
#include "system/auth/auth_middleware.h"
#include "system/auth/jwt_helper.h"
#include "system/auth/user_store.h"

#include <cstdio>
#include <string>

namespace loong {
namespace system {
namespace {

constexpr const char* kTestDb = "/tmp/loong_auth_test.db";
constexpr const char* kJwtSecret = "test-secret-key-for-unit-tests-32bytes!";

class UserStoreTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::remove(kTestDb);
    store_ = std::make_shared<UserStore>();
    ASSERT_TRUE(store_->Open(kTestDb));
  }

  void TearDown() override {
    store_->Close();
    std::remove(kTestDb);
  }

  std::shared_ptr<UserStore> store_;
};

TEST_F(UserStoreTest, DefaultAdminCreated) {
  auto users = store_->ListUsers();
  ASSERT_EQ(users.size(), 1u);
  EXPECT_EQ(users[0].username, "admin");
  EXPECT_EQ(users[0].role, UserRole::kAdmin);
  EXPECT_TRUE(users[0].enabled);
}

TEST_F(UserStoreTest, AuthenticateDefaultAdmin) {
  UserInfo info;
  EXPECT_TRUE(store_->Authenticate("admin", "admin123", info));
  EXPECT_EQ(info.username, "admin");
  EXPECT_EQ(info.role, UserRole::kAdmin);
}

TEST_F(UserStoreTest, AuthenticateWrongPassword) {
  UserInfo info;
  EXPECT_FALSE(store_->Authenticate("admin", "wrongpass", info));
}

TEST_F(UserStoreTest, AuthenticateNonExistentUser) {
  UserInfo info;
  EXPECT_FALSE(store_->Authenticate("nobody", "password", info));
}

TEST_F(UserStoreTest, CreateAndAuthenticateUser) {
  int64_t id = store_->CreateUser("operator1", "pass123456", "Operator One",
                                  UserRole::kOperator);
  EXPECT_GT(id, 0);

  UserInfo info;
  EXPECT_TRUE(store_->Authenticate("operator1", "pass123456", info));
  EXPECT_EQ(info.username, "operator1");
  EXPECT_EQ(info.display_name, "Operator One");
  EXPECT_EQ(info.role, UserRole::kOperator);
}

TEST_F(UserStoreTest, DuplicateUsernameFails) {
  int64_t id1 =
      store_->CreateUser("user1", "pass123456", "User One", UserRole::kViewer);
  EXPECT_GT(id1, 0);

  int64_t id2 =
      store_->CreateUser("user1", "otherpass", "Duplicate", UserRole::kViewer);
  EXPECT_LT(id2, 0);
}

TEST_F(UserStoreTest, ChangePassword) {
  UserInfo info;
  ASSERT_TRUE(store_->Authenticate("admin", "admin123", info));

  EXPECT_TRUE(store_->ChangePassword(info.id, "newpass456"));

  EXPECT_FALSE(store_->Authenticate("admin", "admin123", info));
  EXPECT_TRUE(store_->Authenticate("admin", "newpass456", info));
}

TEST_F(UserStoreTest, UpdateUser) {
  int64_t id =
      store_->CreateUser("viewer1", "pass123456", "Viewer", UserRole::kViewer);
  EXPECT_GT(id, 0);

  EXPECT_TRUE(store_->UpdateUser(id, "New Name", UserRole::kOperator, true));

  UserInfo info;
  EXPECT_TRUE(store_->GetUser(id, info));
  EXPECT_EQ(info.display_name, "New Name");
  EXPECT_EQ(info.role, UserRole::kOperator);
}

TEST_F(UserStoreTest, DisableUser) {
  int64_t id = store_->CreateUser("disabled1", "pass123456", "Will Disable",
                                  UserRole::kViewer);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(store_->UpdateUser(id, "Will Disable", UserRole::kViewer, false));

  UserInfo info;
  EXPECT_FALSE(store_->Authenticate("disabled1", "pass123456", info));
}

TEST_F(UserStoreTest, DeleteUser) {
  int64_t id = store_->CreateUser("todelete", "pass123456", "Delete Me",
                                  UserRole::kViewer);
  ASSERT_GT(id, 0);

  EXPECT_TRUE(store_->DeleteUser(id));

  UserInfo info;
  EXPECT_FALSE(store_->GetUser(id, info));
}

TEST_F(UserStoreTest, CannotDeleteLastAdmin) {
  auto users = store_->ListUsers();
  ASSERT_EQ(users.size(), 1u);
  EXPECT_FALSE(store_->DeleteUser(users[0].id));
}

TEST_F(UserStoreTest, ListUsers) {
  store_->CreateUser("user1", "pass123456", "User 1", UserRole::kViewer);
  store_->CreateUser("user2", "pass123456", "User 2", UserRole::kOperator);

  auto users = store_->ListUsers();
  EXPECT_EQ(users.size(), 3u);  // admin + user1 + user2
}

TEST_F(UserStoreTest, GetUserByName) {
  UserInfo info;
  EXPECT_TRUE(store_->GetUserByName("admin", info));
  EXPECT_EQ(info.username, "admin");

  EXPECT_FALSE(store_->GetUserByName("nonexistent", info));
}

// ========================================
// JWT Helper Tests
// ========================================

class JwtHelperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    jwt_ = std::make_shared<JwtHelper>(kJwtSecret, 3600);
  }

  std::shared_ptr<JwtHelper> jwt_;
};

TEST_F(JwtHelperTest, GenerateAndVerify) {
  UserInfo user;
  user.id = 42;
  user.username = "testuser";
  user.role = UserRole::kOperator;

  std::string token = jwt_->GenerateToken(user);
  EXPECT_FALSE(token.empty());

  JwtClaims claims;
  EXPECT_TRUE(jwt_->VerifyToken(token, claims));
  EXPECT_EQ(claims.user_id, 42);
  EXPECT_EQ(claims.username, "testuser");
  EXPECT_EQ(claims.role, UserRole::kOperator);
}

TEST_F(JwtHelperTest, InvalidToken) {
  JwtClaims claims;
  EXPECT_FALSE(jwt_->VerifyToken("invalid.token.here", claims));
}

TEST_F(JwtHelperTest, TamperedToken) {
  UserInfo user;
  user.id = 1;
  user.username = "admin";
  user.role = UserRole::kAdmin;

  std::string token = jwt_->GenerateToken(user);

  // Tamper with payload
  std::string tampered = token;
  auto dot = tampered.find('.');
  if (dot != std::string::npos && dot + 1 < tampered.size()) {
    tampered[dot + 1] = 'X';
  }

  JwtClaims claims;
  EXPECT_FALSE(jwt_->VerifyToken(tampered, claims));
}

TEST_F(JwtHelperTest, ExpiredToken) {
  auto expired_jwt = std::make_shared<JwtHelper>(kJwtSecret, -1);
  UserInfo user;
  user.id = 1;
  user.username = "admin";
  user.role = UserRole::kAdmin;

  std::string token = expired_jwt->GenerateToken(user);

  JwtClaims claims;
  EXPECT_FALSE(jwt_->VerifyToken(token, claims));
}

TEST_F(JwtHelperTest, WrongSecret) {
  UserInfo user;
  user.id = 1;
  user.username = "admin";
  user.role = UserRole::kAdmin;

  std::string token = jwt_->GenerateToken(user);

  auto other_jwt =
      std::make_shared<JwtHelper>("different-secret-key!!!!!!!!!!!", 3600);
  JwtClaims claims;
  EXPECT_FALSE(other_jwt->VerifyToken(token, claims));
}

// ========================================
// Auth Middleware Tests
// ========================================

class AuthMiddlewareTest : public ::testing::Test {
 protected:
  void SetUp() override {
    std::remove(kTestDb);
    store_ = std::make_shared<UserStore>();
    ASSERT_TRUE(store_->Open(kTestDb));
    jwt_ = std::make_shared<JwtHelper>(kJwtSecret, 3600);
    middleware_ = std::make_shared<AuthMiddleware>(jwt_, store_);
  }

  void TearDown() override {
    store_->Close();
    std::remove(kTestDb);
  }

  std::shared_ptr<UserStore> store_;
  std::shared_ptr<JwtHelper> jwt_;
  std::shared_ptr<AuthMiddleware> middleware_;
};

TEST_F(AuthMiddlewareTest, AuthenticateWithValidToken) {
  UserInfo user;
  ASSERT_TRUE(store_->Authenticate("admin", "admin123", user));

  std::string token = jwt_->GenerateToken(user);
  std::string raw =
      "GET /api/channels HTTP/1.1\r\n"
      "Authorization: Bearer " +
      token +
      "\r\n"
      "\r\n";

  auto ctx = middleware_->Authenticate(raw);
  EXPECT_TRUE(ctx.authenticated);
  EXPECT_EQ(ctx.claims.username, "admin");
}

TEST_F(AuthMiddlewareTest, AuthenticateWithoutToken) {
  std::string raw = "GET /api/channels HTTP/1.1\r\n\r\n";
  auto ctx = middleware_->Authenticate(raw);
  EXPECT_FALSE(ctx.authenticated);
}

TEST_F(AuthMiddlewareTest, RequiresAuth) {
  EXPECT_FALSE(AuthMiddleware::RequiresAuth("/api/auth/login"));
  EXPECT_TRUE(AuthMiddleware::RequiresAuth("/api/channels"));
  EXPECT_TRUE(AuthMiddleware::RequiresAuth("/api/users"));
  EXPECT_TRUE(AuthMiddleware::RequiresAuth("/api/system/status"));
}

TEST_F(AuthMiddlewareTest, AdminHasFullAccess) {
  EXPECT_TRUE(
      AuthMiddleware::HasPermission(UserRole::kAdmin, "GET", "/api/users"));
  EXPECT_TRUE(AuthMiddleware::HasPermission(UserRole::kAdmin, "DELETE",
                                            "/api/users/1"));
  EXPECT_TRUE(
      AuthMiddleware::HasPermission(UserRole::kAdmin, "POST", "/api/channels"));
}

TEST_F(AuthMiddlewareTest, ViewerReadOnly) {
  EXPECT_TRUE(
      AuthMiddleware::HasPermission(UserRole::kViewer, "GET", "/api/channels"));
  EXPECT_FALSE(AuthMiddleware::HasPermission(UserRole::kViewer, "POST",
                                             "/api/channels"));
  EXPECT_FALSE(AuthMiddleware::HasPermission(UserRole::kViewer, "DELETE",
                                             "/api/channels/1"));
  EXPECT_FALSE(
      AuthMiddleware::HasPermission(UserRole::kViewer, "GET", "/api/users"));
}

TEST_F(AuthMiddlewareTest, OperatorCanManageChannels) {
  EXPECT_TRUE(AuthMiddleware::HasPermission(UserRole::kOperator, "GET",
                                            "/api/channels"));
  EXPECT_TRUE(AuthMiddleware::HasPermission(UserRole::kOperator, "POST",
                                            "/api/channels"));
  EXPECT_TRUE(AuthMiddleware::HasPermission(UserRole::kOperator, "DELETE",
                                            "/api/channels/1"));
  EXPECT_FALSE(
      AuthMiddleware::HasPermission(UserRole::kOperator, "GET", "/api/users"));
}

}  // namespace
}  // namespace system
}  // namespace loong
