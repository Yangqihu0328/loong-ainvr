// Copyright 2026 Loong AI NVR Project

#include "video_input/onvif/onvif_ptz.h"

#include "spdlog/spdlog.h"

#include <sys/socket.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <cstring>
#include <netdb.h>
#include <sstream>
#include <utility>

namespace loong::video_input {

namespace {

// Reuse the same simple HTTP POST as onvif_device.cc.
std::string HttpPost(const std::string& url, const std::string& body,
                     const std::string& content_type) {
  std::string host;
  std::string path;
  int port = 80;

  auto scheme_end = url.find("://");
  size_t start = (scheme_end != std::string::npos) ? scheme_end + 3 : 0;
  auto path_start = url.find('/', start);
  std::string authority =
      url.substr(start, (path_start != std::string::npos)
                            ? path_start - start
                            : std::string::npos);
  path = (path_start != std::string::npos) ? url.substr(path_start) : "/";

  auto colon = authority.find(':');
  if (colon != std::string::npos) {
    host = authority.substr(0, colon);
    port = std::stoi(authority.substr(colon + 1));
  } else {
    host = authority;
  }

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return "";

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

  if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
              sizeof(addr)) < 0) {
    close(fd);
    return "";
  }

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

  std::string response;
  char buf[4096];
  ssize_t n = 0;
  while ((n = recv(fd, buf, sizeof(buf), 0)) > 0) {
    response.append(buf, static_cast<size_t>(n));
  }
  close(fd);

  auto header_end = response.find("\r\n\r\n");
  if (header_end != std::string::npos) {
    return response.substr(header_end + 4);
  }
  return response;
}

}  // namespace

OnvifPtz::OnvifPtz(std::string  ptz_service_url,
                   std::string  profile_token)
    : ptz_url_(std::move(ptz_service_url)), profile_token_(std::move(profile_token)) {}

void OnvifPtz::SetCredentials(const std::string& username,
                              const std::string& password) {
  username_ = username;
  password_ = password;
}

bool OnvifPtz::ContinuousMove(float pan_speed, float tilt_speed,
                              float zoom_speed) {
  std::ostringstream body;
  body << "<tptz:ContinuousMove"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:Velocity>"
       << "<tt:PanTilt x=\"" << pan_speed << "\" y=\"" << tilt_speed << "\"/>"
       << "<tt:Zoom x=\"" << zoom_speed << "\"/>"
       << "</tptz:Velocity>"
       << "</tptz:ContinuousMove>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/ContinuousMove", body.str());
  return !response.empty();
}

bool OnvifPtz::Stop() {
  std::ostringstream body;
  body << "<tptz:Stop"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:PanTilt>true</tptz:PanTilt>"
       << "<tptz:Zoom>true</tptz:Zoom>"
       << "</tptz:Stop>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/Stop", body.str());
  return !response.empty();
}

bool OnvifPtz::AbsoluteMove(float pan, float tilt, float zoom) {
  std::ostringstream body;
  body << "<tptz:AbsoluteMove"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:Position>"
       << "<tt:PanTilt x=\"" << pan << "\" y=\"" << tilt << "\"/>"
       << "<tt:Zoom x=\"" << zoom << "\"/>"
       << "</tptz:Position>"
       << "</tptz:AbsoluteMove>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/AbsoluteMove", body.str());
  return !response.empty();
}

bool OnvifPtz::RelativeMove(float pan_delta, float tilt_delta,
                            float zoom_delta) {
  std::ostringstream body;
  body << "<tptz:RelativeMove"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:Translation>"
       << "<tt:PanTilt x=\"" << pan_delta << "\" y=\"" << tilt_delta << "\"/>"
       << "<tt:Zoom x=\"" << zoom_delta << "\"/>"
       << "</tptz:Translation>"
       << "</tptz:RelativeMove>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/RelativeMove", body.str());
  return !response.empty();
}

bool OnvifPtz::GotoPreset(const std::string& preset_token) {
  std::ostringstream body;
  body << "<tptz:GotoPreset"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:PresetToken>" << preset_token << "</tptz:PresetToken>"
       << "</tptz:GotoPreset>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/GotoPreset", body.str());
  return !response.empty();
}

bool OnvifPtz::SetPreset(const std::string& preset_name,
                         std::string& preset_token) {
  std::ostringstream body;
  body << "<tptz:SetPreset"
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">"
       << "<tptz:ProfileToken>" << profile_token_ << "</tptz:ProfileToken>"
       << "<tptz:PresetName>" << preset_name << "</tptz:PresetName>"
       << "</tptz:SetPreset>";

  std::string response = SendPtzCommand(
      "http://www.onvif.org/ver20/ptz/wsdl/SetPreset", body.str());

  // Parse the preset token from the response (if available).
  auto pos = response.find("PresetToken");
  if (pos != std::string::npos) {
    auto start = response.find('>', pos);
    auto end = response.find('<', start);
    if (start != std::string::npos && end != std::string::npos) {
      preset_token = response.substr(start + 1, end - start - 1);
    }
  }

  return !response.empty();
}

std::string OnvifPtz::SendPtzCommand(const std::string& action,
                                     const std::string& body) {
  std::string envelope = BuildEnvelope(body);
  std::string content_type =
      "application/soap+xml; charset=utf-8; action=\"" + action + "\"";
  return HttpPost(ptz_url_, envelope, content_type);
}

std::string OnvifPtz::BuildEnvelope(const std::string& body) const {
  std::ostringstream ss;
  ss << R"(<?xml version="1.0" encoding="UTF-8"?>)"
     << "<s:Envelope"
     << " xmlns:s=\"http://www.w3.org/2003/05/soap-envelope\">"
     << "<s:Header>" << BuildSecurityHeader() << "</s:Header>"
     << "<s:Body>" << body << "</s:Body>"
     << "</s:Envelope>";
  return ss.str();
}

std::string OnvifPtz::BuildSecurityHeader() const {
  if (username_.empty()) return "";

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
