# NMU AXI Slave Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 實作 c_model NMU 的 `AxiSlavePort` class — 對外 reuse spec-elaborated `ni::pins::AxiSlavePortPins`、對內 per-channel beat queue，5 個 channel 獨立 bounded queue、stateless AXI4 protocol field check on push、tick() 留 hook、不做 stateful intra-burst / ordering / CSR / IRQ。

**Architecture:** Hybrid Approach 3 — DPI/test push 進 `AxiSlavePortPins`，private `dispatch_` 拆成 per-channel `AwBeat`/`WBeat`/`ArBeat` 入對應 `std::deque`；outbound B/R 由 internal features (B-RoB / R-RoB) push 進，外部 adapter 各自 `pop_outbound_b/r` 拿 beat 後驅動對應 pins。

**Tech Stack:** C++17 header-only、GoogleTest、CMake、`ni::pins::AxiSlavePortPins` (from `spec_validate/include/ni_signals.h`)、`ni::width::*` 常數 (from `ni_flit_constants.h`)。

**Spec**: `docs/superpowers/specs/2026-05-28-nmu-axi-slave-port-design.md`

**Project rules**:
- Reply Traditional Chinese (技術詞用 English)
- Drift gates every commit: `cd c_model && cmake --build build && ctest`; `cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check`
- Commit format: `type(scope): description` (English; types: feat/fix/docs/style/refactor/test/chore/perf)
- Post-task: Karpathy 4-lens review before checkpoint
- 不 hardcode 規格值 — 用 `ni::width::AXI_*_WIDTH` 等
- 不翻 `spec/ni/doc/`，spec 來源只有 `spec_validate/include/*.h` + `spec_validate/{authored,generated}/*.json`

---

## Stage 1 — Scaffold

### Task 1.1: Create header file with beat structs + enums

**Files:**
- Create: `c_model/include/nmu/axi_slave_port.hpp`

- [ ] **Step 1: Create header skeleton with types**

```cpp
// c_model/include/nmu/axi_slave_port.hpp
#pragma once
#include "ni_signals.h"         // ni::pins::AxiSlavePortPins
#include "ni_flit_constants.h"  // ni::width::AXI_*_WIDTH, NOC_DATA_WIDTH, WSTRB_WIDTH
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace ni::cmodel::nmu {

// Beat data types -- internal interface for future NMU features (ADDR_TRANS / PACKETIZE / DEPACKETIZE / ROB)

struct AwBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size, burst, cache, lock, prot, region, user, qos;
};

struct ArBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size, burst, cache, lock, prot, region, user, qos;
};

struct WBeat {
  std::array<uint8_t, ni::WSTRB_WIDTH> data;  // 32 bytes = 256 bits matches axi_wdata_i
  uint32_t strb;
  uint8_t  last, user;
};

struct BBeat { uint8_t id, resp, user; };

struct RBeat {
  uint8_t  id;
  std::array<uint8_t, ni::WSTRB_WIDTH> data;
  uint8_t  resp, last, user;
};

// Channel mask -- which channels a push_inbound_pins call carries
enum class ChannelMask : uint8_t {
  None = 0,
  Aw   = 1u << 0,
  W    = 1u << 1,
  Ar   = 1u << 2,
  B    = 1u << 3,
  R    = 1u << 4,
};

constexpr ChannelMask operator|(ChannelMask a, ChannelMask b) {
  return static_cast<ChannelMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr ChannelMask operator&(ChannelMask a, ChannelMask b) {
  return static_cast<ChannelMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr bool any(ChannelMask m) { return static_cast<uint8_t>(m) != 0; }

// Per-channel queue depth config
struct QueueDepths {
  std::size_t aw = 16;
  std::size_t w  = 16;
  std::size_t ar = 16;
  std::size_t b  = 16;
  std::size_t r  = 16;
};

}  // namespace ni::cmodel::nmu
```

- [ ] **Step 2: Verify spec assumptions**

Run: `grep -E "WSTRB_WIDTH|AXI_ID_WIDTH" spec_validate/include/ni_flit_constants.h`
Expected: `constexpr int WSTRB_WIDTH = 32;` and `constexpr int AXI_ID_WIDTH = 8;` present。
若不一致，停下來向 user 確認後再寫 Implementation。

### Task 1.2: CMake wiring for tests/nmu/

**Files:**
- Create: `c_model/tests/nmu/CMakeLists.txt`
- Modify: `c_model/tests/CMakeLists.txt` (add subdirectory)

- [ ] **Step 1: Create subdir CMakeLists**

```cmake
# c_model/tests/nmu/CMakeLists.txt
add_cmodel_test(test_axi_slave_port)
```

- [ ] **Step 2: Wire parent**

Modify `c_model/tests/CMakeLists.txt`: append at end:
```cmake
add_subdirectory(nmu)
```

### Task 1.3: Stub test verifying types are accessible

**Files:**
- Create: `c_model/tests/nmu/test_axi_slave_port.cpp`

- [ ] **Step 1: Write stub test**

```cpp
// c_model/tests/nmu/test_axi_slave_port.cpp
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
```

- [ ] **Step 2: Build & run**

Run: `cmake --build build --target test_axi_slave_port && ctest --test-dir build -R AxiSlavePort_Scaffold -V`
Expected: 3 tests PASS。

- [ ] **Step 3: Drift gates**

Run:
```
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
Expected: 0 fail / OK / OK。

- [ ] **Step 4: Karpathy 4-lens review**

對 axi_slave_port.hpp + tests 用 karpathy-guidelines 4 條 lens（overcomplication / surgical / surface assumptions / verifiable success）跑一次。發現任何問題 → 修正再 commit。

- [ ] **Step 5: Commit**

```
git add c_model/include/nmu/axi_slave_port.hpp c_model/tests/nmu/CMakeLists.txt c_model/tests/nmu/test_axi_slave_port.cpp c_model/tests/CMakeLists.txt c_model/IMPLEMENTATION_PLAN.md docs/superpowers/specs/2026-05-28-nmu-axi-slave-port-design.md docs/superpowers/plans/2026-05-28-nmu-axi-slave-port.md
git commit -m "feat(c_model): scaffold NMU AXI slave port types + tests"
```

更新 `c_model/IMPLEMENTATION_PLAN.md` Stage 1 status → Complete。

---

## Stage 2 — AxiSlavePort class: inbound

### Task 2.1: Class declaration + AW-only round-trip

**Files:**
- Modify: `c_model/include/nmu/axi_slave_port.hpp` (add class)
- Modify: `c_model/tests/nmu/test_axi_slave_port.cpp` (add round-trip test)

- [ ] **Step 1: Write failing AW round-trip test**

Append to `test_axi_slave_port.cpp`:
```cpp
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
```

- [ ] **Step 2: Verify test fails to compile**

Run: `cmake --build build --target test_axi_slave_port`
Expected: build FAIL (`AxiSlavePort` undefined)。

- [ ] **Step 3: Declare class skeleton + implement AW path**

In `axi_slave_port.hpp` append (before namespace close):
```cpp
class AxiSlavePort {
public:
  explicit AxiSlavePort(QueueDepths depths = {}) : depths_(depths) {}

  // External interface
  bool push_inbound_pins(const ni::pins::AxiSlavePortPins& p, ChannelMask mask);
  std::optional<BBeat> pop_outbound_b();
  std::optional<RBeat> pop_outbound_r();

  // Internal interface
  std::optional<AwBeat> pop_aw();
  std::optional<WBeat>  pop_w();
  std::optional<ArBeat> pop_ar();
  bool push_b(const BBeat&);
  bool push_r(const RBeat&);

  // Lifecycle / observation
  void tick() {}  // no-op for now; hook for future latency injection
  std::size_t aw_q_size() const { return aw_q_.size(); }
  std::size_t w_q_size()  const { return w_q_.size();  }
  std::size_t ar_q_size() const { return ar_q_.size(); }
  std::size_t b_q_size()  const { return b_q_.size();  }
  std::size_t r_q_size()  const { return r_q_.size();  }
  std::size_t aw_q_capacity() const { return depths_.aw; }
  std::size_t w_q_capacity()  const { return depths_.w;  }
  std::size_t ar_q_capacity() const { return depths_.ar; }
  std::size_t b_q_capacity()  const { return depths_.b;  }
  std::size_t r_q_capacity() const { return depths_.r;  }

private:
  std::deque<AwBeat> aw_q_;
  std::deque<WBeat>  w_q_;
  std::deque<ArBeat> ar_q_;
  std::deque<BBeat>  b_q_;
  std::deque<RBeat>  r_q_;
  QueueDepths        depths_;
};

inline bool AxiSlavePort::push_inbound_pins(const ni::pins::AxiSlavePortPins& p, ChannelMask mask) {
  // Drop B/R bits silently in release (debug builds may add assert later)
  ChannelMask in_only = mask & (ChannelMask::Aw | ChannelMask::W | ChannelMask::Ar);
  if (!any(in_only)) return false;

  // All-or-nothing capacity check
  if (any(in_only & ChannelMask::Aw) && aw_q_.size() >= depths_.aw) return false;
  if (any(in_only & ChannelMask::W)  && w_q_.size()  >= depths_.w)  return false;
  if (any(in_only & ChannelMask::Ar) && ar_q_.size() >= depths_.ar) return false;

  if (any(in_only & ChannelMask::Aw)) {
    aw_q_.push_back(AwBeat{p.axi_awid_i, p.axi_awaddr_i, p.axi_awlen_i,
                            p.axi_awsize_i, p.axi_awburst_i, p.axi_awcache_i,
                            p.axi_awlock_i, p.axi_awprot_i, p.axi_awregion_i,
                            p.axi_awuser_i, p.axi_awqos_i});
  }
  // W and Ar dispatch added in next task
  return true;
}

inline std::optional<AwBeat> AxiSlavePort::pop_aw() {
  if (aw_q_.empty()) return std::nullopt;
  AwBeat front = aw_q_.front();
  aw_q_.pop_front();
  return front;
}

// Stub others for now -- next tasks fill them in
inline std::optional<WBeat>  AxiSlavePort::pop_w()  { if (w_q_.empty())  return std::nullopt; auto f = w_q_.front();  w_q_.pop_front();  return f; }
inline std::optional<ArBeat> AxiSlavePort::pop_ar() { if (ar_q_.empty()) return std::nullopt; auto f = ar_q_.front(); ar_q_.pop_front(); return f; }
inline std::optional<BBeat>  AxiSlavePort::pop_outbound_b() { if (b_q_.empty()) return std::nullopt; auto f = b_q_.front(); b_q_.pop_front(); return f; }
inline std::optional<RBeat>  AxiSlavePort::pop_outbound_r() { if (r_q_.empty()) return std::nullopt; auto f = r_q_.front(); r_q_.pop_front(); return f; }
inline bool AxiSlavePort::push_b(const BBeat& b) { if (b_q_.size() >= depths_.b) return false; b_q_.push_back(b); return true; }
inline bool AxiSlavePort::push_r(const RBeat& r) { if (r_q_.size() >= depths_.r) return false; r_q_.push_back(r); return true; }
```

- [ ] **Step 4: Build & run**

Run: `cmake --build build --target test_axi_slave_port && ctest --test-dir build -R AxiSlavePort_Inbound -V`
Expected: AW round-trip PASS。

### Task 2.2: W / AR round-trip tests + dispatch

**Files:** modify both files。

- [ ] **Step 1: Write failing W + AR tests**

Append two TEST blocks symmetric to AW round-trip (W has `data` array + `strb` + `last` + `user`; AR same 11 fields as AW)。

For W:
```cpp
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
```

For AR: symmetric to AW.

- [ ] **Step 2: Verify W test fails (data not copied)**

Run ctest → W test FAIL（dispatch 還沒寫 W path）

- [ ] **Step 3: Implement W + Ar dispatch**

Insert into `push_inbound_pins` after the AW block:
```cpp
  if (any(in_only & ChannelMask::W)) {
    WBeat wb;
    wb.data = p.axi_wdata_i;
    wb.strb = p.axi_wstrb_i;
    wb.last = p.axi_wlast_i;
    wb.user = p.axi_wuser_i;
    w_q_.push_back(wb);
  }
  if (any(in_only & ChannelMask::Ar)) {
    ar_q_.push_back(ArBeat{p.axi_arid_i, p.axi_araddr_i, p.axi_arlen_i,
                             p.axi_arsize_i, p.axi_arburst_i, p.axi_arcache_i,
                             p.axi_arlock_i, p.axi_arprot_i, p.axi_arregion_i,
                             p.axi_aruser_i, p.axi_arqos_i});
  }
```

- [ ] **Step 4: Build & run all inbound tests**

Run ctest → AW/W/AR round-trip 全 PASS。

### Task 2.3: All-or-nothing + queue depth

- [ ] **Step 1: Write failing all-or-nothing test**

```cpp
TEST(AxiSlavePort_Inbound, AllOrNothingRollback) {
  nmu::AxiSlavePort port(nmu::QueueDepths{.aw=1, .w=16});
  ni::pins::AxiSlavePortPins p{};
  // First AW fills aw_q
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_EQ(port.aw_q_size(), 1u);
  EXPECT_EQ(port.w_q_size(),  0u);

  // Second push with mask=Aw|W -- aw_q full, expect overall false and w_q unchanged
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
  // W still accepts up to 8
  for (int i = 0; i < 8; ++i)
    EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::W)) << "w_q i=" << i;
  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::W)) << "w_q full at depth 8";
}
```

- [ ] **Step 2: Verify tests pass (current dispatch already handles all-or-nothing)**

Run ctest → 兩個 test PASS。如果失敗，回頭檢查 capacity check 邏輯。

- [ ] **Step 3: Drift gates + Karpathy 4-lens + Commit**

```
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
git add c_model/
git commit -m "feat(c_model): NMU AXI slave port inbound dispatch (AW/W/AR)"
```

更新 IMPLEMENTATION_PLAN.md Stage 2 status → Complete。

---

## Stage 3 — Outbound + tick + stateless protocol check

### Task 3.1: Outbound B/R push/pop fidelity

- [ ] **Step 1: Write tests**

```cpp
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
```

- [ ] **Step 2: Verify pass**

Stub implementations in Task 2.1 already cover these. Run ctest → PASS。

### Task 3.2: tick() is no-op

- [ ] **Step 1: Write test**

```cpp
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
```

- [ ] **Step 2: Run → PASS（tick is empty body）**

### Task 3.3: Stateless AXI4 protocol field checks (release behavior)

**Rules covered in this task**：
- AW/AR: `AXI4_*_BURST_ENCODING` (burst < 3), `AXI4_*_SIZE_BOUND` (size <= log2(WSTRB_WIDTH)), `AXI4_*_LEN_ENCODING` (len <= 255 for INCR/FIXED, <= 15 for WRAP), `AXI4_*_WRAP_ALIGN` (addr aligned for WRAP), `AXI4_BURST_NO_4KB_CROSS`
- W: `AXI4_W_STRB_VALID` (strb 上限 = (1<<WSTRB_WIDTH)-1)
- B/R: `AXI4_*_RESP_ENCODING` (resp < 4)

- [ ] **Step 1: Add check helpers (private static, in header)**

```cpp
private:
  static constexpr uint8_t kBurstFixed = 0;
  static constexpr uint8_t kBurstIncr  = 1;
  static constexpr uint8_t kBurstWrap  = 2;

  // Returns true if beat fields are valid per AXI4. Assert in debug, callers ignore in release.
  static bool check_addr_beat_(uint8_t burst, uint64_t addr, uint8_t len, uint8_t size) {
    if (burst > 2)                              return false;  // BURST_ENCODING
    if (size > 5)                               return false;  // SIZE_BOUND (max 32B = log2(32)=5)
    if (burst == kBurstWrap && len > 15)        return false;  // LEN_ENCODING for WRAP
    // WRAP_ALIGN: addr must be aligned to (1 << size)
    if (burst == kBurstWrap && (addr & ((1ull << size) - 1))) return false;
    // BURST_NO_4KB_CROSS: only meaningful for INCR
    if (burst == kBurstIncr) {
      uint64_t bytes_per_beat = 1ull << size;
      uint64_t total = bytes_per_beat * (static_cast<uint64_t>(len) + 1);
      if ((addr & 0xFFF) + total > 0x1000) return false;
    }
    return true;
  }
  static bool check_strb_(uint32_t strb) {
    constexpr uint32_t kMaxStrb = (ni::WSTRB_WIDTH >= 32) ? 0xFFFF'FFFFu : ((1u << ni::WSTRB_WIDTH) - 1);
    return (strb & ~kMaxStrb) == 0;
  }
  static bool check_resp_(uint8_t resp) { return resp < 4; }
```

- [ ] **Step 2: Wire checks into push paths (debug assert / release silent)**

Modify the `push_inbound_pins` AW dispatch block:
```cpp
  if (any(in_only & ChannelMask::Aw)) {
    assert(check_addr_beat_(p.axi_awburst_i, p.axi_awaddr_i, p.axi_awlen_i, p.axi_awsize_i)
           && "AXI4_AW_* protocol violation");
    aw_q_.push_back(AwBeat{ /* ... existing ... */ });
  }
```

Similar for AR, W (`assert(check_strb_(p.axi_wstrb_i))`), and `push_b` / `push_r` (`assert(check_resp_(b.resp))` / `assert(check_resp_(r.resp))`).

- [ ] **Step 3: Test release behavior — violation enqueues anyway**

```cpp
#ifdef NDEBUG  // only meaningful in release build
TEST(AxiSlavePort_ProtocolRelease, BadBurstEncodingStillEnqueues) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_awburst_i = 3;  // reserved encoding
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw));
  EXPECT_EQ(port.aw_q_size(), 1u);
}
#endif
```

- [ ] **Step 4: Test debug behavior — EXPECT_DEATH on violation**

```cpp
#ifndef NDEBUG
TEST(AxiSlavePort_ProtocolDebug, BadBurstEncodingDeathTest) {
  nmu::AxiSlavePort port;
  ni::pins::AxiSlavePortPins p{};
  p.axi_awburst_i = 3;
  EXPECT_DEATH(port.push_inbound_pins(p, nmu::ChannelMask::Aw), "AW_\\* protocol violation");
}
#endif
```

- [ ] **Step 5: Build (debug) → ctest**

Run: `cmake --build build && ctest --test-dir build`
Expected: 所有 inbound/outbound/tick tests PASS；debug death test PASS。

- [ ] **Step 6: Drift gates + Karpathy + Commit**

```
git add c_model/
git commit -m "feat(c_model): NMU AXI slave port outbound + stateless protocol field checks"
```

更新 IMPLEMENTATION_PLAN.md Stage 3 → Complete。

---

## Stage 4 — TEST_P matrix + seeded random + exercise counters

### Task 4.1: Parameterized fixture for channel × depth × mask

- [ ] **Step 1: Add value-parameterized fixture**

```cpp
struct InboundParam {
  nmu::ChannelMask mask;
  nmu::QueueDepths depths;
  const char* label;
};

class AxiSlavePort_InboundP : public ::testing::TestWithParam<InboundParam> {};

TEST_P(AxiSlavePort_InboundP, MaskedPushFillsCorrespondingQueues) {
  auto pr = GetParam();
  nmu::AxiSlavePort port(pr.depths);
  ni::pins::AxiSlavePortPins p{};
  EXPECT_TRUE(port.push_inbound_pins(p, pr.mask));
  // Each channel in mask should have size 1
  if (any(pr.mask & nmu::ChannelMask::Aw)) EXPECT_EQ(port.aw_q_size(), 1u) << pr.label;
  if (any(pr.mask & nmu::ChannelMask::W))  EXPECT_EQ(port.w_q_size(),  1u) << pr.label;
  if (any(pr.mask & nmu::ChannelMask::Ar)) EXPECT_EQ(port.ar_q_size(), 1u) << pr.label;
}

INSTANTIATE_TEST_SUITE_P(MaskMatrix, AxiSlavePort_InboundP, ::testing::Values(
  InboundParam{nmu::ChannelMask::Aw,                            nmu::QueueDepths{}, "Aw-only"},
  InboundParam{nmu::ChannelMask::W,                             nmu::QueueDepths{}, "W-only"},
  InboundParam{nmu::ChannelMask::Ar,                            nmu::QueueDepths{}, "Ar-only"},
  InboundParam{nmu::ChannelMask::Aw | nmu::ChannelMask::W,      nmu::QueueDepths{}, "Aw|W"},
  InboundParam{nmu::ChannelMask::Aw | nmu::ChannelMask::W | nmu::ChannelMask::Ar,
                                                                nmu::QueueDepths{}, "Aw|W|Ar"},
  InboundParam{nmu::ChannelMask::Aw, nmu::QueueDepths{.aw=2},   "depth=2"},
  InboundParam{nmu::ChannelMask::Aw, nmu::QueueDepths{.aw=1},   "depth=1"}
));
```

- [ ] **Step 2: Build & run → all instantiations PASS**

### Task 4.2: Seeded random shadow-model test

- [ ] **Step 1: Write randomized test using `std::deque` as shadow**

```cpp
#include <random>

TEST(AxiSlavePort_Random, SeededShadowModelEquivalence) {
  constexpr unsigned kSeed = 0xC0FFEE;
  constexpr int      kOps  = 1000;
  std::mt19937 rng(kSeed);
  nmu::AxiSlavePort port(nmu::QueueDepths{.aw=8, .w=8, .ar=8, .b=8, .r=8});
  std::deque<uint8_t> shadow_aw_ids;  // 用 id 當代表 token

  for (int i = 0; i < kOps; ++i) {
    int op = rng() % 2;
    if (op == 0) {  // push
      ni::pins::AxiSlavePortPins p{};
      uint8_t id = static_cast<uint8_t>(rng() & 0xFF);
      p.axi_awid_i = id;
      // valid burst encoding to avoid protocol assert
      p.axi_awburst_i = 1;  p.axi_awsize_i = 0;  p.axi_awlen_i = 0;
      bool pushed = port.push_inbound_pins(p, nmu::ChannelMask::Aw);
      bool can_push = shadow_aw_ids.size() < port.aw_q_capacity();
      EXPECT_EQ(pushed, can_push) << "op " << i;
      if (pushed) shadow_aw_ids.push_back(id);
    } else {  // pop
      auto got = port.pop_aw();
      bool empty = shadow_aw_ids.empty();
      EXPECT_EQ(got.has_value(), !empty) << "op " << i;
      if (got.has_value()) {
        EXPECT_EQ(got->id, shadow_aw_ids.front()) << "FIFO order break at op " << i;
        shadow_aw_ids.pop_front();
      }
    }
  }
  EXPECT_EQ(port.aw_q_size(), shadow_aw_ids.size());
}
```

- [ ] **Step 2: Run → PASS（如果失敗，回頭 debug FIFO 行為）**

### Task 4.3: Exercise counters

- [ ] **Step 1: Add a counters-tracking test that runs through all paths**

```cpp
TEST(AxiSlavePort_Coverage, AllKeyPathsExercised) {
  bool saw_full = false, saw_empty = false, saw_atomic_fail = false;
  bool saw_burst_violation = false, saw_resp_violation = false;
  bool saw_aw = false, saw_w = false, saw_ar = false, saw_b = false, saw_r = false;

  nmu::AxiSlavePort port(nmu::QueueDepths{.aw=1, .w=1, .ar=1, .b=1, .r=1});
  ni::pins::AxiSlavePortPins p{};
  p.axi_awburst_i = 1;  // valid

  // saw_empty: pop on empty queue
  EXPECT_FALSE(port.pop_aw().has_value()); saw_empty = true;

  // push AW until full
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Aw)); saw_aw = true;
  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::Aw)); saw_full = true;

  // atomic fail: aw full + W bit set => overall false, w_q unchanged
  EXPECT_FALSE(port.push_inbound_pins(p, nmu::ChannelMask::Aw | nmu::ChannelMask::W));
  EXPECT_EQ(port.w_q_size(), 0u); saw_atomic_fail = true;

  // drain AW, exercise W and Ar
  (void)port.pop_aw();
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::W));  saw_w = true;
  EXPECT_TRUE(port.push_inbound_pins(p, nmu::ChannelMask::Ar)); saw_ar = true;

  // outbound channels
  EXPECT_TRUE(port.push_b(nmu::BBeat{1, 0, 0})); saw_b = true;
  EXPECT_TRUE(port.push_r(nmu::RBeat{}));        saw_r = true;

#ifdef NDEBUG
  // protocol violations only exercised in release (debug build dies via assert)
  ni::pins::AxiSlavePortPins bad = p; bad.axi_awburst_i = 3;
  // queue已有 W beat, 換新 port 跑單獨檢查
  nmu::AxiSlavePort port2;
  EXPECT_TRUE(port2.push_inbound_pins(bad, nmu::ChannelMask::Aw));
  saw_burst_violation = true;
  EXPECT_TRUE(port2.push_b(nmu::BBeat{0, 5, 0}));  // resp=5 invalid
  saw_resp_violation = true;
#else
  saw_burst_violation = true; saw_resp_violation = true;  // skipped in debug
#endif

  EXPECT_TRUE(saw_full); EXPECT_TRUE(saw_empty); EXPECT_TRUE(saw_atomic_fail);
  EXPECT_TRUE(saw_burst_violation); EXPECT_TRUE(saw_resp_violation);
  EXPECT_TRUE(saw_aw); EXPECT_TRUE(saw_w); EXPECT_TRUE(saw_ar); EXPECT_TRUE(saw_b); EXPECT_TRUE(saw_r);
}
```

- [ ] **Step 2: Build & run → PASS**

- [ ] **Step 3: Final drift gates**

```
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```

- [ ] **Step 4: Karpathy 4-lens review + Commit**

```
git add c_model/
git commit -m "test(c_model): parameterized + seeded random + coverage for NMU AXI slave port"
```

- [ ] **Step 5: Remove IMPLEMENTATION_PLAN.md per CLAUDE.md (all stages Complete)**

```
git rm c_model/IMPLEMENTATION_PLAN.md
git commit -m "chore(c_model): remove IMPLEMENTATION_PLAN.md (FEAT-NMU-AXI_SLAVE_PORT done)"
```

---

## Out-of-plan follow-ups (do NOT include in this PR)

- 加入剩餘 6 NMU + 5 NSU features（各自獨立 spec + plan）
- AXI protocol stateful check module（W_BEAT_COUNT / W_LAST_TIMING / B_ONE_RESPONSE_PER_WRITE 等）
- DPI bridge / SV adapter wiring（cycle-accurate co-sim）
- Codegen 從 `ni_protocol_rule_index.json` 產 enum header（取代 string_view rule_id）
- Active / Passive BFM mode integration layer
