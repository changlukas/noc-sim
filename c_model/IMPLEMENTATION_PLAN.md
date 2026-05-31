# IMPLEMENTATION_PLAN — Pure AXI Subsystem (Stage 2 Phase B)

> Stage tracker per CLAUDE.md。詳版 task-by-task plan: `docs/superpowers/plans/2026-05-31-pure-axi-subsystem-phase-b.md`
>
> Spec: `docs/superpowers/specs/2026-05-31-pure-axi-subsystem-phase-b-design.md`
>
> Phase A 78 tests must stay green at every commit.
>
> Remove this file when all stages complete.

---

## Stage B-1: Sparse WSTRB infrastructure

**Goal**: 加 YAML `strb_file` field 解析 + AxiMaster 載入 + WriteResult 加 `strb_per_beat` field + Scoreboard 用 per-beat strb 做 byte-merge update。Phase A 既有 callsite 傳 full-strb vector 維持相容。

**Success Criteria**:
- `test_scenario_parser` strb_file 5 個新 case 全綠（empty / valid / line-count mismatch throw / missing master throw / invalid hex throw）
- `test_scoreboard` sparse byte-merge 1 個新 case 全綠
- 全部 78 Phase A tests 仍綠（signature change 後 callsite 更新）

**Status**: Not Started

---

## Stage B-2: Unaligned start address

**Goal**: AxiMaster 計算 first beat WSTRB（addr 未對齊到 `1<<size` 時 lower bits 設 0），data 對到正確 byte lane；AxiSlave 接收 sparse strb 正確 forward；parser 接受 INCR unaligned。

**Success Criteria**:
- `test_axi_master` 5 個 unaligned addr unit tests 全綠
- `test_axi_slave` 2 個 sparse forward tests 全綠
- Integration fixture `unaligned_start.yaml` 1 個 instance 全綠（diff + scoreboard 雙驗）

**Status**: Not Started

---

## Stage B-3a: Bus semantics refactor (lane-positioned)

**Goal**: c_model AXI bus 採 AXI4 lane-positioned semantics (RTL-faithful)。`byte_lane = bus_addr & (DATA_BYTES-1)`；narrow / unaligned 的 data 放在 `data[byte_lane..byte_lane+bpb-1]`，strb 對應位置 set。Memory.perform_write_/read_ + Scoreboard byte-merge + AxiMaster read accumulator 都改 lane-positioned。**只改 algorithm，不加 feature**。

**Success Criteria**:
- `Memory::perform_write_` 改用 `storage[(req.addr & ~(DATA_BYTES-1)) - base + lane] = data[lane]`
- `Memory::perform_read_` 回 lane-positioned resp.data
- `AxiMaster` read accumulator 從 `data[byte_lane..]` 拉
- `Scoreboard::handle_write_completed` 重算 byte 位置 (per-beat bpb + per-beat addr aware)
- **Phase A 78 tests + B-1 7 + B-2 7 = 92/92 全綠** (若 Phase A test 預設 compact bus，更新預期值)

**Status**: Not Started

---

## Stage B-3b: AxiMaster narrow W push + tests + fixtures

**Goal**: 在 B-3a refactor 後加 narrow feature 本體：AxiMaster narrow byte-lane W push、4 個 sizes 0/1/2/3 unit tests、1 個 AxiSlave narrow forward test、2 個 integration fixtures。同時更新 B-2 `INSTANTIATE_TEST_SUITE_P(UnalignedCases, ...)` 預期 strb 值 (size<5 的 3 個 case 變 lane-positioned 公式 result)。

**Success Criteria**:
- `test_axi_master` narrow size 0/1/2/3 + size=4 共 4-5 個 unit tests 全綠
- `test_axi_slave` `NarrowTransferForwardedToMemory` 全綠
- Integration fixtures `narrow_transfer_size2.yaml`、`narrow_transfer_size0.yaml` 全綠
- B-2 `UnalignedCases` 5 個 case 全綠 (Size4_Off1 → 0x0001FFFE、Size3_Off7 → 0x00007F80、Size2_OffF → 0x00078000；Size5_Off3 與 Size1_Off1F 不變)
- Total ~98/98 ctest

**Status**: Not Started

---

## Stage B-4: WRAP + FIXED burst

**Goal**: AxiSlave 加 WRAP addr 計算（wrap_lower/upper boundary + 環繞）+ FIXED addr（所有 beat 同 addr）；parser 接受 WRAP/FIXED；parser 拒收 WRAP len ∉ {1,3,7,15}、WRAP unaligned、WRAP+unaligned combo。

**Success Criteria**:
- `test_axi_slave` WRAP all 4 valid lens (1/3/7/15) + FIXED tests 全綠
- `test_scenario_parser` WRAP rejection tests 全綠
- Integration fixtures `wrap_burst_aligned.yaml`、`wrap_burst_actual_wrap.yaml`、`fixed_burst.yaml` 3 個全綠

**Status**: Not Started

---

## Stage B-5a: 4KB cross + OperationContext + per-ID FIFO refactor

**Goal**: AxiSlave `active_writes_/reads_` 從 single slot 改 per-ID FIFO (`std::map<uint8_t, std::deque<...>>`)；AxiMaster 加 `OperationContext` 層管理 4KB-split sub-bursts；4KB split algorithm 含 256-beat max 限制；1 WriteResult per scenario txn。

**Success Criteria**:
- Phase A 78 tests **全部仍綠**（per-ID FIFO refactor 不能破壞）
- `test_axi_master` 4KB split algorithm 4-5 個 unit tests 全綠
- `test_axi_slave` per-ID FIFO multi-burst tests 全綠
- Integration fixture `cross_4kb_auto_split.yaml` 全綠

**Status**: Not Started

---

## Stage B-5b: Runtime protocol validation

**Goal**: 新 `c_model/include/axi/protocol_rules.hpp`（`AXI_PROTOCOL_ASSERT` macro + 22 inline `check_*()` helpers）；在 AxiSlave/AxiMaster `tick()` 各 step 插入 inline asserts；22 個 EXPECT_DEATH 死亡測試（parameterized + readable case names + release build `GTEST_SKIP()`）；2 個 combined fixture (`narrow_unaligned.yaml`、`sparse_multibeat.yaml`)；1 個 parser-negative test for WRAP+unaligned。

**Success Criteria**:
- `axi/protocol_rules.hpp` 22 helpers 全部就位
- `test_protocol_rules.cpp` 22 death tests 全綠（debug build）
- 2 個 combined fixture 全綠
- Final ctest count ≈ 146 (Phase A 78 + Phase B ~68 new)
- 4 條 drift gates 全綠
- 移除 `c_model/IMPLEMENTATION_PLAN.md`

**Status**: Not Started
