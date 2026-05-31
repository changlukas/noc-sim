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

TEST(AxiSlave, AwWIndependence_WBeforeAw) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  for (uint8_t i = 0; i < 2; ++i) {
    axi::WBeat w{};
    w.data.fill(0xAA + i);
    w.strb = 0xFFFF'FFFFu;
    w.last = (i == 1);
    slave.push_w(w);
  }
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 0u);

  axi::AwBeat aw{};
  aw.id = 5; aw.addr = 0x3000; aw.len = 1; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 2u);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0xAA);
  EXPECT_EQ(mem.captured_writes[1].data[0], 0xAB);
}

TEST(AxiSlave, WriteBurstAtomicOob_PushesDecerrSkipsMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x100);  // 256 bytes

  axi::AwBeat aw{};
  aw.id = 9; aw.addr = 0x10E0; aw.len = 3; aw.size = 5; aw.burst = axi::Burst::INCR;
  // 4 beats * 32 bytes = 128 -> 0x10E0 + 128 = 0x1160 > 0x1100 -> OOB
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 4; ++i) {
    axi::WBeat w{}; w.data.fill(0); w.strb = 0xFFFF'FFFFu; w.last = (i==3);
    slave.push_w(w);
  }
  slave.tick();

  EXPECT_EQ(mem.captured_writes.size(), 0u);
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->resp, axi::Resp::DECERR);
  EXPECT_EQ(slave.w_q_size(), 0u);
}

TEST(AxiSlave, ReadBurstSingleBeatInBoundsOkay) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 2; ar.addr = 0x1080; ar.len = 0; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 1u);
  EXPECT_EQ(mem.captured_reads.front().addr, 0x1080u);

  axi::MemReadResp rresp{};
  rresp.id = 2; rresp.data.fill(0x77); rresp.resp = axi::Resp::OKAY;
  rresp.last = true; rresp.tag = mem.captured_reads.front().tag;
  mem.queued_read_resps.push_back(rresp);
  slave.tick();

  auto r = slave.pop_r();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->id, 2); EXPECT_EQ(r->resp, axi::Resp::OKAY);
  EXPECT_EQ(r->last, true);
  EXPECT_EQ(r->data[0], 0x77);
}

TEST(AxiSlave, ReadBurstIncr4Beat_InBounds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 6; ar.addr = 0x1000; ar.len = 3; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 4u);

  for (uint8_t i = 0; i < 4; ++i) {
    axi::MemReadResp rresp{};
    rresp.id = 6; rresp.data.fill(0xB0 + i); rresp.resp = axi::Resp::OKAY;
    rresp.last = (i == 3); rresp.tag = mem.captured_reads[i].tag;
    mem.queued_read_resps.push_back(rresp);
  }
  for (int t = 0; t < 4; ++t) slave.tick();

  for (uint8_t i = 0; i < 4; ++i) {
    auto r = slave.pop_r();
    ASSERT_TRUE(r.has_value()) << "beat " << int(i);
    EXPECT_EQ(r->data[0], 0xB0 + i);
    EXPECT_EQ(r->last, i == 3);
  }
}

TEST(AxiSlave, ReadBurstAtomicOob_AllBeatsDecerr) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x100);
  axi::ArBeat ar{};
  ar.id = 4; ar.addr = 0x10F0; ar.len = 1; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  slave.tick();
  EXPECT_EQ(mem.captured_reads.size(), 0u);
  for (uint8_t i = 0; i < 2; ++i) {
    auto r = slave.pop_r();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->resp, axi::Resp::DECERR);
    EXPECT_EQ(r->last, i == 1);
  }
}

TEST(AxiSlave, BackpressureRetry_NoBeatDropped) {
  test::MockMemoryPort mem;
  mem.write_capacity = 1;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 1; aw.addr = 0x1000; aw.len = 2; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 3; ++i) {
    axi::WBeat w{}; w.data.fill(0x40 + i); w.strb = 0xFFFF'FFFFu; w.last = (i == 2);
    slave.push_w(w);
  }
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(slave.w_q_size(), 2u);

  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  mem.captured_writes.pop_front();
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);

  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  mem.captured_writes.pop_front();
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);

  EXPECT_EQ(slave.w_q_size(), 0u);
}

TEST(AxiSlave, WriteBurstWorstRespAccumulatedAcrossBeats) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  axi::AwBeat aw{};
  aw.id = 11; aw.addr = 0x4000; aw.len = 2; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 3; ++i) {
    axi::WBeat w{}; w.data.fill(0); w.strb = 0xFFFF'FFFFu; w.last = (i == 2);
    slave.push_w(w);
  }
  // 3 ticks to submit all 3 W beats
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 3u);

  // Mock returns: beat 0 OKAY, beat 1 DECERR, beat 2 SLVERR
  // worst should be DECERR (3) > SLVERR (2) > OKAY (0)
  mem.queued_write_resps.push_back(axi::MemWriteResp{11, axi::Resp::OKAY,   mem.captured_writes[0].tag});
  mem.queued_write_resps.push_back(axi::MemWriteResp{11, axi::Resp::DECERR, mem.captured_writes[1].tag});
  mem.queued_write_resps.push_back(axi::MemWriteResp{11, axi::Resp::SLVERR, mem.captured_writes[2].tag});

  for (int t = 0; t < 4; ++t) slave.tick();
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->resp, axi::Resp::DECERR) << "worst_resp should pick arithmetic max (DECERR=3), not last (SLVERR=2)";
}

TEST(AxiSlave, SequentialBurstsDifferentIds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw1{}; aw1.id = 1; aw1.addr = 0x1000; aw1.len = 0; aw1.size = 5;
  aw1.burst = axi::Burst::INCR;
  slave.push_aw(aw1);
  axi::WBeat w1{}; w1.data.fill(0x11); w1.strb = 0xFFFF'FFFFu; w1.last = true;
  slave.push_w(w1);
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].id, 1);
  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  slave.tick();
  EXPECT_TRUE(slave.pop_b().has_value());

  axi::AwBeat aw2 = aw1; aw2.id = 2; aw2.addr = 0x1100;
  axi::WBeat w2 = w1; w2.data.fill(0x22);
  slave.push_aw(aw2);
  slave.push_w(w2);
  mem.captured_writes.pop_front();
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].id, 2);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0x22);
}

// Phase B-3b: narrow burst (size=2, bpb=4) addr+strb forwarded per-beat
// without slave-side reinterpretation. AxiSlave just propagates the W beats
// to the memory port; addr increments by bpb per INCR beat.
TEST(AxiSlave, NarrowTransferForwardedToMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 9; aw.addr = 0x1004; aw.len = 1; aw.size = 2; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  // Beat 0: addr 0x1004, byte_lane=4, strb=0xF<<4=0xF0.
  axi::WBeat w0{}; w0.data.fill(0); w0.strb = 0x000000F0u; w0.last = false;
  slave.push_w(w0);
  // Beat 1: addr 0x1008, byte_lane=8, strb=0xF<<8=0xF00.
  axi::WBeat w1{}; w1.data.fill(0); w1.strb = 0x00000F00u; w1.last = true;
  slave.push_w(w1);

  for (int t = 0; t < 4; ++t) slave.tick();

  ASSERT_EQ(mem.captured_writes.size(), 2u);
  EXPECT_EQ(mem.captured_writes[0].addr, 0x1004u);
  EXPECT_EQ(mem.captured_writes[0].strb, 0x000000F0u);
  EXPECT_EQ(mem.captured_writes[0].last, false);
  EXPECT_EQ(mem.captured_writes[1].addr, 0x1008u);
  EXPECT_EQ(mem.captured_writes[1].strb, 0x00000F00u);
  EXPECT_EQ(mem.captured_writes[1].last, true);
}

// Phase B-2.3: WSTRB on the W channel passes through to MemWriteReq.strb
// unchanged. AxiSlave does not interpret WSTRB; the memory port records the
// exact mask the master emitted so per-byte enable semantics survive.
TEST(AxiSlave, SparseStrbForwardedToMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 8; aw.addr = 0x1000; aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  axi::WBeat w{};
  w.data.fill(0xCC);
  w.strb = 0xFFFFFFF8u;  // lanes 0..2 cleared, lanes 3..31 set
  w.last = true;
  slave.push_w(w);

  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].strb, 0xFFFFFFF8u);
}

// Regression: when several AWs are queued before their B responses come back,
// the W router must advance to the next AW once the current burst's W beats
// are fully forwarded — not wait for the B drain. Otherwise the second burst's
// W beats are mis-routed to (or overwrite) the first burst's address space.
TEST(AxiSlave, ConcurrentBurstsDifferentIds_WRoutingAdvances) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw1{}; aw1.id = 1; aw1.addr = 0x1000; aw1.len = 0; aw1.size = 5;
  aw1.burst = axi::Burst::INCR;
  axi::AwBeat aw2 = aw1; aw2.id = 2; aw2.addr = 0x1020;
  axi::AwBeat aw3 = aw1; aw3.id = 3; aw3.addr = 0x1040;

  axi::WBeat w1{}; w1.data.fill(0x11); w1.strb = 0xFFFF'FFFFu; w1.last = true;
  axi::WBeat w2 = w1; w2.data.fill(0x22);
  axi::WBeat w3 = w1; w3.data.fill(0x33);

  // Push 3 AWs + 3 W beats all in one tick, no B responses yet.
  slave.push_aw(aw1); slave.push_w(w1);
  slave.push_aw(aw2); slave.push_w(w2);
  slave.push_aw(aw3); slave.push_w(w3);
  slave.tick();

  // All 3 W beats must reach memory with the correct (id, addr, data) routing.
  ASSERT_EQ(mem.captured_writes.size(), 3u);
  EXPECT_EQ(mem.captured_writes[0].id,   1);
  EXPECT_EQ(mem.captured_writes[0].addr, 0x1000u);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0x11);
  EXPECT_EQ(mem.captured_writes[1].id,   2);
  EXPECT_EQ(mem.captured_writes[1].addr, 0x1020u);
  EXPECT_EQ(mem.captured_writes[1].data[0], 0x22);
  EXPECT_EQ(mem.captured_writes[2].id,   3);
  EXPECT_EQ(mem.captured_writes[2].addr, 0x1040u);
  EXPECT_EQ(mem.captured_writes[2].data[0], 0x33);
}

// Phase B-4: WRAP burst per-beat address wraps at wrap_upper = wrap_lower +
// total_burst_bytes (AXI4 IHI 0022 B1.4.3). wrap_lower = addr &
// ~(total_burst_bytes - 1). WRAP requires len ∈ {1,3,7,15} and addr aligned
// to (1<<size); the parser enforces these, so the slave can assume total is
// a power of 2.
TEST(AxiSlave, WrapBurstLen3_4BeatActualWrap) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 5; ar.addr = 0x1060;  // burst total = 4*32 = 128, wrap_lower = 0x1000, wrap_upper = 0x1080
  ar.len = 3; ar.size = 5; ar.burst = axi::Burst::WRAP;
  // beat 0 @ 0x1060; beat 1 @ 0x1080 → wraps → 0x1000; beat 2 @ 0x1020; beat 3 @ 0x1040
  slave.push_ar(ar);
  for (int t = 0; t < 6; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 4u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x1060u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x1000u);
  EXPECT_EQ(mem.captured_reads[2].addr, 0x1020u);
  EXPECT_EQ(mem.captured_reads[3].addr, 0x1040u);
}

TEST(AxiSlave, WrapBurstLen1_2BeatNoWrap) {
  // 2-beat WRAP that stays inside the wrap window without crossing wrap_upper.
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  axi::ArBeat ar{};
  ar.id = 7; ar.addr = 0x1040; ar.len = 1; ar.size = 5; ar.burst = axi::Burst::WRAP;
  // burst total = 64, wrap_lower = 0x1040 (already aligned to 64), wrap_upper = 0x1080.
  // Beats at 0x1040, 0x1060 (both < 0x1080).
  slave.push_ar(ar);
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 2u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x1040u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x1060u);
}

TEST(AxiSlave, WrapBurstLen7_8BeatActualWrap) {
  // 8-beat WRAP, addr mid-window.
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  axi::ArBeat ar{};
  ar.id = 9; ar.addr = 0x10C0; ar.len = 7; ar.size = 5; ar.burst = axi::Burst::WRAP;
  // burst total = 256, wrap_lower = 0x1000, wrap_upper = 0x1100. Beats start at 0x10C0.
  // beat 0 @ 0x10C0, beat 1 @ 0x10E0, beat 2 @ 0x1100 → wraps → 0x1000, ..., beat 7 @ 0x10A0.
  slave.push_ar(ar);
  for (int t = 0; t < 10; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 8u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x10C0u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x10E0u);
  EXPECT_EQ(mem.captured_reads[2].addr, 0x1000u);
  EXPECT_EQ(mem.captured_reads[7].addr, 0x10A0u);
}

TEST(AxiSlave, WrapBurstLen15_16Beat) {
  // 16-beat narrow WRAP — size=4 (bpb=16, burst total = 16*16 = 256).
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  axi::ArBeat ar{};
  ar.id = 11; ar.addr = 0x10F0; ar.len = 15; ar.size = 4; ar.burst = axi::Burst::WRAP;
  // burst total = 256, wrap_lower = 0x1000, wrap_upper = 0x1100. 16 beats of 16 bytes.
  slave.push_ar(ar);
  for (int t = 0; t < 20; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 16u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x10F0u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x1000u);  // wraps immediately after first beat
  EXPECT_EQ(mem.captured_reads[15].addr, 0x10E0u);
}

// Phase B-5a: Per-ID FIFO. AXI4 allows multi-outstanding bursts with the same
// ID provided responses come back in issue order. The slave now keys
// active_writes_ / active_reads_ as map<id, deque<state>>, so stacked same-id
// AWs are admitted (no exclusion) and W beats route to the oldest in-flight
// burst at that id via aw_issue_order_.
TEST(AxiSlave, SameIdMultiOutstanding_FifoOrder) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  for (int i = 0; i < 3; ++i) {
    axi::AwBeat aw{};
    aw.id = 5; aw.addr = 0x1000 + i * 0x40;
    aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
    slave.push_aw(aw);
    axi::WBeat w{}; w.data.fill(0xA0 + i); w.strb = 0xFFFFFFFFu; w.last = true;
    slave.push_w(w);
  }
  for (int t = 0; t < 5; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 3u);
  EXPECT_EQ(mem.captured_writes[0].addr, 0x1000u);
  EXPECT_EQ(mem.captured_writes[1].addr, 0x1040u);
  EXPECT_EQ(mem.captured_writes[2].addr, 0x1080u);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0xA0);
  EXPECT_EQ(mem.captured_writes[1].data[0], 0xA1);
  EXPECT_EQ(mem.captured_writes[2].data[0], 0xA2);

  // Drain B responses in submission order; each B should carry id=5.
  for (std::size_t i = 0; i < 3; ++i) {
    mem.queued_write_resps.push_back(
        axi::MemWriteResp{5, axi::Resp::OKAY, mem.captured_writes[i].tag});
  }
  for (int t = 0; t < 5; ++t) slave.tick();
  for (int i = 0; i < 3; ++i) {
    auto b = slave.pop_b();
    ASSERT_TRUE(b.has_value()) << "burst " << i;
    EXPECT_EQ(b->id, 5);
    EXPECT_EQ(b->resp, axi::Resp::OKAY);
  }
}

// Phase B-4: FIXED burst — every beat targets the same address (AXI4 IHI 0022
// B1.4.3). Memory sees N writes at addr; last-beat-wins semantics emerge
// from sequential storage updates.
TEST(AxiSlave, FixedBurstAllBeatsSameAddr) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  axi::AwBeat aw{};
  aw.id = 6; aw.addr = 0x1000; aw.len = 3; aw.size = 5; aw.burst = axi::Burst::FIXED;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 4; ++i) {
    axi::WBeat w{}; w.data.fill(0x10 + i); w.strb = 0xFFFFFFFFu; w.last = (i == 3);
    slave.push_w(w);
  }
  for (int t = 0; t < 8; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 4u);
  for (auto& cw : mem.captured_writes) {
    EXPECT_EQ(cw.addr, 0x1000u);
  }
}
