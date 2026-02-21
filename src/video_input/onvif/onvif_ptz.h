// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_VIDEO_INPUT_ONVIF_ONVIF_PTZ_H_
#define LOONG_VIDEO_INPUT_ONVIF_ONVIF_PTZ_H_

#include <string>

namespace loong::video_input {

/// PTZ (Pan-Tilt-Zoom) control via ONVIF.
///
/// Sends ONVIF PTZ SOAP commands to the device's PTZ service endpoint.
class OnvifPtz {
 public:
  /// @param ptz_service_url  The ONVIF PTZ service URL.
  /// @param profile_token    The media profile to control.
  OnvifPtz(std::string  ptz_service_url,
           std::string  profile_token);

  /// Set authentication credentials.
  void SetCredentials(const std::string& username,
                      const std::string& password);

  /// Continuous move: pan/tilt/zoom speeds in [-1.0, 1.0].
  bool ContinuousMove(float pan_speed, float tilt_speed, float zoom_speed);

  /// Stop all movement.
  bool Stop();

  /// Absolute move to position (pan, tilt in [-1,1], zoom in [0,1]).
  bool AbsoluteMove(float pan, float tilt, float zoom);

  /// Relative move by delta values.
  bool RelativeMove(float pan_delta, float tilt_delta, float zoom_delta);

  /// Go to a predefined preset.
  bool GotoPreset(const std::string& preset_token);

  /// Save the current position as a preset.
  bool SetPreset(const std::string& preset_name, std::string& preset_token);

 private:
  std::string SendPtzCommand(const std::string& action,
                             const std::string& body);
  std::string BuildEnvelope(const std::string& body) const;
  std::string BuildSecurityHeader() const;

  std::string ptz_url_;
  std::string profile_token_;
  std::string username_;
  std::string password_;
};

}  // namespace loong::video_input

#endif  // LOONG_VIDEO_INPUT_ONVIF_ONVIF_PTZ_H_
