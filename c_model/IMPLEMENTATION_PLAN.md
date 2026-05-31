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

## Stage B-3: Narrow transfer

**Goal**: AxiMaster 按 size 計算 byte lane 與 strb，把 data 放在 WBeat 正確位置；AxiSlave 解 interpret；parser 接受 narrow size。

**Success Criteria**:
- `test_axi_master` 所有 narrow sizes 0-4 byte-lane 計算 unit tests 全綠
- `test_axi_slave` narrow interpret tests 全綠
- Integration fixtures `narrow_transfer_size2.yaml` + `narrow_transfer_size0.yaml` 2 個全綠

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
