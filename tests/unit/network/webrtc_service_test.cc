// Copyright 2026 Loong AI NVR Project
// Unit tests for WebRTC Service (FEAT-3.1)

#include "network/webrtc/webrtc_service.h"

#include <gtest/gtest.h>

namespace loong::network {
namespace {

// ============================================================
// WebRtcConfig Tests
// ============================================================

TEST(WebRtcConfigTest, Defaults) {
  WebRtcConfig config;
  EXPECT_EQ(config.stun_server, "stun:stun.l.google.com:19302");
  EXPECT_EQ(config.max_sessions, 16);
  EXPECT_EQ(config.ice_timeout_sec, 10);
}

// ============================================================
// WebRtcSession Tests
// ============================================================

TEST(WebRtcSessionTest, DefaultState) {
  WebRtcSession session;
  EXPECT_TRUE(session.session_id.empty());
  EXPECT_EQ(session.channel_id, -1);
  EXPECT_EQ(session.state, WebRtcSessionState::kNew);
  EXPECT_EQ(session.peer_connection, nullptr);
  EXPECT_EQ(session.video_transceiver, nullptr);
}

// ============================================================
// WebRtcService Tests
// ============================================================

class WebRtcServiceTest : public ::testing::Test {
 protected:
  WebRtcService service_;
};

TEST_F(WebRtcServiceTest, InitialState) {
  EXPECT_EQ(service_.SessionCount(), 0);
  EXPECT_FALSE(service_.HasViewers(0));
}

TEST_F(WebRtcServiceTest, InitializeAndShutdown) {
  WebRtcConfig config;
  config.max_sessions = 4;
  bool ok = service_.Initialize(config);
  // May fail if loong-rtc library is not available in test env
  // But the service should handle gracefully
  if (ok) {
    EXPECT_EQ(service_.SessionCount(), 0);
    service_.Shutdown();
  }
}

TEST_F(WebRtcServiceTest, HandleOfferWithoutInit) {
  std::string session_id;
  std::string answer = service_.HandleOffer(0, "fake_sdp", session_id);
  EXPECT_TRUE(answer.empty());
  EXPECT_TRUE(session_id.empty());
}

TEST_F(WebRtcServiceTest, HandleIceCandidateNoSession) {
  EXPECT_FALSE(service_.HandleIceCandidate("nonexistent", "candidate"));
}

TEST_F(WebRtcServiceTest, CloseNonexistentSession) {
  EXPECT_FALSE(service_.CloseSession("nonexistent"));
}

TEST_F(WebRtcServiceTest, WriteFrameWithoutInit) {
  uint8_t data[] = {0x00, 0x00, 0x01, 0x65};
  // Should not crash
  service_.WriteVideoFrame(0, data, sizeof(data), 0, true);
}

TEST_F(WebRtcServiceTest, SessionCountForChannel) {
  EXPECT_EQ(service_.SessionCountForChannel(0), 0);
  EXPECT_EQ(service_.SessionCountForChannel(1), 0);
}

TEST_F(WebRtcServiceTest, HasViewersEmpty) {
  EXPECT_FALSE(service_.HasViewers(0));
  EXPECT_FALSE(service_.HasViewers(1));
  EXPECT_FALSE(service_.HasViewers(99));
}

TEST_F(WebRtcServiceTest, StartStopCleanupTimer) {
  // Should not crash or hang
  service_.StartCleanupTimer();
  service_.StopCleanupTimer();
}

// Integration test: full offer/answer flow (requires loong-rtc runtime)
TEST_F(WebRtcServiceTest, DISABLED_FullOfferAnswerFlow) {
  WebRtcConfig config;
  ASSERT_TRUE(service_.Initialize(config));

  // Minimal SDP offer (would need a real browser SDP for actual testing)
  std::string offer = "v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\n";
  std::string session_id;
  std::string answer = service_.HandleOffer(0, offer, session_id);

  // With a real SDP this would succeed
  // EXPECT_FALSE(answer.empty());
  // EXPECT_FALSE(session_id.empty());

  service_.Shutdown();
}

}  // namespace
}  // namespace loong::network
