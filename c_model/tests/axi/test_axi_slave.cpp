// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#include "axi/axi_slave.hpp"
#include "mock_memory_port.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;
namespace test = ni::cmodel::axi::testing;

TEST(AxiSlave, ConstructsAndAcceptsEmptyTick) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  EXPECT_EQ(slave.aw_q_size(), 0u);
  slave.tick();
  EXPECT_EQ(slave.b_q_size(), 0u);
}

TEST(AxiSlave, WriteBurstSingleBeatInBoundsOkay) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  axi::AwBeat aw{};
  aw.id = 7; aw.addr = 0x1000; aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  axi::WBeat w{};
  w.data.fill(0xCD); w.strb = 0xFFFF'FFFFu; w.last = true;
  slave.push_w(w);

  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes.front().id,   7);
  EXPECT_EQ(mem.captured_writes.front().addr, 0x1000u);
  EXPECT_EQ(mem.captured_writes.front().last, true);

  mem.queued_write_resps.push_back(axi::MemWriteResp{
      mem.captured_writes.front().id, axi::Resp::OKAY,
      mem.captured_writes.front().tag});

  slave.tick();
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->id, 7);
  EXPECT_EQ(b->resp, axi::Resp::OKAY);
}

TEST(AxiSlave, WriteBurstIncr8Beat_InBounds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  axi::AwBeat aw{};
  aw.id = 3; aw.addr = 0x2000; aw.len = 7; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  for (uint8_t i = 0; i < 8; ++i) {
    axi::WBeat w{};
    w.data.fill(0x10 + i);
    w.strb = 0xFFFF'FFFFu;
    w.last = (i == 7);
    slave.push_w(w);
  }

  for (int t = 0; t < 16; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 8u);
  for (std::size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(mem.captured_writes[i].addr, 0x2000u + i * 32u);
    EXPECT_EQ(mem.captured_writes[i].data[0], 0x10 + static_cast<uint8_t>(i));
    EXPECT_EQ(mem.captured_writes[i].last, i == 7);
  }

  for (std::size_t i = 0; i < 8; ++i) {
    mem.queued_write_resps.push_back(
        axi::MemWriteResp{3, axi::Resp::OKAY, mem.captured_writes[i].tag});
  }
  for (int t = 0; t < 8; ++t) slave.tick();
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->id, 3);
}
