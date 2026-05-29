# NMU AXI Slave Port — c_model Design

> 2026-05-28 · brainstorm 收斂結果，feature = `FEAT-NMU-AXI_SLAVE_PORT`
>
> Layer B / Stage 2 第一個 feature class。此 doc 範圍只此一個 feature；其餘 NMU 6 + NSU 5 features 各自獨立 spec。

---

## Purpose

為 c_model NMU 建立 AXI slave port 的 beat-buffer 層 — 把 spec-generated pin payload (`ni::pins::AxiSlavePortPins`) 在外、per-channel beat queue 在內，給未來 ADDR_TRANS / PACKETIZE / DEPACKETIZE features 自然的 channel API，同時保持 DPI / SV adapter 介面與 spec 零 drift。

---

## Scope

**In scope**:
- 接受 inbound AW / W / AR beat (從 DPI / SV adapter / C++ test)
- 接受 outbound B / R beat (從未來 features，經 RoB)
- per-channel bounded queue (各自獨立深度)
- stateless AXI4 protocol field check on push (debug assert / release silent)
- mode = ALWAYS ACTIVE（不在這個 feature 處理 active/passive）

**Out of scope**:
- AXI4 outstanding / ID matching / ordering → ROB feature
- Stateful intra-burst rule check (`W_BEAT_COUNT` / `*_LAST_TIMING` 等) → 未來 dedicated AXI protocol check module
- Burst expansion / packetize → PACKETIZE feature
- Violation accumulation list / CSR write / IRQ → test infrastructure + integration layer
- Active / Passive BFM mode → 未來 integration layer
- Async clock domain crossing (`aclk_i` vs `noc_clk_i`) — **see "Scope deviation" 段**
- valid/ready handshake FSM — spec 已將 handshake 抽離 pin struct，本 layer 不模擬

### Scope deviation from spec inventory

`spec_validate/authored/ni_function_blocks.json` 對本 feature 定義為 "AXI4 full-protocol slave port ... with built-in async data boundary crossing between aclk_i and noc_clk_i"。本 design 不在這層模 CDC，理由：c_model 為 untimed behavior model，無 cycle / clock 概念，CDC 在此層退化為 identity；cycle-accurate co-sim 時由 RTL 側負責 CDC。若未來需要 c_model 內模 CDC latency/metastability injection，作為獨立 feature 增量處理，不回頭擴張本 class。

---

## Architecture

**Position**: c_model NMU 的 edge-facing class，僅在 NMU 全部跑 c_model 時實例化。hot-swap 邊界在 NoC flit link（不在 AXI port），故 NMU = RTL 時整顆 NMU（含 AXI port）也是 RTL，本 class 不被使用。

**Boundary**:
- 對外（DPI / SV adapter / user test code）：`ni::pins::AxiSlavePortPins` (spec-elaborated, 來自 `spec_validate/include/ni_signals.h`)
- 對內（其他 NMU features）：per-channel beat (`AwBeat` / `WBeat` / `ArBeat` / `BBeat` / `RBeat`)
- 中間 conversion 集中於 private helper (`dispatch_` / `gather_`)

**Mode**: 永遠 ACTIVE。不引入 `BfmMode` enum。Active/Passive 由未來 integration layer 統一處理。

**Tick model**: explicit `tick()` 但目前 no-op（buffer 操作 push/pop 即時生效）；保留 hook 給未來 latency injection / rate limiting。

**File layout**:
- `c_model/include/nmu/axi_slave_port.hpp` (header-only)
- `c_model/tests/nmu/test_axi_slave_port.cpp`

**Dependencies**:
- `spec_validate/include/ni_signals.h` (`ni::pins::AxiSlavePortPins`)
- `spec_validate/include/ni_flit_constants.h` (AXI_*_WIDTH 常數)
- 不依賴 Layer A 的 `Flit` / `RegisterFile`

---

## Components

**對內 beat types**（c_model 自由設計，per-channel）：

```cpp
namespace ni::cmodel::nmu {

struct AwBeat { uint8_t id; uint64_t addr; uint8_t len, size, burst,
                          cache, lock, prot, region, user, qos; };
struct WBeat  { std::array<uint8_t, ni::AXI_DATA_WIDTH/8> data;
                uint32_t strb; uint8_t last, user; };
struct ArBeat { /* 同 AwBeat 結構 */ };
struct BBeat  { uint8_t id, resp, user; };
struct RBeat  { uint8_t id; std::array<uint8_t,32> data;
                uint8_t resp, last, user; };

enum class ChannelMask : uint8_t {
  None = 0, Aw = 1<<0, W = 1<<1, Ar = 1<<2, B = 1<<3, R = 1<<4,
};

struct QueueDepths { std::size_t aw=16, w=16, ar=16, b=16, r=16; };  // [TBD] default 16

}
```

**主 class**：

```cpp
class AxiSlavePort {
public:
  explicit AxiSlavePort(QueueDepths depths = {});

  // === External interface (DPI / SV adapter / user test code) ===
  // Inbound：adapter sample pin → 包成 AxiSlavePortPins → push
  bool push_inbound_pins(const ni::pins::AxiSlavePortPins& p, ChannelMask mask);
  // Outbound：B / R 完全獨立 channel，adapter 各自 pop 後驅動對應 pins
  std::optional<BBeat> pop_outbound_b();
  std::optional<RBeat> pop_outbound_r();

  // === Internal interface (future NMU features) ===
  // Inbound consumption：ADDR_TRANS / PACKETIZE 從這裡取 beat
  std::optional<AwBeat> pop_aw();
  std::optional<WBeat>  pop_w();
  std::optional<ArBeat> pop_ar();
  // Outbound injection：B-RoB / R-RoB 把 beat push 進來
  bool push_b(const BBeat&);
  bool push_r(const RBeat&);

  // === Lifecycle / observation ===
  void tick();   // 目前 no-op，hook 給未來 latency injection
  std::size_t aw_q_size() const; std::size_t aw_q_capacity() const;
  // ... 同 W / AR / B / R

private:
  void dispatch_(const ni::pins::AxiSlavePortPins&, ChannelMask);

  std::deque<AwBeat> aw_q_;  std::deque<WBeat> w_q_;  std::deque<ArBeat> ar_q_;
  std::deque<BBeat>  b_q_;   std::deque<RBeat> r_q_;
  QueueDepths        depths_;
};
```

**Push semantic**:
- `push_inbound_pins(pins, mask)` 為 **all-or-nothing**：先檢查 mask 涵蓋的所有 channel 都有空，全有空才真 push；任一滿即整體 false。
- `push_inbound_pins` 的 `mask` 僅允許 input channel bit (Aw/W/Ar)；含 B/R bit 在 debug build assert、release ignore。

**Pop outbound semantic**:
- `pop_outbound_b()` / `pop_outbound_r()` 各自獨立，無 arbitration。Adapter 取 beat 後自行 pack 進對應 `axi_b*_o` / `axi_r*_o` pins（在 RTL pin 上 B/R 是兩組獨立 wire，可同 cycle 並存）。
- B/R 不在這層 merge 成 resp channel；merge 由 NoC interface 後續的 mux 處理（out of scope）。

**規模估計**：全部 inline 在 hpp，~150 行。

### OSS alignment

「per-channel payload struct + valid/ready 在 payload 之外 + per-channel queue」是業界一致 pattern：

- **fpganinja/taxi** (`taxi_axi_if.sv`)：AW 11 fields (id/addr/len/size/burst/lock/cache/prot/qos/region/user)，valid/ready 在 payload 外；FIFO depth 為 compile-time integer param
- **pulp-platform/axi** (`typedef.svh`)：per-channel packed struct，AW 多一個 `atop` field 變 12（AXI4+ATOP 擴展）
- **NVlabs/matchlib** (`axi4.h`)：C++ payload struct，AW/AR 共用 `AddrPayload`；config 透過 typedef 控制 unused sideband 為 zero-width
- **Minres/SystemC-Components** (`axi4_target.h`)：pin-level target 內部 `aw_data` struct + `aw_que` queue

我們 `AwBeat` 11 fields 對齊 taxi。ATOP 故意不加 — 目前 `spec_validate/include/ni_signals.h` 的 `AxiSlavePortPins` 無 `axi_awatop_i`，待 authored JSON 增列再同步擴展 `AwBeat`。

---

## Data flow

**Inbound**（master → c_model → 未來 NMU features）

```
[AXI master] --(SV adapter samples)--> push_inbound_pins(pins, mask)
                                              |
                                              v
                                  AxiSlavePort.dispatch_()
                                              |
                                              v
                            aw_q_ / w_q_ / ar_q_ (各自獨立)
                                              |
                                              v
                              pop_aw / pop_w / pop_ar
                                              |
                                              v
                        [ADDR_TRANS / PACKETIZE, future]
```

**Outbound**（未來 NMU features → c_model → master）

```
[NoC] -> [DEPACKETIZE(B)] -> [B-RoB] --push_b--> b_q_  --pop_outbound_b--> [adapter drives axi_b*_o] -> [AXI master]
                                                  (AxiSlavePort)
[NoC] -> [DEPACKETIZE(R)] -> [R-RoB] --push_r--> r_q_  --pop_outbound_r--> [adapter drives axi_r*_o] -> [AXI master]
```

- B 與 R 為兩條完全獨立的 channel，**本層不 merge、不 arbitrate**
- Adapter 各自 pop 後驅動 `axi_b*_o` / `axi_r*_o` pins（pin 上是兩組獨立 wire，同 cycle 並存合法）
- Merge B/R 成 resp channel 是 NoC interface 後續的 mux 做的事（out of scope）

---

## Error handling

**Mechanics**:
- Queue 滿：`push_*` 回 `false`（backpressure 機制，非 error）
- Empty pop：`pop_*` 回 `nullopt`（非 error）

**Stateless AXI4 protocol field check on push**（仿 `Flit` 模式：debug assert / release silent accept）：

| Channel | 檢查 rule（spec rule_id） |
|---------|---------------------------|
| AW / AR | `AXI4_*_LEN_ENCODING` / `AXI4_*_SIZE_BOUND` / `AXI4_*_BURST_ENCODING` / `AXI4_*_WRAP_ALIGN` / `AXI4_BURST_NO_4KB_CROSS` |
| W       | `AXI4_W_STRB_VALID` / `AXI4_W_STRB_SPARSE_LEGAL` |
| B / R   | `AXI4_*_RESP_ENCODING` |

Release-build 行為與 RTL 一致（不正當輸入照進 queue，下游自己處理）。

**故意排除**:
- Stateful intra-burst checks (`*_BEAT_COUNT` / `*_LAST_TIMING` / `B_ONE_RESPONSE_PER_WRITE`) — dedicated protocol check module 負責
- ID matching / ordering rules — ROB feature 負責
- Handshake rules (`*_VALID_STABLE` / `*_VALID_NO_WAIT`) — Channel-level 無對應信號
- Violation accumulation list — 測試環境負責
- CSR `ERR_STATUS` 更新 + `irq_o` — 未來 integration layer 負責

---

## Testing

**Framework**: GoogleTest，仿 Layer A 風格 (`test_flit.cpp` / `test_register_file.cpp`)

**File**: `c_model/tests/nmu/test_axi_slave_port.cpp`

| # | Category | 範例 |
|---|---------|------|
| 1 | Per-channel push/pop fidelity | 構造 `AxiSlavePortPins` 設 AW 欄位 → `push_inbound_pins(_, Aw)` → `pop_aw` → 11 欄位逐一比對 |
| 2 | All-or-nothing push | `mask=Aw\|W`、`aw_q` 已滿 → push 整體 false，`w_q` 不變 |
| 3 | QueueDepths 各 channel 獨立 | `depths={aw:2,...}` → 第 3 個 AW false，但 W 仍可推 |
| 4 | Outbound pop B/R 獨立 | `push_b` + `push_r` → `pop_outbound_b` / `pop_outbound_r` 各拿到對應 beat；單推一邊 → 另一邊 nullopt；都沒推 → 兩邊都 nullopt |
| 5 | tick() no-op | snapshot 內部 size → tick() → 不變 |
| 6 | DPI boundary 相容 | `ni::pins::AxiSlavePortPins` round-trip 無欄位失真 |
| 7 | Stateless protocol violation (release build) | bad `awburst=3` → push 仍 true、beat 進 queue |
| 8 | Stateless protocol violation (debug build) | bad `awburst=3` → `EXPECT_DEATH` |
| 9 | **Parameterized fixture matrix** (`TEST_P`) | `channel ∈ {Aw,W,Ar,B,R}` × `depth ∈ {1, 2, default-16}` × `mask ∈ {single, Aw\|W, Aw\|W\|Ar}` × `build ∈ {debug, release}`，覆蓋面均勻而非 ad-hoc |
| 10 | **Seeded random queue model test** | 固定 seed RNG 跑 N 次 push/pop 操作；對照 `std::deque` shadow model：滿時 push 必 false、非空時 pop 順序 FIFO、空時 nullopt |
| 11 | **Exercise counters** | 測試結尾 `EXPECT_TRUE` 一組 boolean：見過 full / 見過 empty / 見過 all-or-nothing rollback / 見過 each protocol rule / 見過 each channel — 防止 silent coverage 退化 |

**OSS test pattern 對齊**：
- Parameterized fixture：對齊 taxi (pytest params) / cocotbext-axi (`TestFactory`)
- Shadow model + seeded RNG：對齊 matchlib / libsystemctlm-soc random traffic
- Exercise counters：matchlib `COV_ENABLE` 的輕量版本

**不在此 feature 的測試**:
- AXI4 stateful protocol checks
- End-to-end NMU flow（待 ADDR_TRANS / PACKETIZE / DEPACKETIZE）
- DPI co-sim（待 SV adapter）

**Drift gate**（每 commit）:
```
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q
```

---

## [TBD]

- ~~`QueueDepths` default = 16~~ **RESOLVED**：採 16；taxi 用 32（`WRITE_FIFO_DEPTH=32`/`READ_FIFO_DEPTH=32`），16 對 c_model untimed model 已足夠；spec 後續定 `AXI_*_FIFO_DEPTH` 常數時對齊
- Protocol rule_id 用於 debug assert / log message — 短期 `string_view` 字面值；長期建議 codegen 從 `ni_protocol_rule_index.json` 產 enum header，超出本 feature 範圍
- `tick()` future use case（latency injection / rate limiting）— 等實際需求出現再 design
