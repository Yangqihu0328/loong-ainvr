// Copyright 2026 Loong AI NVR Project

#include "network/websocket/websocket_server.h"

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

WebSocketServer::WebSocketServer(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

WebSocketServer::~WebSocketServer() { Stop(); }

bool WebSocketServer::Start() {
  if (running_) {
    spdlog::warn("WebSocketServer: already running, ignoring Start()");
    return true;
  }

  listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    spdlog::error("WebSocketServer: socket() failed");
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
    spdlog::error("WebSocketServer: bind() failed on {}:{}", host_, port_);
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  if (listen(listen_fd_, 64) < 0) {
    spdlog::error("WebSocketServer: listen() failed");
    close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  running_ = true;
  accept_thread_ = std::thread(&WebSocketServer::AcceptLoop, this);
  spdlog::info("WebSocketServer: listening on ws://{}:{}", host_, port_);
  return true;
}

void WebSocketServer::Stop() {
  if (!running_) return;
  running_ = false;

  // Wake up accept() by closing the listen fd.
  if (listen_fd_ >= 0) {
    shutdown(listen_fd_, SHUT_RDWR);
    close(listen_fd_);
    listen_fd_ = -1;
  }

  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }

  // Close all client connections.
  std::lock_guard<std::mutex> lock(connections_mutex_);
  for (auto& conn : connections_) {
    conn->active = false;
    if (conn->fd >= 0) {
      shutdown(conn->fd, SHUT_RDWR);
      close(conn->fd);
      conn->fd = -1;
    }
  }
  connections_.clear();

  spdlog::info("WebSocketServer: stopped");
}

void WebSocketServer::Broadcast(const std::string& topic,
                                const nlohmann::json& data) {
  nlohmann::json msg = {{"topic", topic}, {"data", data}};
  std::string text = msg.dump();
  auto frame = WebSocketFrame::BuildTextFrame(text);

  std::lock_guard<std::mutex> lock(connections_mutex_);
  for (auto& conn : connections_) {
    if (!conn->active) continue;
    // Check if subscribed (empty set = subscribed to everything).
    if (!conn->subscribed_topics.empty() &&
        conn->subscribed_topics.find(topic) == conn->subscribed_topics.end()) {
      continue;
    }
    SendFrame(conn, frame);
  }
}

size_t WebSocketServer::ConnectionCount() const {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  return connections_.size();
}

// ============================================================
// Private
// ============================================================

void WebSocketServer::AcceptLoop() {
  while (running_) {
    struct pollfd pfd {};
    pfd.fd = listen_fd_;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 500);  // 500ms timeout
    if (ret <= 0) continue;

    struct sockaddr_in client_addr {};
    socklen_t addr_len = sizeof(client_addr);
    int client_fd =
        accept(listen_fd_, reinterpret_cast<struct sockaddr*>(&client_addr),
               &addr_len);
    if (client_fd < 0) continue;

    // Disable Nagle for low latency.
    int flag = 1;
    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Perform WebSocket handshake.
    if (!PerformHandshake(client_fd)) {
      close(client_fd);
      continue;
    }

    auto conn = std::make_shared<WsConnection>();
    conn->fd = client_fd;
    conn->active = true;

    {
      std::lock_guard<std::mutex> lock(connections_mutex_);
      connections_.push_back(conn);
    }

    // Spawn a reader thread for this connection.
    std::thread(&WebSocketServer::HandleClient, this, conn).detach();
  }
}

bool WebSocketServer::PerformHandshake(int fd) {
  char buf[4096];
  ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
  if (n <= 0) return false;
  buf[n] = '\0';

  std::string request(buf, static_cast<size_t>(n));

  // Extract Sec-WebSocket-Key header.
  std::string key_header = "Sec-WebSocket-Key: ";
  auto pos = request.find(key_header);
  if (pos == std::string::npos) return false;

  auto start = pos + key_header.size();
  auto end = request.find("\r\n", start);
  if (end == std::string::npos) return false;

  std::string client_key = request.substr(start, end - start);
  std::string accept_key = WebSocketFrame::ComputeAcceptKey(client_key);
  std::string response = WebSocketFrame::BuildUpgradeResponse(accept_key);

  ssize_t sent = send(fd, response.c_str(), response.size(), MSG_NOSIGNAL);
  return sent == static_cast<ssize_t>(response.size());
}

void WebSocketServer::HandleClient(std::shared_ptr<WsConnection> conn) {
  std::vector<uint8_t> buffer;
  buffer.reserve(8192);
  char recv_buf[4096];

  while (running_ && conn->active) {
    struct pollfd pfd {};
    pfd.fd = conn->fd;
    pfd.events = POLLIN;

    int ret = poll(&pfd, 1, 1000);
    if (ret < 0) break;
    if (ret == 0) continue;

    ssize_t n = recv(conn->fd, recv_buf, sizeof(recv_buf), 0);
    if (n <= 0) break;

    buffer.insert(buffer.end(), recv_buf, recv_buf + n);

    // Parse frames from buffer.
    while (!buffer.empty()) {
      WsFrame frame;
      size_t consumed =
          WebSocketFrame::ParseFrame(buffer.data(), buffer.size(), frame);
      if (consumed == 0) break;

      buffer.erase(buffer.begin(),
                   buffer.begin() + static_cast<ptrdiff_t>(consumed));

      switch (frame.opcode) {
        case WsOpcode::kText: {
          // Parse subscription message.
          try {
            std::string text(frame.payload.begin(), frame.payload.end());
            auto j = nlohmann::json::parse(text);
            if (j.contains("subscribe") && j["subscribe"].is_array()) {
              conn->subscribed_topics.clear();
              for (const auto& t : j["subscribe"]) {
                conn->subscribed_topics.insert(t.get<std::string>());
              }
              spdlog::debug("WebSocket client subscribed to {} topics",
                            conn->subscribed_topics.size());
            }
          } catch (...) {
            // Ignore malformed messages.
          }
          break;
        }
        case WsOpcode::kPing: {
          auto pong = WebSocketFrame::BuildPongFrame(frame.payload);
          SendFrame(conn, pong);
          break;
        }
        case WsOpcode::kClose: {
          auto close_frame = WebSocketFrame::BuildCloseFrame(1000);
          SendFrame(conn, close_frame);
          conn->active = false;
          break;
        }
        default:
          break;
      }
    }
  }

  // Clean up.
  conn->active = false;
  if (conn->fd >= 0) {
    close(conn->fd);
    conn->fd = -1;
  }
  RemoveConnection(conn);
}

void WebSocketServer::SendFrame(const std::shared_ptr<WsConnection>& conn,
                                const std::vector<uint8_t>& frame) {
  if (!conn->active || conn->fd < 0) return;
  std::lock_guard<std::mutex> lock(conn->write_mutex);
  send(conn->fd, frame.data(), frame.size(), MSG_NOSIGNAL);
}

void WebSocketServer::RemoveConnection(
    const std::shared_ptr<WsConnection>& conn) {
  std::lock_guard<std::mutex> lock(connections_mutex_);
  connections_.erase(
      std::remove(connections_.begin(), connections_.end(), conn),
      connections_.end());
}

}  // namespace loong::network
