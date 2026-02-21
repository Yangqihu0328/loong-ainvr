// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_NETWORK_WEBSOCKET_WEBSOCKET_SERVER_H_
#define LOONG_NETWORK_WEBSOCKET_WEBSOCKET_SERVER_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace loong::network {

/// A lightweight WebSocket server (RFC 6455) for real-time event push.
///
/// Supports topic-based subscriptions:
///   - Client sends: {"subscribe": ["alarm", "system_status"]}
///   - Server pushes: {"topic": "alarm", "data": {...}}
///
/// Runs on a separate port from the HTTP server.
class WebSocketServer {
 public:
  WebSocketServer(std::string  host, int port);
  ~WebSocketServer();

  /// Start accepting connections.
  bool Start();

  /// Stop the server and close all connections.
  void Stop();

  /// Broadcast a JSON message to all clients subscribed to a topic.
  void Broadcast(const std::string& topic, const nlohmann::json& data);

  /// Get the number of active connections.
  size_t ConnectionCount() const;

  // Non-copyable
  WebSocketServer(const WebSocketServer&) = delete;
  WebSocketServer& operator=(const WebSocketServer&) = delete;

 private:
  struct WsConnection {
    int fd = -1;
    std::set<std::string> subscribed_topics;
    std::mutex write_mutex;
    std::atomic<bool> active{true};
  };

  void AcceptLoop();
  void HandleClient(std::shared_ptr<WsConnection> conn);
  bool PerformHandshake(int fd);
  void SendFrame(const std::shared_ptr<WsConnection>& conn,
                 const std::vector<uint8_t>& frame);
  void RemoveConnection(const std::shared_ptr<WsConnection>& conn);

  std::string host_;
  int port_;
  int listen_fd_ = -1;

  std::atomic<bool> running_{false};
  std::thread accept_thread_;

  mutable std::mutex connections_mutex_;
  std::vector<std::shared_ptr<WsConnection>> connections_;
};

}  // namespace loong::network

#endif  // LOONG_NETWORK_WEBSOCKET_WEBSOCKET_SERVER_H_
