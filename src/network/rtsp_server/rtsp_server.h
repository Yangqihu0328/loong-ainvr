// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_RTSP_SERVER_RTSP_SERVER_H_
#define LOONG_NETWORK_RTSP_SERVER_RTSP_SERVER_H_

#include "core/common/types.h"
#include "network/rtsp_server/rtp_packetizer.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace loong::network {

/// Minimal RTSP server for distributing AI-overlaid video streams.
///
/// Supports:
///   - RTSP methods: OPTIONS, DESCRIBE, SETUP (TCP interleaved), PLAY, TEARDOWN
///   - URL format:  rtsp://host:port/live/ch{id}
///   - H.264 and H.265 streams
///   - Multiple concurrent clients per channel
class RtspServer {
 public:
  RtspServer(std::string host, int port);
  ~RtspServer();

  /// Start the RTSP server.
  bool Start();

  /// Stop the server and disconnect all clients.
  void Stop();

  /// Register a video channel for streaming.
  void RegisterChannel(int channel_id, CodecType codec, int width, int height,
                       int fps);

  /// Unregister a channel.
  void UnregisterChannel(int channel_id);

  /// Push an encoded frame to all clients watching this channel.
  void PushFrame(int channel_id, const uint8_t* data, size_t size, int64_t pts,
                 bool is_keyframe);

  // Non-copyable
  RtspServer(const RtspServer&) = delete;
  RtspServer& operator=(const RtspServer&) = delete;

 private:
  /// Per-channel metadata.
  struct ChannelInfo {
    CodecType codec = CodecType::kH264;
    int width = 1920;
    int height = 1080;
    int fps = 25;
    std::vector<uint8_t> sps;
    std::vector<uint8_t> pps;
  };

  /// Per-client session.
  struct RtspSession {
    int fd = -1;
    int channel_id = -1;
    std::string session_id;
    bool playing = false;
    std::atomic<bool> active{true};
    std::mutex write_mutex;
    std::unique_ptr<RtpPacketizer> packetizer;
    bool got_keyframe = false;
  };

  void AcceptLoop();
  void HandleClient(std::shared_ptr<RtspSession> session);

  void HandleOptions(int fd, const std::string& cseq);
  void HandleDescribe(int fd, const std::string& cseq, const std::string& url);
  void HandleSetup(std::shared_ptr<RtspSession>& session,
                   const std::string& cseq, const std::string& url,
                   const std::string& transport);
  void HandlePlay(std::shared_ptr<RtspSession>& session,
                  const std::string& cseq);
  void HandleTeardown(std::shared_ptr<RtspSession>& session,
                      const std::string& cseq);

  void SendResponse(int fd, const std::string& response);
  int ParseChannelId(const std::string& url) const;
  std::string GenerateSessionId() const;
  std::string BuildSdp(int channel_id) const;

  std::string host_;
  int port_;
  int listen_fd_ = -1;

  std::atomic<bool> running_{false};
  std::thread accept_thread_;

  mutable std::mutex channels_mutex_;
  std::unordered_map<int, ChannelInfo> channels_;

  mutable std::mutex sessions_mutex_;
  std::vector<std::shared_ptr<RtspSession>> sessions_;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_RTSP_SERVER_RTSP_SERVER_H_
