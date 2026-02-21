// Copyright 2026 Loong AI NVR Project

#include "network/webrtc/webrtc_service.h"

#include "network/webrtc/loongrtc_include.h"

#include <chrono>
#include <random>
#include <spdlog/spdlog.h>

namespace loong::network {

namespace {

inline PRtcPeerConnection AsPc(void* p) {
  return static_cast<PRtcPeerConnection>(p);
}
inline PRtcRtpTransceiver AsVt(void* p) {
  return static_cast<PRtcRtpTransceiver>(p);
}

}  // namespace

static int64_t NowMsStatic() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// ========== C callback trampolines ==========

struct SessionCallbackData {
  WebRtcService* service;
  std::string session_id;
};

static VOID IceCandidateTrampoline(UINT64 custom_data, PCHAR candidate_str) {
  auto* cbd = reinterpret_cast<SessionCallbackData*>(custom_data);
  if (cbd && cbd->service) {
    cbd->service->HandleIceCandidateInternal(
        cbd->session_id, candidate_str ? candidate_str : "");
  }
}

static VOID ConnectionStateTrampoline(UINT64 custom_data,
                                      RTC_PEER_CONNECTION_STATE new_state) {
  auto* cbd = reinterpret_cast<SessionCallbackData*>(custom_data);
  if (cbd && cbd->service) {
    cbd->service->OnConnectionStateChangeInternal(cbd->session_id, new_state);
  }
}

// ========== WebRtcService ==========

WebRtcService::WebRtcService() = default;

WebRtcService::~WebRtcService() { Shutdown(); }

bool WebRtcService::Initialize(const WebRtcConfig& config) {
  if (initialized_) return true;
  config_ = config;

  STATUS status = initLoongRtc();
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: initLoongRtc failed (0x{:08x})", status);
    return false;
  }

  initialized_ = true;
  spdlog::info("WebRtcService: initialized (stun={}, max_sessions={})",
               config_.stun_server, config_.max_sessions);
  return true;
}

void WebRtcService::Shutdown() {
  StopCleanupTimer();

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& [id, session] : sessions_) {
    if (session->video_transceiver) {
      auto vt = AsVt(session->video_transceiver);
      freeTransceiver(&vt);
      session->video_transceiver = nullptr;
    }
    if (session->peer_connection) {
      auto pc = AsPc(session->peer_connection);
      freePeerConnection(&pc);
      session->peer_connection = nullptr;
    }
  }
  sessions_.clear();

  if (initialized_) {
    deinitLoongRtc();
    initialized_ = false;
  }

  spdlog::info("WebRtcService: shutdown");
}

std::string WebRtcService::HandleOffer(int channel_id,
                                       const std::string& offer_sdp,
                                       std::string& out_session_id) {
  if (!initialized_) return "";

  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    if (static_cast<int>(sessions_.size()) >= config_.max_sessions) {
      spdlog::warn("WebRtcService: max sessions ({}) reached",
                   config_.max_sessions);
      return "";
    }
  }

  std::string session_id = GenerateSessionId();

  // Configure peer connection
  RtcConfiguration rtc_config;
  MEMSET(&rtc_config, 0, SIZEOF(RtcConfiguration));
  if (!config_.stun_server.empty()) {
    STRNCPY(rtc_config.iceServers[0].urls, config_.stun_server.c_str(),
            MAX_ICE_CONFIG_URI_LEN);
    rtc_config.iceServers[0].urls[MAX_ICE_CONFIG_URI_LEN] = '\0';
  }
  rtc_config.iceTransportPolicy = ICE_TRANSPORT_POLICY_ALL;

  // Create peer connection
  PRtcPeerConnection pc = nullptr;
  STATUS status = createPeerConnection(&rtc_config, &pc);
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: createPeerConnection failed (0x{:08x})",
                  status);
    return "";
  }

  // Create callback data (leaked intentionally, cleaned in RemoveSession)
  auto* cbd = new SessionCallbackData{this, session_id};

  // Set callbacks
  peerConnectionOnIceCandidate(pc, reinterpret_cast<UINT64>(cbd),
                               IceCandidateTrampoline);
  peerConnectionOnConnectionStateChange(pc, reinterpret_cast<UINT64>(cbd),
                                        ConnectionStateTrampoline);

  // Add H.264 video transceiver (sendonly)
  RtcMediaStreamTrack video_track;
  MEMSET(&video_track, 0, SIZEOF(RtcMediaStreamTrack));
  video_track.kind = MEDIA_STREAM_TRACK_KIND_VIDEO;
  video_track.codec =
      RTC_CODEC_H264_PROFILE_42E01F_LEVEL_ASYMMETRY_ALLOWED_PACKETIZATION_MODE;
  STRCPY(video_track.streamId, "loong-nvr");
  std::string track_id = "video-ch" + std::to_string(channel_id);
  STRNCPY(video_track.trackId, track_id.c_str(), MAX_MEDIA_STREAM_TRACK_ID_LEN);

  RtcRtpTransceiverInit transceiver_init;
  MEMSET(&transceiver_init, 0, SIZEOF(RtcRtpTransceiverInit));
  transceiver_init.direction = RTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY;

  PRtcRtpTransceiver video_transceiver = nullptr;
  status =
      addTransceiver(pc, &video_track, &transceiver_init, &video_transceiver);
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: addTransceiver failed (0x{:08x})", status);
    freePeerConnection(&pc);
    delete cbd;
    return "";
  }

  // Set remote description (client's offer)
  RtcSessionDescriptionInit offer_desc;
  MEMSET(&offer_desc, 0, SIZEOF(RtcSessionDescriptionInit));
  offer_desc.type = SDP_TYPE_OFFER;
  STRNCPY(offer_desc.sdp, offer_sdp.c_str(),
          MAX_SESSION_DESCRIPTION_INIT_SDP_LEN);

  status = setRemoteDescription(pc, &offer_desc);
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: setRemoteDescription failed (0x{:08x})",
                  status);
    freeTransceiver(&video_transceiver);
    freePeerConnection(&pc);
    delete cbd;
    return "";
  }

  // Create answer
  RtcSessionDescriptionInit answer_desc;
  MEMSET(&answer_desc, 0, SIZEOF(RtcSessionDescriptionInit));
  status = createAnswer(pc, &answer_desc);
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: createAnswer failed (0x{:08x})", status);
    freeTransceiver(&video_transceiver);
    freePeerConnection(&pc);
    delete cbd;
    return "";
  }

  // Set local description
  status = setLocalDescription(pc, &answer_desc);
  if (STATUS_FAILED(status)) {
    spdlog::error("WebRtcService: setLocalDescription failed (0x{:08x})",
                  status);
    freeTransceiver(&video_transceiver);
    freePeerConnection(&pc);
    delete cbd;
    return "";
  }

  // Store session
  auto session = std::make_shared<WebRtcSession>();
  session->session_id = session_id;
  session->channel_id = channel_id;
  session->state = WebRtcSessionState::kConnecting;
  session->peer_connection = pc;
  session->video_transceiver = video_transceiver;
  session->created_at = NowMs();
  session->last_activity = session->created_at;
  session->pending_answer_sdp = answer_desc.sdp;
  session->callback_data = cbd;

  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    sessions_[session_id] = session;
  }

  out_session_id = session_id;
  spdlog::info("WebRtcService: session {} created for channel {}", session_id,
               channel_id);
  return std::string(answer_desc.sdp);
}

bool WebRtcService::HandleIceCandidate(const std::string& session_id,
                                       const std::string& candidate) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  auto& session = it->second;
  session->last_activity = NowMs();

  STATUS status = addIceCandidate(AsPc(session->peer_connection),
                                  const_cast<char*>(candidate.c_str()));
  if (STATUS_FAILED(status)) {
    spdlog::warn("WebRtcService: addIceCandidate failed for {} (0x{:08x})",
                 session_id, status);
    return false;
  }
  return true;
}

bool WebRtcService::CloseSession(const std::string& session_id) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return false;

  auto& session = it->second;

  if (session->video_transceiver) {
    auto vt = AsVt(session->video_transceiver);
    freeTransceiver(&vt);
    session->video_transceiver = nullptr;
  }
  if (session->peer_connection) {
    auto pc = AsPc(session->peer_connection);
    freePeerConnection(&pc);
    session->peer_connection = nullptr;
  }
  delete static_cast<SessionCallbackData*>(session->callback_data);
  sessions_.erase(it);

  spdlog::info("WebRtcService: session {} closed", session_id);
  return true;
}

void WebRtcService::WriteVideoFrame(int channel_id, const uint8_t* data,
                                    size_t size, int64_t pts,
                                    bool is_keyframe) {
  if (!initialized_) return;

  Frame frame;
  MEMSET(&frame, 0, SIZEOF(Frame));
  frame.version = FRAME_CURRENT_VERSION;
  frame.frameData = const_cast<uint8_t*>(data);
  frame.size = static_cast<UINT32>(size);
  frame.presentationTs = static_cast<UINT64>(pts);  // already in microseconds
  if (is_keyframe) {
    frame.flags = FRAME_FLAG_KEY_FRAME;
  }

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& [id, session] : sessions_) {
    if (session->channel_id == channel_id &&
        session->state == WebRtcSessionState::kConnected &&
        session->video_transceiver != nullptr) {
      STATUS status = writeFrame(AsVt(session->video_transceiver), &frame);
      if (STATUS_FAILED(status)) {
        spdlog::trace("WebRtcService: writeFrame failed for {} (0x{:08x})", id,
                      status);
      }
    }
  }
}

int WebRtcService::SessionCount() const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  return static_cast<int>(sessions_.size());
}

int WebRtcService::SessionCountForChannel(int channel_id) const {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  int count = 0;
  for (const auto& [_, session] : sessions_) {
    if (session->channel_id == channel_id) count++;
  }
  return count;
}

bool WebRtcService::HasViewers(int channel_id) const {
  return SessionCountForChannel(channel_id) > 0;
}

void WebRtcService::StartCleanupTimer() {
  if (cleanup_running_.load()) return;
  cleanup_running_.store(true);
  cleanup_thread_ = std::thread(&WebRtcService::CleanupLoop, this);
}

void WebRtcService::StopCleanupTimer() {
  if (!cleanup_running_.load()) return;
  cleanup_running_.store(false);
  if (cleanup_thread_.joinable()) {
    cleanup_thread_.join();
  }
}

std::string WebRtcService::GenerateSessionId() {
  static std::mt19937 rng(std::random_device{}());
  static const char chars[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string id;
  id.reserve(12);
  for (int i = 0; i < 12; ++i) {
    id += chars[rng() % (sizeof(chars) - 1)];
  }
  return id;
}

int64_t WebRtcService::NowMs() { return NowMsStatic(); }

void WebRtcService::HandleIceCandidateInternal(const std::string& session_id,
                                               const std::string& candidate) {
  if (candidate.empty()) {
    spdlog::debug("WebRtcService: ICE gathering complete for {}", session_id);
    return;
  }

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it != sessions_.end()) {
    it->second->pending_ice_candidates.push_back(candidate);
  }
}

void WebRtcService::OnConnectionStateChangeInternal(
    const std::string& session_id, uint32_t new_state) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  auto it = sessions_.find(session_id);
  if (it == sessions_.end()) return;

  auto& session = it->second;
  switch (new_state) {
    case RTC_PEER_CONNECTION_STATE_CONNECTED:
      session->state = WebRtcSessionState::kConnected;
      spdlog::info("WebRtcService: session {} CONNECTED", session_id);
      break;
    case RTC_PEER_CONNECTION_STATE_DISCONNECTED:
      session->state = WebRtcSessionState::kDisconnected;
      spdlog::info("WebRtcService: session {} DISCONNECTED", session_id);
      break;
    case RTC_PEER_CONNECTION_STATE_FAILED:
      session->state = WebRtcSessionState::kFailed;
      spdlog::warn("WebRtcService: session {} FAILED", session_id);
      break;
    case RTC_PEER_CONNECTION_STATE_CLOSED:
      session->state = WebRtcSessionState::kClosed;
      break;
    default:
      break;
  }
  session->last_activity = NowMs();
}

void WebRtcService::CleanupLoop() {
  while (cleanup_running_.load()) {
    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      int64_t now = NowMs();
      int64_t timeout_ms =
          static_cast<int64_t>(config_.ice_timeout_sec) * 1000 * 6;

      std::vector<std::string> to_remove;
      for (const auto& [id, session] : sessions_) {
        bool stale = (now - session->last_activity) > timeout_ms;
        bool failed = session->state == WebRtcSessionState::kFailed ||
                      session->state == WebRtcSessionState::kClosed;
        if (stale || failed) {
          to_remove.push_back(id);
        }
      }

      for (const auto& id : to_remove) {
        auto it = sessions_.find(id);
        if (it != sessions_.end()) {
          auto& s = it->second;
          if (s->video_transceiver) {
            auto vt = AsVt(s->video_transceiver);
            freeTransceiver(&vt);
            s->video_transceiver = nullptr;
          }
          if (s->peer_connection) {
            auto pc = AsPc(s->peer_connection);
            freePeerConnection(&pc);
            s->peer_connection = nullptr;
          }
          delete static_cast<SessionCallbackData*>(s->callback_data);
          sessions_.erase(it);
          spdlog::info("WebRtcService: cleaned up stale session {}", id);
        }
      }
    }

    for (int i = 0; i < 10 && cleanup_running_.load(); ++i) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void WebRtcService::CloseSessionsForChannel(int channel_id) {
  std::vector<std::string> to_close;
  {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (const auto& [id, session] : sessions_) {
      if (session->channel_id == channel_id) {
        to_close.push_back(id);
      }
    }
  }

  for (const auto& id : to_close) {
    CloseSession(id);
  }

  if (!to_close.empty()) {
    spdlog::info("WebRtcService: closed {} sessions for channel {}",
                 to_close.size(), channel_id);
  }
}

}  // namespace loong::network
