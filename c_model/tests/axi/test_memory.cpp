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
