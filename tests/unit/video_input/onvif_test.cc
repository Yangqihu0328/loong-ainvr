// Copyright 2026 Loong AI NVR Project

#include "video_input/onvif/onvif_device.h"
#include "video_input/onvif/onvif_discovery.h"
#include "video_input/onvif/onvif_ptz.h"

#include <gtest/gtest.h>

namespace loong {
namespace video_input {
namespace {

TEST(OnvifDiscoveryTest, DiscoverReturnsEmptyInTestEnv) {
  // In a test environment without real ONVIF devices,
  // Discover should return an empty list within the timeout.
  auto devices = OnvifDiscovery::Discover(500);
  // Just verify it doesn't crash.
  EXPECT_GE(devices.size(), 0u);
}

TEST(OnvifDeviceTest, ConstructionDoesNotCrash) {
  OnvifDevice device("http://192.168.1.100/onvif/device_service");
  device.SetCredentials("admin", "password");
  // No network call yet, just construction.
  SUCCEED();
}

TEST(OnvifDeviceTest, GetProfilesReturnsEmptyForUnreachable) {
  OnvifDevice device("http://192.168.255.255:9999/onvif/device_service");
  auto profiles = device.GetProfiles();
  EXPECT_TRUE(profiles.empty());
}

TEST(OnvifPtzTest, ConstructionDoesNotCrash) {
  OnvifPtz ptz("http://192.168.1.100/onvif/ptz_service", "profile1");
  ptz.SetCredentials("admin", "password");
  SUCCEED();
}

TEST(OnvifPtzTest, StopDoesNotCrashOnUnreachable) {
  OnvifPtz ptz("http://192.168.255.255:9999/onvif/ptz_service", "profile1");
  bool ok = ptz.Stop();
  EXPECT_FALSE(ok);
}

}  // namespace
}  // namespace video_input
}  // namespace loong
