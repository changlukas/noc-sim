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

TEST(AxiSlavePort_Inbound, AwRoundTripPreservesAllFields) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_awid_i     = 0x5A;
  p.axi_awaddr_i   = 0x1234'5678'9ABC'DEF0ULL;
  p.axi_awlen_i    = 7;
  p.axi_awsize_i   = 3;
  p.axi_awburst_i  = 1;
  p.axi_awcache_i  = 0xF;
  p.axi_awlock_i   = 0;
  p.axi_awprot_i   = 0x2;
  p.axi_awregion_i = 0x3;
  p.axi_awuser_i   = 0x42;
  p.axi_awqos_i    = 0xC;

  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  auto out = port.pop_aw();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->id,     0x5A);
  EXPECT_EQ(out->addr,   0x1234'5678'9ABC'DEF0ULL);
  EXPECT_EQ(out->len,    7);
  EXPECT_EQ(out->size,   3);
  EXPECT_EQ(out->burst,  1);
  EXPECT_EQ(out->cache,  0xF);
  EXPECT_EQ(out->lock,   0);
  EXPECT_EQ(out->prot,   0x2);
  EXPECT_EQ(out->region, 0x3);
  EXPECT_EQ(out->user,   0x42);
  EXPECT_EQ(out->qos,    0xC);
}

TEST(AxiSlavePort_Inbound, WRoundTripPreservesData) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  for (std::size_t i = 0; i < ni::WSTRB_WIDTH; ++i) p.axi_wdata_i[i] = static_cast<uint8_t>(i ^ 0xA5);
  p.axi_wstrb_i = 0xDEADBEEF;
  p.axi_wlast_i = 1;
  p.axi_wuser_i = 0x33;
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::W));
  auto out = port.pop_w();
  ASSERT_TRUE(out.has_value());
  for (std::size_t i = 0; i < ni::WSTRB_WIDTH; ++i) EXPECT_EQ(out->data[i], static_cast<uint8_t>(i ^ 0xA5));
  EXPECT_EQ(out->strb, 0xDEADBEEF);
  EXPECT_EQ(out->last, 1);
  EXPECT_EQ(out->user, 0x33);
}

TEST(AxiSlavePort_Inbound, ArRoundTripPreservesAllFields) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_arid_i     = 0x71;
  p.axi_araddr_i   = 0xCAFE'BABE'F00D'5AA5ULL;
  p.axi_arlen_i    = 15;
  p.axi_arsize_i   = 2;
  p.axi_arburst_i  = 1;
  p.axi_arcache_i  = 0x6;
  p.axi_arlock_i   = 0;
  p.axi_arprot_i   = 0x1;
  p.axi_arregion_i = 0x5;
  p.axi_aruser_i   = 0x88;
  p.axi_arqos_i    = 0x9;
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Ar));
  auto out = port.pop_ar();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->id,     0x71);
  EXPECT_EQ(out->addr,   0xCAFE'BABE'F00D'5AA5ULL);
  EXPECT_EQ(out->len,    15);
  EXPECT_EQ(out->size,   2);
  EXPECT_EQ(out->burst,  1);
  EXPECT_EQ(out->cache,  0x6);
  EXPECT_EQ(out->lock,   0);
  EXPECT_EQ(out->prot,   0x1);
  EXPECT_EQ(out->region, 0x5);
  EXPECT_EQ(out->user,   0x88);
  EXPECT_EQ(out->qos,    0x9);
}

TEST(AxiSlavePort_Inbound, AllOrNothingRollback) {
  nmu::AxiSlavePort port(nmu::QueueDepths{.aw=1, .w=16});
  ni::pins::AxiSlavePortPins p{};
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_EQ(port.aw_q_size(), 1u);
  EXPECT_EQ(port.w_q_size(),  0u);

  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::Aw | nmu::ChannelMask::W));
  EXPECT_EQ(port.aw_q_size(), 1u);
  EXPECT_EQ(port.w_q_size(),  0u) << "W should not be enqueued when AW path rejected";
}

TEST(AxiSlavePort_Inbound, IndependentDepthsPerChannel) {
  nmu::AxiSlavePort port(nmu::QueueDepths{.aw=2, .w=8, .ar=1, .b=1, .r=1});
  ni::pins::AxiSlavePortPins p{};
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::Aw)) << "aw_q full";
  for (int i = 0; i < 8; ++i)
    EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::W)) << "w_q i=" << i;
  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::W)) << "w_q full at depth 8";
}

TEST(AxiSlavePort_Outbound, BPushPopRoundTrip) {
  nmu::AxiSlavePort port;
  nmu::BBeat b{0x33, 0x1, 0x7};  // id, resp=SLVERR, user
  EXPECT_TRUE(port.push_b(b));
  auto out = port.pop_outbound_b();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->id, 0x33);
  EXPECT_EQ(out->resp, 0x1);
  EXPECT_EQ(out->user, 0x7);
}

TEST(AxiSlavePort_Outbound, RPushPopRoundTrip) {
  nmu::AxiSlavePort port;
  nmu::RBeat r{};
  r.id = 0x55; r.resp = 0x2; r.last = 1; r.user = 0xAB;
  for (std::size_t i = 0; i < ni::WSTRB_WIDTH; ++i) r.data[i] = static_cast<uint8_t>(i);
  EXPECT_TRUE(port.push_r(r));
  auto out = port.pop_outbound_r();
  ASSERT_TRUE(out.has_value());
  EXPECT_EQ(out->id, 0x55); EXPECT_EQ(out->resp, 0x2);
  EXPECT_EQ(out->last, 1);  EXPECT_EQ(out->user, 0xAB);
  for (std::size_t i = 0; i < ni::WSTRB_WIDTH; ++i) EXPECT_EQ(out->data[i], static_cast<uint8_t>(i));
}

TEST(AxiSlavePort_Outbound, IndependentBR) {
  nmu::AxiSlavePort port;
  port.push_b(nmu::BBeat{1, 0, 0});
  EXPECT_TRUE(port.pop_outbound_b().has_value());
  EXPECT_FALSE(port.pop_outbound_r().has_value()) << "R should not be affected by B push";
}

TEST(AxiSlavePort_Tick, IsNoOp) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  port.push_inbound_pins(p, nmu::ChannelMask::Aw);
  port.push_b(nmu::BBeat{1, 0, 0});
  auto aw_before = port.aw_q_size();
  auto b_before  = port.b_q_size();
  port.tick();
  EXPECT_EQ(port.aw_q_size(), aw_before);
  EXPECT_EQ(port.b_q_size(),  b_before);
}

#ifdef NDEBUG
TEST(AxiSlavePort_ProtocolRelease, BadBurstEncodingStillEnqueues) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_awburst_i = 3;  // reserved encoding
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_EQ(port.aw_q_size(), 1u);
}
#endif

#ifndef NDEBUG
TEST(AxiSlavePort_ProtocolDebug, BadBurstEncodingDeathTest) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_awburst_i = 3;
  EXPECT_DEATH(port.push_inbound_pins(p, nmu::ChannelMask::Aw), "AW_\\* protocol violation");
}
#endif
