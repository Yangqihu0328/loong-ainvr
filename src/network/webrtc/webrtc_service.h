// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_WEBRTC_WEBRTC_SERVICE_H_
#define LOONG_NETWORK_WEBRTC_WEBRTC_SERVICE_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace loong::network {

/// WebRTC session state.
enum class WebRtcSessionState {
  kNew,
  kConnecting,
  kConnected,
  kDisconnected,
  kFailed,
  kClosed,
};

/// Configuration for the WebRTC service.
struct WebRtcConfig {
  std::string stun_server = "stun:stun.l.google.com:19302";
  int max_sessions = 16;
  int ice_timeout_sec = 10;
};

/// Represents a single WebRTC streaming session for one viewer.
struct WebRtcSession {
  std::string session_id;
  int channel_id = -1;
  WebRtcSessionState state = WebRtcSessionState::kNew;

  void* peer_connection = nullptr;     // PRtcPeerConnection (loong-rtc)
  void* video_transceiver = nullptr;   // PRtcRtpTransceiver (loong-rtc)

  int64_t created_at = 0;
  int64_t last_activity = 0;

  std::string pending_answer_sdp;
  std::vector<std::string> pending_ice_candidates;

  // Opaque callback context (owned by WebRtcService)
  void* callback_data = nullptr;
};

/// Manages WebRTC peer connections for live streaming.
///
/// Integrates with the loong-rtc library to create WebRTC sessions
/// that stream H.264 video from NVR channels to browser viewers.
///
/// Workflow:
///   1. Browser sends SDP Offer → POST /api/webrtc/offer
///   2. Server creates PeerConnection, adds H.264 track, creates Answer
///   3. Server returns SDP Answer
///   4. ICE candidates are exchanged via POST /api/webrtc/ice
///   5. Once connected, H.264 frames are pushed via WriteVideoFrame()
class WebRtcService {
 public:
  WebRtcService();
  ~WebRtcService();

  /// Initialize the service with configuration.
  bool Initialize(const WebRtcConfig& config);

  /// Shutdown and clean up all sessions.
  void Shutdown();

  /// Create a new session for a channel, process the client's SDP offer,
  /// and return the server's SDP answer.
  /// Returns empty string on failure.
  std::string HandleOffer(int channel_id, const std::string& offer_sdp,
                          std::string& out_session_id);

  /// Add a remote ICE candidate to an existing session.
  bool HandleIceCandidate(const std::string& session_id,
                          const std::string& candidate);

  /// Close and destroy a session.
  bool CloseSession(const std::string& session_id);

  /// Write an H.264 encoded video frame to all active sessions on a channel.
  void WriteVideoFrame(int channel_id, const uint8_t* data, size_t size,
                       int64_t pts, bool is_keyframe);

  /// Get the number of active sessions.
  int SessionCount() const;

  /// Get the number of active sessions for a specific channel.
  int SessionCountForChannel(int channel_id) const;

  /// Check if any viewers are connected to a channel.
  bool HasViewers(int channel_id) const;

  /// Close all sessions for a specific channel (e.g., when the channel stops).
  void CloseSessionsForChannel(int channel_id);

  /// Start periodic cleanup of stale sessions.
  void StartCleanupTimer();

  /// Stop the cleanup timer.
  void StopCleanupTimer();

  // Internal callbacks (called from C trampolines — public for access)
  void HandleIceCandidateInternal(const std::string& session_id,
                                  const std::string& candidate);
  void OnConnectionStateChangeInternal(const std::string& session_id,
                                       uint32_t new_state);

  // Non-copyable
  WebRtcService(const WebRtcService&) = delete;
  WebRtcService& operator=(const WebRtcService&) = delete;

 private:
  static std::string GenerateSessionId();
  static int64_t NowMs();

  void OnIceCandidate(const std::string& session_id, const char* candidate);
  void OnConnectionStateChange(const std::string& session_id,
                               uint32_t new_state);
  void CleanupLoop();
  void RemoveSession(const std::string& session_id);

  WebRtcConfig config_;
  bool initialized_ = false;

  mutable std::mutex sessions_mutex_;
  std::unordered_map<std::string, std::shared_ptr<WebRtcSession>> sessions_;

  std::atomic<bool> cleanup_running_{false};
  std::thread cleanup_thread_;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_WEBRTC_WEBRTC_SERVICE_H_
