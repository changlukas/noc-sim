// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#include "axi/memory.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;

TEST(Memory, InBoundsWriteImmediateResp_ZeroLatency) {
  axi::Memory mem(0x1000, 0x1000, 0, 0);
  axi::MemWriteReq req{};
  req.addr = 0x1000;
  req.data.fill(0xAB);
  req.strb = 0xFFFF'FFFFu;
  req.id   = 0x05;
  req.last = true;
  req.tag  = 42;
  EXPECT_TRUE(mem.submit_write(req));
  mem.tick();
  auto resp = mem.pop_write_resp();
  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->id,   0x05);
  EXPECT_EQ(resp->resp, axi::Resp::OKAY);
  EXPECT_EQ(resp->tag,  42u);
}

TEST(Memory, WriteLatencyCountdown) {
  axi::Memory mem(0x1000, 0x1000, 5, 0);
  axi::MemWriteReq req{};
  req.addr = 0x1000; req.data.fill(0x55); req.strb = 0xFFFF'FFFFu;
  req.id = 1; req.last = true; req.tag = 100;
  EXPECT_TRUE(mem.submit_write(req));
  for (int t = 1; t <= 5; ++t) {
    mem.tick();
    auto r = mem.pop_write_resp();
    if (t < 5) {
      EXPECT_FALSE(r.has_value()) << "premature response at tick " << t;
    } else {
      ASSERT_TRUE(r.has_value());
      EXPECT_EQ(r->tag, 100u);
    }
  }
}

TEST(Memory, ReadLatencyCountdown) {
  axi::Memory mem(0x1000, 0x1000, 0, 3);
  axi::MemReadReq req{};
  req.addr = 0x1000; req.size = 5; req.id = 2; req.last = true; req.tag = 200;
  EXPECT_TRUE(mem.submit_read(req));
  EXPECT_FALSE((mem.tick(), mem.pop_read_resp()).has_value());
  EXPECT_FALSE((mem.tick(), mem.pop_read_resp()).has_value());
  auto r = (mem.tick(), mem.pop_read_resp());
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->tag, 200u);
}
