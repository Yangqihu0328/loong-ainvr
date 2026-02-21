// Copyright 2026 Loong AI NVR Project

#include "network/media_stream/ws_media_service.h"

#include "network/media_stream/fmp4_muxer.h"
#include "network/websocket/websocket_frame.h"
#include "spdlog/spdlog.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <netinet/tcp.h>
#include <poll.h>
#include <utility>

namespace loong::network {

WsMediaService::WsMediaService(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

WsMediaService::~WsMediaService() { Stop(); }

bool WsMediaService::Start() {
  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    spdlog::error("WsMediaService: socket() failed");
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
    spdlog::error("WsMediaService: bind() failed on {}:{}", host_, port_);
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, 128) < 0) {
    spdlog::error("WsMediaService: listen() failed");
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&WsMediaService::AcceptLoop, this);
  spdlog::info("WsMediaService: listening on ws://{}:{}", host_, port_);
  return true;
}

void WsMediaService::Stop() {
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

  // Disconnect all viewers
  std::lock_guard<std::mutex> lock(channels_mutex_);
  for (auto& [id, stream] : channels_) {
    std::lock_guard<std::mutex> slock(stream->mtx);
    stream->active = false;
    for (auto& weak_viewer : stream->viewers) {
      auto viewer = weak_viewer.lock();
      if (viewer) {
        viewer->active = false;
        if (viewer->fd >= 0) {
          shutdown(viewer->fd, SHUT_RDWR);
          close(viewer->fd);
          viewer->fd = -1;
        }
        std::lock_guard<std::mutex> vlock(viewer->mtx);
        viewer->cv.notify_all();
      }
    }
    stream->viewers.clear();
  }

  spdlog::info("WsMediaService: stopped");
}

void WsMediaService::RegisterChannel(int channel_id, CodecType codec,
                                     uint16_t width, uint16_t height,
                                     int framerate) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto stream = std::make_unique<ChannelStream>();
  stream->codec = codec;
  stream->width = width;
  stream->height = height;
  stream->framerate = (framerate > 0) ? static_cast<uint32_t>(framerate) : 30u;
  uint32_t fps = stream->framerate;
  channels_[channel_id] = std::move(stream);
  spdlog::info("WsMediaService: registered channel {} ({}x{}, {} @{}fps)",
               channel_id, width, height,
               codec == CodecType::kH265 ? "H.265" : "H.264", fps);
}

void WsMediaService::UnregisterChannel(int channel_id) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  // Mark inactive and notify viewers while stream is still alive.
  // The inner scope ensures the stream lock is released before erase
  // destroys the ChannelStream (and its mutex).
  {
    auto& stream = it->second;
    std::lock_guard<std::mutex> slock(stream->mtx);
    stream->active = false;

    for (auto& weak_viewer : stream->viewers) {
      auto viewer = weak_viewer.lock();
      if (viewer) {
        viewer->active = false;
        if (viewer->fd >= 0) {
          shutdown(viewer->fd, SHUT_RDWR);
        }
        std::lock_guard<std::mutex> vlock(viewer->mtx);
        viewer->cv.notify_all();
      }
    }
  }

  channels_.erase(it);
  spdlog::info("WsMediaService: unregistered channel {}", channel_id);
}

void WsMediaService::PushFrame(int channel_id, const uint8_t* data, size_t size,
                               int64_t pts, bool is_keyframe) {
  if (!running_) return;

  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);
  if (!stream->active) return;

  // Extract and update parameter sets from keyframes
  if (is_keyframe) {
    bool params_changed = false;

    if (stream->codec == CodecType::kH264) {
      std::vector<uint8_t> sps;
      std::vector<uint8_t> pps;
      if (Fmp4Muxer::ExtractH264Params(data, size, sps, pps)) {
        if (sps != stream->h264_sps || pps != stream->h264_pps) {
          stream->h264_sps = std::move(sps);
          stream->h264_pps = std::move(pps);
          params_changed = true;
        }
      }
    } else if (stream->codec == CodecType::kH265) {
      std::vector<uint8_t> vps;
      std::vector<uint8_t> sps;
      std::vector<uint8_t> pps;
      if (Fmp4Muxer::ExtractH265Params(data, size, vps, sps, pps)) {
        if (vps != stream->h265_vps || sps != stream->h265_sps ||
            pps != stream->h265_pps) {
          stream->h265_vps = std::move(vps);
          stream->h265_sps = std::move(sps);
          stream->h265_pps = std::move(pps);
          params_changed = true;
        }
      }
    }

    // Regenerate init segment when parameter sets change
    if (params_changed) {
      if (stream->codec == CodecType::kH264) {
        stream->init_segment = Fmp4Muxer::MakeH264InitSegment(
            stream->h264_sps, stream->h264_pps, stream->width, stream->height);
      } else if (stream->codec == CodecType::kH265) {
        stream->init_segment = Fmp4Muxer::MakeH265InitSegment(
            stream->h265_vps, stream->h265_sps, stream->h265_pps, stream->width,
            stream->height);
      }
      spdlog::debug("WsMediaService: ch{} regenerated init segment ({} bytes)",
                    channel_id, stream->init_segment.size());
    }
  }

  // Cannot mux without init segment
  if (stream->init_segment.empty()) return;

  // Compute timestamps in 90kHz timescale
  if (stream->base_pts < 0) {
    stream->base_pts = pts;
  }
  int64_t relative_us = pts - stream->base_pts;
  if (relative_us < 0) relative_us = 0;

  // Convert microseconds to 90kHz ticks
  auto decode_time =
      static_cast<uint64_t>(relative_us * Fmp4Muxer::kTimescale / 1000000);

  uint32_t duration = Fmp4Muxer::kTimescale / stream->framerate;

  auto segment = Fmp4Muxer::MakeMediaSegment(data, size, decode_time, duration,
                                             stream->sequence_number++,
                                             is_keyframe, stream->codec);
  if (segment.empty()) return;

  // Wrap segment in WebSocket binary frame
  auto ws_frame = BuildWsBinaryFrame(segment.data(), segment.size());

  // Also prepare init segment WS frame for new viewers
  // (Cached per-viewer on first send, but prepare the WS frame here for
  //  keyframe-triggered new viewer sends)

  // Broadcast to all viewers
  auto viewer_it = stream->viewers.begin();
  while (viewer_it != stream->viewers.end()) {
    auto viewer = viewer_it->lock();
    if (!viewer) {
      viewer_it = stream->viewers.erase(viewer_it);
      continue;
    }

    std::lock_guard<std::mutex> vlock(viewer->mtx);
    if (!viewer->active) {
      viewer_it = stream->viewers.erase(viewer_it);
      continue;
    }

    // New viewers wait for a keyframe before starting
    if (!viewer->got_keyframe) {
      if (is_keyframe) {
        viewer->got_keyframe = true;
      } else {
        ++viewer_it;
        continue;
      }
    }

    // Send init segment to new viewers who haven't received it
    if (!viewer->got_init_segment && !stream->init_segment.empty()) {
      auto init_ws = BuildWsBinaryFrame(stream->init_segment.data(),
                                        stream->init_segment.size());
      viewer->pending_segments.push_back(std::move(init_ws));
      viewer->got_init_segment = true;
    }

    // Backpressure: drop old segments if queue is full
    if (viewer->pending_segments.size() >= MediaViewer::kMaxPendingSegments) {
      if (!is_keyframe) {
        ++viewer_it;
        continue;
      }
      // For keyframes, drop oldest half to make room
      while (viewer->pending_segments.size() >
             MediaViewer::kMaxPendingSegments / 2) {
        viewer->pending_segments.pop_front();
      }
      // Re-send init segment after dropping
      viewer->got_init_segment = false;
    }

    viewer->pending_segments.push_back(ws_frame);
    viewer->cv.notify_one();
    ++viewer_it;
  }
}

size_t WsMediaService::ViewerCount() const {
  size_t count = 0;
  std::lock_guard<std::mutex> lock(channels_mutex_);
  for (const auto& [id, stream] : channels_) {
    std::lock_guard<std::mutex> slock(stream->mtx);
    for (const auto& wp : stream->viewers) {
      if (auto sp = wp.lock()) {
        if (sp->active) ++count;
      }
    }
  }
  return count;
}

// ============================================================
// WebSocket Server Logic
// ============================================================

void WsMediaService::AcceptLoop() {
  while (running_) {
    struct pollfd pfd {};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 500);
    if (ret <= 0) continue;

    struct sockaddr_in client_addr {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd =
        accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr),
               &addr_len);
    if (client_fd < 0) continue;

    // Disable Nagle for low latency
    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Perform WebSocket handshake and extract channel ID from URL
    int channel_id = -1;
    if (!PerformHandshake(client_fd, channel_id)) {
      close(client_fd);
      continue;
    }

    // Register viewer for the channel
    auto viewer = std::make_shared<MediaViewer>();
    viewer->fd = client_fd;
    viewer->active = true;

    {
      std::lock_guard<std::mutex> lock(channels_mutex_);
      auto it = channels_.find(channel_id);
      if (it == channels_.end()) {
        // Channel not found — close connection
        auto close_frame = WebSocketFrame::BuildCloseFrame(1008);
        send(client_fd, close_frame.data(), close_frame.size(), MSG_NOSIGNAL);
        close(client_fd);
        continue;
      }
      std::lock_guard<std::mutex> slock(it->second->mtx);
      it->second->viewers.push_back(viewer);
      spdlog::info("WsMediaService: new viewer for ch{} (fd={})", channel_id,
                   client_fd);
    }

    // Spawn handler thread for this viewer
    std::thread(&WsMediaService::HandleClient, this, viewer, channel_id)
        .detach();
  }
}

bool WsMediaService::PerformHandshake(int fd, int& channel_id) {
  char buf[4096];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) return false;
  buf[n] = '\0';

  std::string request(buf, static_cast<size_t>(n));

  // Extract the request path (first line: GET /path HTTP/1.1)
  auto first_line_end = request.find("\r\n");
  if (first_line_end == std::string::npos) return false;

  std::string first_line = request.substr(0, first_line_end);
  // Parse: "GET /ws/live/ch{id} HTTP/1.1"
  auto path_start = first_line.find(' ');
  if (path_start == std::string::npos) return false;
  ++path_start;
  auto path_end = first_line.find(' ', path_start);
  if (path_end == std::string::npos) return false;

  std::string path = first_line.substr(path_start, path_end - path_start);
  channel_id = ParseChannelId(path);
  if (channel_id < 0) return false;

  // Extract Sec-WebSocket-Key
  std::string key_header = "Sec-WebSocket-Key: ";
  auto pos = request.find(key_header);
  if (pos == std::string::npos) return false;

  auto start = pos + key_header.size();
  auto end = request.find("\r\n", start);
  if (end == std::string::npos) return false;

  std::string client_key = request.substr(start, end - start);
  std::string accept_key = WebSocketFrame::ComputeAcceptKey(client_key);

  // Build and send upgrade response
  std::string response =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " +
      accept_key + "\r\n\r\n";

  ssize_t sent = send(fd, response.c_str(), response.size(), MSG_NOSIGNAL);
  return sent == static_cast<ssize_t>(response.size());
}

int WsMediaService::ParseChannelId(const std::string& path) {
  // Expected: /ws/live/ch{id} or /live/ch{id}
  auto pos = path.find("/ch");
  if (pos == std::string::npos) return -1;
  pos += 3;  // skip "/ch"

  std::string num_str;
  while (pos < path.size() && path[pos] >= '0' && path[pos] <= '9') {
    num_str.push_back(path[pos]);
    ++pos;
  }

  if (num_str.empty()) return -1;
  try {
    return std::stoi(num_str);
  } catch (...) {
    return -1;
  }
}

void WsMediaService::HandleClient(std::shared_ptr<MediaViewer> viewer,
                                  int channel_id) {
  // This thread reads from the viewer (handling ping/pong and close)
  // and writes pending segments to the WebSocket.
  std::vector<uint8_t> recv_buffer;
  recv_buffer.reserve(4096);
  char recv_buf[4096];

  while (running_ && viewer->active) {
    // Check for incoming WebSocket frames (ping/close)
    struct pollfd pfd {};
    pfd.fd = viewer->fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 0);  // non-blocking check
    if (ret > 0) {
      ssize_t n = recv(viewer->fd, recv_buf, sizeof(recv_buf), 0);
      if (n <= 0) break;

      recv_buffer.insert(recv_buffer.end(), recv_buf, recv_buf + n);

      while (!recv_buffer.empty()) {
        WsFrame frame;
        size_t consumed = WebSocketFrame::ParseFrame(recv_buffer.data(),
                                                     recv_buffer.size(), frame);
        if (consumed == 0) break;

        recv_buffer.erase(
            recv_buffer.begin(),
            recv_buffer.begin() + static_cast<ptrdiff_t>(consumed));

        switch (frame.opcode) {
          case WsOpcode::kPing: {
            auto pong = WebSocketFrame::BuildPongFrame(frame.payload);
            std::lock_guard<std::mutex> wlock(viewer->mtx);
            send(viewer->fd, pong.data(), pong.size(), MSG_NOSIGNAL);
            break;
          }
          case WsOpcode::kClose: {
            auto close_resp = WebSocketFrame::BuildCloseFrame(1000);
            send(viewer->fd, close_resp.data(), close_resp.size(),
                 MSG_NOSIGNAL);
            viewer->active = false;
            break;
          }
          default:
            break;
        }
      }
    }

    if (!viewer->active) break;

    // Send pending segments
    std::unique_lock<std::mutex> vlock(viewer->mtx);
    viewer->cv.wait_for(vlock, std::chrono::milliseconds(30), [&viewer]() {
      return !viewer->pending_segments.empty() || !viewer->active;
    });

    if (!viewer->active) break;

    while (!viewer->pending_segments.empty()) {
      auto& seg = viewer->pending_segments.front();
      ssize_t sent = send(viewer->fd, seg.data(), seg.size(), MSG_NOSIGNAL);
      if (sent <= 0) {
        viewer->active = false;
        break;
      }
      viewer->pending_segments.pop_front();
    }
  }

  // Cleanup
  viewer->active = false;
  if (viewer->fd >= 0) {
    close(viewer->fd);
    viewer->fd = -1;
  }
  RemoveViewer(channel_id, viewer);
  spdlog::info("WsMediaService: viewer disconnected from ch{}", channel_id);
}

void WsMediaService::RemoveViewer(int channel_id,
                                  const std::shared_ptr<MediaViewer>& viewer) {
  std::lock_guard<std::mutex> lock(channels_mutex_);
  auto it = channels_.find(channel_id);
  if (it == channels_.end()) return;

  auto& stream = it->second;
  std::lock_guard<std::mutex> slock(stream->mtx);
  auto& viewers = stream->viewers;
  viewers.erase(std::remove_if(viewers.begin(), viewers.end(),
                               [&viewer](const std::weak_ptr<MediaViewer>& wp) {
                                 auto sp = wp.lock();
                                 return !sp || sp == viewer;
                               }),
                viewers.end());
}

void WsMediaService::SendBinaryFrame(const std::shared_ptr<MediaViewer>& viewer,
                                     const std::vector<uint8_t>& data) {
  if (!viewer->active || viewer->fd < 0) return;
  auto frame = BuildWsBinaryFrame(data.data(), data.size());
  send(viewer->fd, frame.data(), frame.size(), MSG_NOSIGNAL);
}

std::vector<uint8_t> WsMediaService::BuildWsBinaryFrame(const uint8_t* data,
                                                        size_t size) {
  std::vector<uint8_t> frame;
  frame.reserve(10 + size);

  // FIN=1, opcode=binary(0x2)
  frame.push_back(0x82);

  // Payload length (no mask for server-to-client frames)
  if (size < 126) {
    frame.push_back(static_cast<uint8_t>(size));
  } else if (size <= 0xFFFF) {
    frame.push_back(126);
    frame.push_back(static_cast<uint8_t>((size >> 8) & 0xFF));
    frame.push_back(static_cast<uint8_t>(size & 0xFF));
  } else {
    frame.push_back(127);
    for (int i = 7; i >= 0; --i) {
      frame.push_back(static_cast<uint8_t>((size >> (i * 8)) & 0xFF));
    }
  }

  frame.insert(frame.end(), data, data + size);
  return frame;
}

}  // namespace loong::network
