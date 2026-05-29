#include "nmu/axi_slave_port.hpp"
#include <gtest/gtest.h>

namespace nmu = ni::cmodel::nmu;

TEST(AxiSlavePort_Scaffold, BeatStructsAreConstructible) {
  nmu::AwBeat aw{};
  nmu::WBeat  w{};
  nmu::ArBeat ar{};
  nmu::BBeat  b{};
  nmu::RBeat  r{};
  (void)aw; (void)w; (void)ar; (void)b; (void)r;
  SUCCEED();
}

TEST(AxiSlavePort_Scaffold, ChannelMaskBitwise) {
  using nmu::ChannelMask;
  auto m = ChannelMask::Aw | ChannelMask::W;
  EXPECT_TRUE(any(m & ChannelMask::Aw));
  EXPECT_TRUE(any(m & ChannelMask::W));
  EXPECT_FALSE(any(m & ChannelMask::Ar));
}

TEST(AxiSlavePort_Scaffold, QueueDepthsDefault) {
  nmu::QueueDepths d{};
  EXPECT_EQ(d.aw, 16u);
  EXPECT_EQ(d.w,  16u);
  EXPECT_EQ(d.ar, 16u);
  EXPECT_EQ(d.b,  16u);
  EXPECT_EQ(d.r,  16u);
}
