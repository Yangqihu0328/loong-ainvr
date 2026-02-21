// Copyright 2026 Loong AI NVR Project

#include "video_input/onvif/onvif_discovery.h"

#include <cstring>
#include <functional>
#include <sstream>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <pugixml.hpp>

#include "spdlog/spdlog.h"

namespace loong::video_input {

namespace {

constexpr const char* kMulticastAddr = "239.255.255.250";
constexpr int kMulticastPort = 3702;

// Extract IP address from an XAddr URL (e.g., "http://192.168.1.10/onvif/...")
std::string ExtractIpFromUrl(const std::string& url) {
  auto pos = url.find("://");
  if (pos == std::string::npos) return "";
  pos += 3;
  auto end = url.find(':', pos);
  if (end == std::string::npos) {
    end = url.find('/', pos);
  }
  if (end == std::string::npos) end = url.size();
  return url.substr(pos, end - pos);
}

}  // namespace

std::vector<OnvifDeviceInfo> OnvifDiscovery::Discover(int timeout_ms) {
  std::vector<OnvifDeviceInfo> devices;

  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    spdlog::error("OnvifDiscovery: socket() failed");
    return devices;
  }

  // Allow multiple sockets on the same port.
  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  // Set multicast TTL.
  int ttl = 4;
  setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

  struct sockaddr_in dest {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(static_cast<uint16_t>(kMulticastPort));
  inet_pton(AF_INET, kMulticastAddr, &dest.sin_addr);

  std::string probe = BuildProbeMessage();
  ssize_t sent = sendto(fd, probe.c_str(), probe.size(), 0,
                        reinterpret_cast<struct sockaddr*>(&dest),
                        sizeof(dest));
  if (sent < 0) {
    spdlog::error("OnvifDiscovery: sendto() failed");
    close(fd);
    return devices;
  }

  spdlog::debug("OnvifDiscovery: probe sent ({} bytes)", sent);

  // Collect responses.
  char buf[65536];
  int elapsed = 0;
  constexpr int kPollInterval = 200;

  while (elapsed < timeout_ms) {
    struct pollfd pfd {};
    pfd.fd = fd;
    pfd.events = POLLIN;

    int remaining = timeout_ms - elapsed;
    int wait = std::min(remaining, kPollInterval);
    int ret = poll(&pfd, 1, wait);
    elapsed += wait;

    if (ret <= 0) continue;

    struct sockaddr_in sender {};
    socklen_t sender_len = sizeof(sender);
    ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0,
                         reinterpret_cast<struct sockaddr*>(&sender),
                         &sender_len);
    if (n <= 0) continue;
    buf[n] = '\0';

    std::string response(buf, static_cast<size_t>(n));
    auto xaddrs = ParseProbeResponse(response);

    for (const auto& xaddr : xaddrs) {
      // De-duplicate by xaddr.
      bool dup = false;
      for (const auto& d : devices) {
        if (d.xaddr == xaddr) {
          dup = true;
          break;
        }
      }
      if (dup) continue;

      OnvifDeviceInfo info;
      info.xaddr = xaddr;
      info.ip_address = ExtractIpFromUrl(xaddr);
      devices.push_back(info);

      spdlog::debug("OnvifDiscovery: found device at {}", xaddr);
    }
  }

  close(fd);
  spdlog::info("OnvifDiscovery: discovered {} devices", devices.size());
  return devices;
}

std::string OnvifDiscovery::BuildProbeMessage() {
  std::ostringstream ss;
  ss << R"(<?xml version="1.0" encoding="UTF-8"?>)"
     << "<s:Envelope"
     << " xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\""
     << " xmlns:a=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\""
     << " xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\""
     << " xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
     << "<s:Header>"
     << "<a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/Probe</a:Action>"
     << "<a:MessageID>urn:uuid:loong-ainvr-probe-001</a:MessageID>"
     << "<a:To>urn:schemas-xmlsoap-org:ws:2005:04:discovery</a:To>"
     << "</s:Header>"
     << "<s:Body>"
     << "<d:Probe>"
     << "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
     << "</d:Probe>"
     << "</s:Body>"
     << "</s:Envelope>";
  return ss.str();
}

std::vector<std::string> OnvifDiscovery::ParseProbeResponse(
    const std::string& xml_response) {
  std::vector<std::string> xaddrs;
  pugi::xml_document doc;
  pugi::xml_parse_result result =
      doc.load_string(xml_response.c_str());
  if (!result) return xaddrs;

  // Recursively search for XAddrs elements in the ProbeMatch response.
  std::function<void(pugi::xml_node)> search = [&](pugi::xml_node node) {
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
      std::string name = child.name();
      if (name.find("XAddrs") != std::string::npos) {
        std::string text = child.text().get();
        std::istringstream iss(text);
        std::string url;
        while (iss >> url) {
          if (!url.empty()) {
            xaddrs.push_back(url);
          }
        }
      }
      search(child);
    }
  };
  search(doc);

  return xaddrs;
}

}  // namespace loong::video_input
