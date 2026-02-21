// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_VIDEO_INPUT_ONVIF_ONVIF_DISCOVERY_H_
#define LOONG_VIDEO_INPUT_ONVIF_ONVIF_DISCOVERY_H_

#include <cstdint>
#include <string>
#include <vector>

namespace loong::video_input {

/// Information about a discovered ONVIF device.
struct OnvifDeviceInfo {
  std::string xaddr;        // Device service endpoint URL
  std::string ip_address;
  std::string manufacturer;
  std::string model;
  std::string firmware_version;
  std::string serial_number;
  std::string hardware_id;
};

/// ONVIF WS-Discovery (IEEE WS-Discovery / SOAP over UDP multicast).
///
/// Sends a Probe message to 239.255.255.250:3702 and collects device
/// responses within a configurable timeout.
class OnvifDiscovery {
 public:
  /// Discover ONVIF devices on the local network.
  /// @param timeout_ms  How long to wait for probe responses.
  /// @return List of discovered devices.
  static std::vector<OnvifDeviceInfo> Discover(int timeout_ms = 3000);

 private:
  static std::string BuildProbeMessage();
  static std::vector<std::string> ParseProbeResponse(
      const std::string& xml_response);
};

}  // namespace loong::video_input

#endif  // LOONG_VIDEO_INPUT_ONVIF_ONVIF_DISCOVERY_H_
