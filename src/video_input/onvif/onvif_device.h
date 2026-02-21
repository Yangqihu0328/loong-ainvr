// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_VIDEO_INPUT_ONVIF_ONVIF_DEVICE_H_
#define LOONG_VIDEO_INPUT_ONVIF_ONVIF_DEVICE_H_

#include <string>
#include <vector>

namespace loong::video_input {

/// ONVIF media profile information.
struct OnvifProfile {
  std::string token;
  std::string name;
  int width = 0;
  int height = 0;
  int framerate = 0;
  std::string encoding;  // "H264", "H265", "JPEG"
  std::string stream_uri;
};

/// Interacts with a single ONVIF device via SOAP/HTTP.
///
/// Supports:
///   - GetDeviceInformation: manufacturer, model, firmware, serial
///   - GetProfiles: list media profiles
///   - GetStreamUri: get RTSP URL for a given profile
class OnvifDevice {
 public:
  /// Create a device client targeting a specific service endpoint.
  /// @param xaddr  The device service URL (e.g.,
  /// "http://192.168.1.10/onvif/device_service")
  explicit OnvifDevice(std::string xaddr);

  /// Set authentication credentials (ONVIF UsernameToken / WS-Security).
  void SetCredentials(const std::string& username, const std::string& password);

  /// Get basic device information.
  bool GetDeviceInformation(std::string& manufacturer, std::string& model,
                            std::string& firmware_version,
                            std::string& serial_number,
                            std::string& hardware_id);

  /// Discover the device's media service URL.
  bool GetMediaServiceUrl(std::string& media_url);

  /// Get list of media profiles from the media service.
  std::vector<OnvifProfile> GetProfiles();

  /// Get the RTSP stream URI for a given profile token.
  std::string GetStreamUri(const std::string& profile_token);

 private:
  std::string SendSoapRequest(const std::string& url, const std::string& action,
                              const std::string& body);
  std::string BuildSoapEnvelope(const std::string& body) const;
  std::string BuildSecurityHeader() const;

  std::string xaddr_;
  std::string username_;
  std::string password_;
  std::string media_url_;
};

}  // namespace loong::video_input

#endif  // LOONG_VIDEO_INPUT_ONVIF_ONVIF_DEVICE_H_
