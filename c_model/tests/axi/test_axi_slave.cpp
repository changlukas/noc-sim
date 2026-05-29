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
