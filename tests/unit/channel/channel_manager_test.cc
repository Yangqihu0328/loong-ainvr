// Copyright 2026 Loong AI NVR Project

#include "channel/channel_manager/channel_manager.h"

#include "gtest/gtest.h"

namespace loong {
namespace channel {
namespace {

ChannelConfig MakeConfig(const std::string& name) {
  ChannelConfig config;
  config.name = name;
  config.rtsp_url = "rtsp://test/stream";
  return config;
}

TEST(ChannelManager, CreateAndDelete) {
  ChannelManager mgr;
  int id = mgr.CreateChannel(MakeConfig("cam1"));
  EXPECT_GT(id, 0);
  EXPECT_EQ(mgr.ChannelCount(), 1u);

  EXPECT_TRUE(mgr.DeleteChannel(id));
  EXPECT_EQ(mgr.ChannelCount(), 0u);
}

TEST(ChannelManager, MaxChannelsEnforced) {
  ChannelManager mgr;
  for (int i = 0; i < ChannelManager::kMaxChannels; ++i) {
    int id = mgr.CreateChannel(MakeConfig("cam" + std::to_string(i)));
    EXPECT_GT(id, 0);
  }
  EXPECT_EQ(mgr.ChannelCount(),
            static_cast<size_t>(ChannelManager::kMaxChannels));

  // 65th channel should fail
  int id = mgr.CreateChannel(MakeConfig("cam_overflow"));
  EXPECT_EQ(id, -1);
}

TEST(ChannelManager, GetStatus) {
  ChannelManager mgr;
  int id = mgr.CreateChannel(MakeConfig("cam1"));

  auto status = mgr.GetStatus(id);
  EXPECT_EQ(status.id, id);
  EXPECT_EQ(status.name, "cam1");
  EXPECT_EQ(status.state, ChannelState::kCreated);
}

TEST(ChannelManager, GetAllStatus) {
  ChannelManager mgr;
  mgr.CreateChannel(MakeConfig("cam1"));
  mgr.CreateChannel(MakeConfig("cam2"));
  mgr.CreateChannel(MakeConfig("cam3"));

  auto all_status = mgr.GetAllStatus();
  EXPECT_EQ(all_status.size(), 3u);
}

TEST(ChannelManager, DeleteNonExistent) {
  ChannelManager mgr;
  EXPECT_FALSE(mgr.DeleteChannel(999));
}

}  // namespace
}  // namespace channel
}  // namespace loong
