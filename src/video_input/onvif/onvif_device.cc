// Copyright 2026 Loong AI NVR Project

#include "video_input/onvif/onvif_device.h"

#include "spdlog/spdlog.h"

#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstring>
#include <functional>
#include <netdb.h>
#include <pugixml.hpp>
#include <sstream>
#include <utility>

namespace loong::video_input {

namespace {

// Simple HTTP POST to an ONVIF endpoint.  Returns the response body.
std::string HttpPost(const std::string& url, const std::string& body,
                     const std::string& content_type) {
  // Parse URL → host, port, path.
  std::string host;
  std::string path;
  int port = 80;

  auto scheme_end = url.find("://");
  size_t start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
  auto path_start = url.find('/', start);
  std::string authority =
      url.substr(start, (path_start != std::string::npos) ? path_start - start
                                                          : std::string::npos);
  path = (path_start != std::string::npos) ? url.substr(path_start) : "/";

  auto colon = authority.find(':');
  if (colon != std::string::npos) {
    host = authority.substr(0, colon);
    port = std::stoi(authority.substr(colon + 1));
  } else {
    host = authority;
  }

  // Connect.
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "";

  // Set timeout.
  struct timeval tv {};
  tv.tv_sec = 5;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr {};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));

  struct hostent* he = gethostbyname(host.c_str());
  if (!he) {
    close(fd);
    return "";
  }
  std::memcpy(&addr.sin_addr, he->h_addr_list[0],
              static_cast<size_t>(he->h_length));

  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <
      0) {
    close(fd);
    return "";
  }

  // Send HTTP POST request.
  std::ostringstream req;
  req << "POST " << path << " HTTP/1.1\r\n"
      << "Host: " << host << ":" << port << "\r\n"
      << "Content-Type: " << content_type << "\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n"
      << "\r\n"
      << body;

  std::string request = req.str();
  send(fd, request.c_str(), request.size(), 0);

  // Read response.
  std::string response;
  char buf[4096];
  ssize_t n = 0;
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
    response.append(buf, static_cast<size_t>(n));
  }
  close(fd);

  // Strip HTTP headers.
  auto header_end = response.find("\r\n\r\n");
  if (header_end != std::string::npos) {
    return response.substr(header_end + 4);
  }
  return response;
}

}  // namespace

OnvifDevice::OnvifDevice(std::string xaddr) : xaddr_(std::move(xaddr)) {}

void OnvifDevice::SetCredentials(const std::string& username,
                                 const std::string& password) {
  username_ = username;
  password_ = password;
}

bool OnvifDevice::GetDeviceInformation(std::string& manufacturer,
                                       std::string& model,
                                       std::string& firmware_version,
                                       std::string& serial_number,
                                       std::string& hardware_id) {
  std::string body =
      "<tds:GetDeviceInformation"
      " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\"/>";

  std::string response = SendSoapRequest(
      xaddr_, "http://www.onvif.org/ver10/device/wsdl/GetDeviceInformation",
      body);

  if (response.empty()) return false;

  pugi::xml_document doc;
  if (!doc.load_string(response.c_str())) return false;

  std::function<void(pugi::xml_node)> search = [&](pugi::xml_node node) {
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
      std::string name = child.name();
      if (name.find("Manufacturer") != std::string::npos) {
        manufacturer = child.text().get();
      } else if (name.find("Model") != std::string::npos) {
        model = child.text().get();
      } else if (name.find("FirmwareVersion") != std::string::npos) {
        firmware_version = child.text().get();
      } else if (name.find("SerialNumber") != std::string::npos) {
        serial_number = child.text().get();
      } else if (name.find("HardwareId") != std::string::npos) {
        hardware_id = child.text().get();
      }
      search(child);
    }
  };
  search(doc);

  spdlog::debug("ONVIF Device: {} {} (FW: {})", manufacturer, model,
                firmware_version);
  return true;
}

bool OnvifDevice::GetMediaServiceUrl(std::string& media_url) {
  std::string body =
      "<tds:GetServices"
      " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\">"
      "<tds:IncludeCapability>false</tds:IncludeCapability>"
      "</tds:GetServices>";

  std::string response = SendSoapRequest(
      xaddr_, "http://www.onvif.org/ver10/device/wsdl/GetServices", body);

  if (response.empty()) return false;

  pugi::xml_document doc;
  if (!doc.load_string(response.c_str())) return false;

  bool found = false;
  std::function<void(pugi::xml_node)> search = [&](pugi::xml_node node) {
    if (found) return;
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
      std::string name = child.name();
      if (name.find("Namespace") != std::string::npos) {
        std::string ns = child.text().get();
        if (ns.find("media") != std::string::npos) {
          auto parent = child.parent();
          for (auto sibling = parent.first_child(); sibling;
               sibling = sibling.next_sibling()) {
            std::string sname = sibling.name();
            if (sname.find("XAddr") != std::string::npos) {
              media_url = sibling.text().get();
              media_url_ = media_url;
              found = true;
              return;
            }
          }
        }
      }
      search(child);
    }
  };
  search(doc);
  if (found) return true;

  // Fallback: derive media URL from device URL.
  media_url = xaddr_;
  auto pos = media_url.rfind("/onvif/");
  if (pos != std::string::npos) {
    media_url = media_url.substr(0, pos) + "/onvif/media_service";
  }
  media_url_ = media_url;
  return true;
}

std::vector<OnvifProfile> OnvifDevice::GetProfiles() {
  std::vector<OnvifProfile> profiles;

  if (media_url_.empty()) {
    std::string url;
    GetMediaServiceUrl(url);
  }

  std::string body =
      "<trt:GetProfiles"
      " xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\"/>";

  std::string response = SendSoapRequest(
      media_url_, "http://www.onvif.org/ver10/media/wsdl/GetProfiles", body);

  if (response.empty()) return profiles;

  pugi::xml_document doc;
  if (!doc.load_string(response.c_str())) return profiles;

  std::function<void(pugi::xml_node)> search = [&](pugi::xml_node node) {
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
      std::string name = child.name();
      if (name.find("Profiles") != std::string::npos) {
        OnvifProfile p;
        p.token = child.attribute("token").as_string();
        p.name = child.child_value("Name");
        if (p.name.empty()) {
          for (auto sub = child.first_child(); sub; sub = sub.next_sibling()) {
            std::string sname = sub.name();
            if (sname.find("Name") != std::string::npos) {
              p.name = sub.text().get();
            }
          }
        }
        profiles.push_back(p);
      }
      search(child);
    }
  };
  search(doc);

  return profiles;
}

std::string OnvifDevice::GetStreamUri(const std::string& profile_token) {
  if (media_url_.empty()) {
    std::string url;
    GetMediaServiceUrl(url);
  }

  std::ostringstream body_ss;
  body_ss << "<trt:GetStreamUri"
          << " xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
          << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
          << "<trt:StreamSetup>"
          << "<tt:Stream>RTP-Unicast</tt:Stream>"
          << "<tt:Transport><tt:Protocol>RTSP</tt:Protocol></tt:Transport>"
          << "</trt:StreamSetup>"
          << "<trt:ProfileToken>" << profile_token << "</trt:ProfileToken>"
          << "</trt:GetStreamUri>";

  std::string response = SendSoapRequest(
      media_url_, "http://www.onvif.org/ver10/media/wsdl/GetStreamUri",
      body_ss.str());

  if (response.empty()) return "";

  pugi::xml_document doc;
  if (!doc.load_string(response.c_str())) return "";

  std::string result_uri;
  std::function<void(pugi::xml_node)> search = [&](pugi::xml_node node) {
    if (!result_uri.empty()) return;
    for (auto child = node.first_child(); child; child = child.next_sibling()) {
      std::string name = child.name();
      if (name.find("Uri") != std::string::npos) {
        std::string uri = child.text().get();
        if (uri.find("rtsp://") != std::string::npos) {
          result_uri = uri;
          return;
        }
      }
      search(child);
    }
  };
  search(doc);
  return result_uri;
}

std::string OnvifDevice::SendSoapRequest(const std::string& url,
                                         const std::string& action,
                                         const std::string& body) {
  std::string envelope = BuildSoapEnvelope(body);
  std::string content_type =
      "application/soap+xml; charset=utf-8; action=\"" + action + "\"";

  return HttpPost(url, envelope, content_type);
}

std::string OnvifDevice::BuildSoapEnvelope(const std::string& body) const {
  std::ostringstream ss;
  ss << R"(<?xml version="1.0" encoding="UTF-8"?>)"
     << "<s:Envelope"
     << " xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
     << "<s:Header>" << BuildSecurityHeader() << "</s:Header>"
     << "<s:Body>" << body << "</s:Body>"
     << "</s:Envelope>";
  return ss.str();
}

std::string OnvifDevice::BuildSecurityHeader() const {
  if (username_.empty()) return "";

  // Simple UsernameToken (cleartext — production should use digest).
  std::ostringstream ss;
  ss << "<Security"
     << " xmlns=\"http://docs.oasis-open.org/wss/2004/01/"
     << "oasis-200401-wss-wssecurity-secext-1.0.xsd\">"
     << "<UsernameToken>"
     << "<Username>" << username_ << "</Username>"
     << "<Password>" << password_ << "</Password>"
     << "</UsernameToken>"
     << "</Security>";
  return ss.str();
}

}  // namespace loong::video_input
