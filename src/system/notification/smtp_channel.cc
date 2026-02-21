// Copyright 2026 Loong AI NVR Project

#include "system/notification/smtp_channel.h"

#include "spdlog/spdlog.h"

#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <sstream>

namespace loong::system {

namespace {

constexpr int kSocketTimeout = 10;  // seconds

std::string ReadLine(SSL* ssl, int fd) {
  std::string line;
  char c = 0;
  while (true) {
    int n = ssl ? SSL_read(ssl, &c, 1) : static_cast<int>(read(fd, &c, 1));
    if (n <= 0) break;
    line += c;
    if (c == '\n') break;
  }
  return line;
}

bool SendCmd(SSL* ssl, int fd, const std::string& cmd) {
  if (ssl) {
    return SSL_write(ssl, cmd.c_str(), static_cast<int>(cmd.size())) > 0;
  }
  return write(fd, cmd.c_str(), cmd.size()) > 0;
}

std::string ReadResponse(SSL* ssl, int fd) {
  std::string result;
  while (true) {
    auto line = ReadLine(ssl, fd);
    result += line;
    if (line.size() >= 4 && line[3] == ' ') break;
    if (line.empty()) break;
  }
  return result;
}

int GetResponseCode(const std::string& response) {
  if (response.size() < 3) return -1;
  try {
    return std::stoi(response.substr(0, 3));
  } catch (...) {
    return -1;
  }
}

}  // namespace

SmtpChannel::SmtpChannel(const SmtpConfig& config) : config_(config) {}

bool SmtpChannel::IsConfigured() const {
  return config_.enabled && !config_.host.empty() &&
         !config_.from_address.empty() && !config_.to_addresses.empty();
}

void SmtpChannel::UpdateConfig(const SmtpConfig& config) { config_ = config; }

NotificationResult SmtpChannel::Test() {
  NotificationMessage test_msg;
  test_msg.title = "Loong AI NVR Test Notification";
  test_msg.body =
      "This is a test notification from Loong AI NVR. "
      "If you received this, email notifications are working.";
  test_msg.severity = "info";
  test_msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  return Send(test_msg);
}

NotificationResult SmtpChannel::Send(const NotificationMessage& msg) {
  if (!IsConfigured()) {
    return {false, "SMTP not configured"};
  }

  std::string subject = "[" + msg.severity + "] " + msg.title;
  std::string html = FormatHtmlBody(msg);
  return SendEmail(subject, html);
}

NotificationResult SmtpChannel::SendEmail(const std::string& subject,
                                          const std::string& html_body) {
  // Resolve host
  struct addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo* result = nullptr;
  std::string port_str = std::to_string(config_.port);
  if (getaddrinfo(config_.host.c_str(), port_str.c_str(), &hints, &result) !=
      0) {
    return {false, "DNS resolution failed for " + config_.host};
  }

  int sock =
      socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (sock < 0) {
    freeaddrinfo(result);
    return {false, "socket creation failed"};
  }

  // Set timeout
  struct timeval tv {};
  tv.tv_sec = kSocketTimeout;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  if (connect(sock, result->ai_addr, result->ai_addrlen) != 0) {
    freeaddrinfo(result);
    close(sock);
    return {false,
            "connection to " + config_.host + ":" + port_str + " failed"};
  }
  freeaddrinfo(result);

  SSL_CTX* ctx = nullptr;
  SSL* ssl = nullptr;
  auto cleanup = [&]() {
    if (ssl) {
      SSL_shutdown(ssl);
      SSL_free(ssl);
    }
    if (ctx) SSL_CTX_free(ctx);
    close(sock);
  };

  // Read greeting
  auto resp = ReadResponse(nullptr, sock);
  if (GetResponseCode(resp) != 220) {
    cleanup();
    return {false, "SMTP greeting failed: " + resp};
  }

  // EHLO
  SendCmd(nullptr, sock, "EHLO localhost\r\n");
  resp = ReadResponse(nullptr, sock);
  if (GetResponseCode(resp) != 250) {
    cleanup();
    return {false, "EHLO failed: " + resp};
  }

  // STARTTLS if configured
  if (config_.use_tls) {
    SendCmd(nullptr, sock, "STARTTLS\r\n");
    resp = ReadResponse(nullptr, sock);
    if (GetResponseCode(resp) != 220) {
      cleanup();
      return {false, "STARTTLS failed: " + resp};
    }

    ctx = SSL_CTX_new(TLS_client_method());
    ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    if (SSL_connect(ssl) <= 0) {
      cleanup();
      return {false, "TLS handshake failed"};
    }

    // EHLO again after TLS
    SendCmd(ssl, sock, "EHLO localhost\r\n");
    resp = ReadResponse(ssl, sock);
  }

  // AUTH LOGIN
  if (!config_.username.empty()) {
    SendCmd(ssl, sock, "AUTH LOGIN\r\n");
    resp = ReadResponse(ssl, sock);
    if (GetResponseCode(resp) != 334) {
      cleanup();
      return {false, "AUTH LOGIN failed: " + resp};
    }

    SendCmd(ssl, sock, Base64Encode(config_.username) + "\r\n");
    resp = ReadResponse(ssl, sock);
    if (GetResponseCode(resp) != 334) {
      cleanup();
      return {false, "AUTH username failed: " + resp};
    }

    SendCmd(ssl, sock, Base64Encode(config_.password) + "\r\n");
    resp = ReadResponse(ssl, sock);
    if (GetResponseCode(resp) != 235) {
      cleanup();
      return {false, "AUTH password failed: " + resp};
    }
  }

  // MAIL FROM
  SendCmd(ssl, sock, "MAIL FROM:<" + config_.from_address + ">\r\n");
  resp = ReadResponse(ssl, sock);
  if (GetResponseCode(resp) != 250) {
    cleanup();
    return {false, "MAIL FROM failed: " + resp};
  }

  // RCPT TO
  for (const auto& to : config_.to_addresses) {
    SendCmd(ssl, sock, "RCPT TO:<" + to + ">\r\n");
    resp = ReadResponse(ssl, sock);
    if (GetResponseCode(resp) != 250) {
      cleanup();
      return {false, "RCPT TO failed for " + to + ": " + resp};
    }
  }

  // DATA
  SendCmd(ssl, sock, "DATA\r\n");
  resp = ReadResponse(ssl, sock);
  if (GetResponseCode(resp) != 354) {
    cleanup();
    return {false, "DATA failed: " + resp};
  }

  // Build email headers + body
  std::string to_list;
  for (size_t i = 0; i < config_.to_addresses.size(); ++i) {
    if (i > 0) to_list += ", ";
    to_list += config_.to_addresses[i];
  }

  std::time_t now = std::time(nullptr);
  char date_buf[64];
  std::strftime(date_buf, sizeof(date_buf), "%a, %d %b %Y %H:%M:%S %z",
                std::localtime(&now));

  std::ostringstream email;
  email << "From: " << config_.from_name << " <" << config_.from_address
        << ">\r\n";
  email << "To: " << to_list << "\r\n";
  email << "Subject: " << subject << "\r\n";
  email << "Date: " << date_buf << "\r\n";
  email << "MIME-Version: 1.0\r\n";
  email << "Content-Type: text/html; charset=utf-8\r\n";
  email << "\r\n";
  email << html_body << "\r\n";
  email << ".\r\n";

  SendCmd(ssl, sock, email.str());
  resp = ReadResponse(ssl, sock);
  if (GetResponseCode(resp) != 250) {
    cleanup();
    return {false, "email delivery failed: " + resp};
  }

  // QUIT
  SendCmd(ssl, sock, "QUIT\r\n");
  ReadResponse(ssl, sock);

  cleanup();
  spdlog::info("SmtpChannel: email sent to {} recipients",
               config_.to_addresses.size());
  return {true, ""};
}

std::string SmtpChannel::FormatHtmlBody(const NotificationMessage& msg) {
  std::ostringstream html;
  html << "<html><body style='font-family:Arial,sans-serif;'>";
  html << "<h2 style='color:#333;'>" << msg.title << "</h2>";
  html << "<p>" << msg.body << "</p>";
  html << "<table style='border-collapse:collapse;margin:16px 0;'>";

  if (msg.channel_id >= 0) {
    html << "<tr><td style='padding:4px 12px;font-weight:bold;'>Channel</td>"
         << "<td style='padding:4px 12px;'>" << msg.channel_id << "</td></tr>";
  }
  if (!msg.event_type.empty()) {
    html << "<tr><td style='padding:4px 12px;font-weight:bold;'>Event</td>"
         << "<td style='padding:4px 12px;'>" << msg.event_type << "</td></tr>";
  }
  if (msg.confidence > 0) {
    html << "<tr><td style='padding:4px 12px;font-weight:bold;'>"
         << "Confidence</td><td style='padding:4px 12px;'>"
         << static_cast<int>(msg.confidence * 100) << "%</td></tr>";
  }

  std::string severity_color = "#17a2b8";
  if (msg.severity == "warning") severity_color = "#ffc107";
  if (msg.severity == "critical") severity_color = "#dc3545";
  html << "<tr><td style='padding:4px 12px;font-weight:bold;'>Severity</td>"
       << "<td style='padding:4px 12px;color:" << severity_color << ";'>"
       << msg.severity << "</td></tr>";

  html << "</table>";
  html << "<hr style='border:1px solid #eee;'>";
  html << "<p style='color:#999;font-size:12px;'>"
       << "Sent by Loong AI NVR</p>";
  html << "</body></html>";
  return html.str();
}

std::string SmtpChannel::Base64Encode(const std::string& input) {
  BIO* bio = BIO_new(BIO_s_mem());
  BIO* b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  bio = BIO_push(b64, bio);
  BIO_write(bio, input.c_str(), static_cast<int>(input.size()));
  BIO_flush(bio);

  BUF_MEM* buf = nullptr;
  BIO_get_mem_ptr(bio, &buf);
  std::string encoded(buf->data, buf->length);
  BIO_free_all(bio);
  return encoded;
}

}  // namespace loong::system
