// Copyright 2026 Loong AI NVR Project

#include "network/rtsp_server/rtsp_server.h"

#include "spdlog/spdlog.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>
#include <poll.h>
#include <random>
#include <sstream>
#include <utility>

namespace loong::network {

RtspServer::RtspServer(std::string  host, int port)
    : host_(std::move(host)), port_(port) {}

RtspServer::~RtspServer() {
  Stop();
}

bool RtspServer::Start() {
  if (running_) {
    spdlog::warn("RtspServer: already running, ignoring Start()");
    return true;
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    spdlog::error("RtspServer: socket() failed");
    return false;
  }

  int opt = 1;
  setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port_));
  inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

  if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
           sizeof(addr)) < 0) {
    spdlog::error("RtspServer: bind() failed on {}:{}", host_, port_);
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, 64) < 0) {
    spdlog::error("RtspServer: listen() failed");
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&RtspServer::AcceptLoop, this);
  return true;
}

void RtspServer::Stop() {
  if (!running_) return;
  running_ = false;

  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& s : sessions_) {
    s->active = false;
    if (s->fd >= 0) {
      shutdown(s->fd, SHUT_RDWR);
      close(s->fd);
      s->fd = -1;
    }
  }
  sessions_.clear();

  spdlog::info("RtspServer: stopped");
}

void RtspServer::RegisterChannel(int channel_id, CodecType codec,
                                 int width, int height, int fps) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  ChannelInfo info;
  info.codec = codec;
  info.width = width;
  info.height = height;
  info.fps = fps;
  channels_[channel_id] = info;
  spdlog::info("RtspServer: registered channel {} ({}x{} @ {} fps)",
               channel_id, width, height, fps);
}

void RtspServer::UnregisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  channels_.erase(channel_id);
}

void RtspServer::PushFrame(int channel_id, const uint8_t* data, size_t size,
                           int64_t pts, bool is_keyframe) {
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  for (auto& session : sessions_) {
    if (session->channel_id != channel_id || !session->playing ||
        !session->active) {
      continue;
    }

    if (!session->got_keyframe) {
      if (!is_keyframe) continue;
      session->got_keyframe = true;
    }

    session->packetizer->Packetize(
        data, size, pts,
        [&session](const uint8_t* pkt, size_t pkt_size) {
          std::lock_guard<std::mutex> wlock(session->write_mutex);
          if (session->fd >= 0 && session->active) {
            send(session->fd, pkt, pkt_size, MSG_NOSIGNAL);
          }
        });
  }
}

// ============================================================
// Private
// ============================================================

void RtspServer::AcceptLoop() {
  while (running_) {
    struct pollfd pfd {};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 500);
    if (ret <= 0) continue;

    struct sockaddr_in client_addr {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd_,
                           reinterpret_cast<struct sockaddr*>(&client_addr),
                           &addr_len);
    if (client_fd < 0) continue;

    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    auto session = std::make_shared<RtspSession>();
    session->fd = client_fd;
    session->active = true;

    {
      std::lock_guard<std::mutex> lock(sessions_mutex_);
      sessions_.push_back(session);
    }

    std::thread(&RtspServer::HandleClient, this, session).detach();
  }
}

void RtspServer::HandleClient(std::shared_ptr<RtspSession> session) {
  char buf[4096];

  while (running_ && session->active) {
    struct pollfd pfd {};
    pfd.fd = session->fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 1000);
    if (ret < 0) break;
    if (ret == 0) continue;

    ssize_t n = recv(session->fd, buf, sizeof(buf) - 1, 0);
    if (n <= 0) break;
    buf[n] = '\0';

    std::string request(buf, static_cast<size_t>(n));

    // Parse RTSP method, URL, and CSeq.
    std::istringstream iss(request);
    std::string method;
    std::string url;
    std::string version;
    iss >> method >> url >> version;

    std::string cseq = "0";
    std::string transport;
    std::string line;
    while (std::getline(iss, line)) {
      if (line.rfind("CSeq:", 0) == 0 || line.rfind("CSeq: ", 0) == 0) {
        auto pos = line.find(':');
        if (pos != std::string::npos) {
          cseq = line.substr(pos + 1);
          // Trim whitespace
          auto start = cseq.find_first_not_of(" \t\r\n");
          if (start != std::string::npos) cseq = cseq.substr(start);
          auto end = cseq.find_last_not_of(" \t\r\n");
          if (end != std::string::npos) cseq = cseq.substr(0, end + 1);
        }
      }
      if (line.rfind("Transport:", 0) == 0) {
        auto pos = line.find(':');
        if (pos != std::string::npos) {
          transport = line.substr(pos + 1);
          auto start = transport.find_first_not_of(" \t\r\n");
          if (start != std::string::npos) transport = transport.substr(start);
        }
      }
    }

    if (method == "OPTIONS") {
      HandleOptions(session->fd, cseq);
    } else if (method == "DESCRIBE") {
      HandleDescribe(session->fd, cseq, url);
    } else if (method == "SETUP") {
      HandleSetup(session, cseq, url, transport);
    } else if (method == "PLAY") {
      HandlePlay(session, cseq);
    } else if (method == "TEARDOWN") {
      HandleTeardown(session, cseq);
      break;
    }
  }

  session->active = false;
  session->playing = false;
  if (session->fd >= 0) {
    close(session->fd);
    session->fd = -1;
  }

  // Remove from sessions list.
  std::lock_guard<std::mutex> lock(sessions_mutex_);
  sessions_.erase(std::remove(sessions_.begin(), sessions_.end(), session),
                  sessions_.end());
}

void RtspServer::HandleOptions(int fd, const std::string& cseq) {
  std::ostringstream ss;
  ss << "RTSP/1.0 200 OK\r\n"
     << "CSeq: " << cseq << "\r\n"
     << "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN\r\n"
     << "\r\n";
  SendResponse(fd, ss.str());
}

void RtspServer::HandleDescribe(int fd, const std::string& cseq,
                                const std::string& url) {
  int channel_id = ParseChannelId(url);
  std::string sdp = BuildSdp(channel_id);

  if (sdp.empty()) {
    std::ostringstream ss;
    ss << "RTSP/1.0 404 Not Found\r\n"
       << "CSeq: " << cseq << "\r\n"
       << "\r\n";
    SendResponse(fd, ss.str());
    return;
  }

  std::ostringstream ss;
  ss << "RTSP/1.0 200 OK\r\n"
     << "CSeq: " << cseq << "\r\n"
     << "Content-Type: application/sdp\r\n"
     << "Content-Length: " << sdp.size() << "\r\n"
     << "\r\n"
     << sdp;
  SendResponse(fd, ss.str());
}

void RtspServer::HandleSetup(std::shared_ptr<RtspSession>& session,
                             const std::string& cseq,
                             const std::string& url,
                             const std::string& /*transport*/) {
  int channel_id = ParseChannelId(url);

  {
    std::lock_guard<std::mutex> lock(channels_mutex_);
    auto it = channels_.find(channel_id);
    if (it == channels_.end()) {
      std::ostringstream ss;
      ss << "RTSP/1.0 404 Not Found\r\n"
         << "CSeq: " << cseq << "\r\n"
         << "\r\n";
      SendResponse(session->fd, ss.str());
      return;
    }

    session->channel_id = channel_id;
    session->session_id = GenerateSessionId();
    session->packetizer =
        std::make_unique<RtpPacketizer>(it->second.codec);
    session->packetizer->SetInterleavedChannel(0);
  }

  std::ostringstream ss;
  ss << "RTSP/1.0 200 OK\r\n"
     << "CSeq: " << cseq << "\r\n"
     << "Session: " << session->session_id << "\r\n"
     << "Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n"
     << "\r\n";
  SendResponse(session->fd, ss.str());
}

void RtspServer::HandlePlay(std::shared_ptr<RtspSession>& session,
                            const std::string& cseq) {
  session->playing = true;
  session->got_keyframe = false;

  std::ostringstream ss;
  ss << "RTSP/1.0 200 OK\r\n"
     << "CSeq: " << cseq << "\r\n"
     << "Session: " << session->session_id << "\r\n"
     << "Range: npt=0.000-\r\n"
     << "\r\n";
  SendResponse(session->fd, ss.str());

  spdlog::info("RTSP: PLAY ch{} session={}", session->channel_id,
               session->session_id);
}

void RtspServer::HandleTeardown(std::shared_ptr<RtspSession>& session,
                                const std::string& cseq) {
  session->playing = false;
  session->active = false;

  std::ostringstream ss;
  ss << "RTSP/1.0 200 OK\r\n"
     << "CSeq: " << cseq << "\r\n"
     << "Session: " << session->session_id << "\r\n"
     << "\r\n";
  SendResponse(session->fd, ss.str());
}

void RtspServer::SendResponse(int fd, const std::string& response) {
  send(fd, response.c_str(), response.size(), MSG_NOSIGNAL);
}

int RtspServer::ParseChannelId(const std::string& url) const {
  // Expected: rtsp://host:port/live/ch{id} or .../live/ch{id}/...
  auto pos = url.find("/live/ch");
  if (pos == std::string::npos) return -1;
  pos += 8;  // skip "/live/ch"

  std::string num_str;
  while (pos < url.size() && url[pos] >= '0' && url[pos] <= '9') {
    num_str.push_back(url[pos]);
    ++pos;
  }

  if (num_str.empty()) return -1;
  try {
    return std::stoi(num_str);
  } catch (...) {
    return -1;
  }
}

std::string RtspServer::GenerateSessionId() const {
  static std::mt19937 rng(std::random_device{}());
  static std::uniform_int_distribution<uint32_t> dist;
  std::ostringstream ss;
  ss << std::hex << dist(rng) << dist(rng);
  return ss.str();
}

std::string RtspServer::BuildSdp(int channel_id) const {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return "";

  const auto& info = it->second;
  std::string codec_name = (info.codec == CodecType::kH265) ? "H265" : "H264";
  int payload_type = 96;

  std::ostringstream ss;
  ss << "v=0\r\n"
     << "o=- 0 0 IN IP4 " << host_ << "\r\n"
     << "s=Loong AI NVR Channel " << channel_id << "\r\n"
     << "c=IN IP4 0.0.0.0\r\n"
     << "t=0 0\r\n"
     << "m=video 0 RTP/AVP " << payload_type << "\r\n"
     << "a=rtpmap:" << payload_type << " " << codec_name << "/90000\r\n"
     << "a=framerate:" << info.fps << "\r\n"
     << "a=control:trackID=0\r\n";

  return ss.str();
}

}  // namespace loong::network
