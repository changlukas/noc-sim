# IMPLEMENTATION_PLAN — FEAT-NMU-AXI_SLAVE_PORT

> **Stage tracker** per CLAUDE.md。詳版 task-by-task plan: `docs/superpowers/plans/2026-05-28-nmu-axi-slave-port.md`
>
> **Spec**: `docs/superpowers/specs/2026-05-28-nmu-axi-slave-port-design.md`
>
> Remove this file when all stages complete (per CLAUDE.md)。

---

## Stage 1: Scaffold — header file、beat structs、enums、CMake wiring

**Goal**: 建立 `c_model/include/nmu/axi_slave_port.hpp` 與 `c_model/tests/nmu/test_axi_slave_port.cpp` 兩個檔骨架；宣告 `AwBeat` / `WBeat` / `ArBeat` / `BBeat` / `RBeat` / `ChannelMask` / `QueueDepths` POD types；新增 `c_model/tests/nmu/CMakeLists.txt` 並從 parent `tests/CMakeLists.txt` add_subdirectory。

**Success Criteria**:
- `cmake --build build && ctest -R test_axi_slave_port` 通過
- 一支 stub test 驗證 types 可構造、namespace `ni::cmodel::nmu` 可訪問
- `cd ../spec_validate && py -3 -m pytest -q` 仍 0 fail（spec 無動）

**Status**: Not Started

---

## Stage 2: AxiSlavePort class — inbound push/pop + all-or-nothing + per-channel depth

**Goal**: 實作主 class 的 inbound 路徑 — `push_inbound_pins(pins, mask)` (all-or-nothing) + private `dispatch_` + `pop_aw / pop_w / pop_ar` + per-channel queue 與 size/capacity getters。

**Success Criteria** (對應 spec Test categories)：
- #1 Per-channel push/pop fidelity（AW 11 欄位 round-trip）
- #2 All-or-nothing push（`mask=Aw|W`、`aw_q` 滿 → 整體 false、`w_q` 不變）
- #3 QueueDepths 各 channel 獨立
- #6 DPI boundary 相容（`AxiSlavePortPins` 直接 push 不失真）
- W、AR channel 同 #1 fidelity

**Status**: Not Started

---

## Stage 3: Outbound + tick + stateless AXI4 protocol field check

**Goal**: 補完 outbound 路徑（`push_b / push_r / pop_outbound_b / pop_outbound_r`）；`tick()` no-op；每個 push 上 stateless AXI4 protocol field check (LEN/SIZE/BURST/WRAP/4KB/STRB/RESP)，debug assert / release silent。

**Success Criteria**：
- #4 Outbound pop B/R 獨立（兩條 queue 互不干擾）
- #5 `tick()` no-op（內部 state 不變）
- #7 Stateless protocol violation release-build → push 仍 true、beat enqueue
- #8 Stateless protocol violation debug-build → `EXPECT_DEATH`

**Status**: Not Started

---

## Stage 4: Parameterized fixture + seeded random shadow-model + exercise counters

**Goal**: 把 Stage 2/3 的 ad-hoc test 重構成 GoogleTest `TEST_P` 參數化矩陣；新增 seeded RNG 對照 `std::deque` shadow-model 的 randomized 測試；新增 exercise counters 確保關鍵 path（full / empty / all-or-nothing rollback / each protocol rule / each channel）都被跑過。

**Success Criteria**：
- #9 TEST_P 覆蓋 `channel × depth × mask × build` 矩陣
- #10 Seeded random test 通過，shadow model 與 class 行為位元一致
- #11 結尾 `EXPECT_TRUE` 所有 exercise counter
- 整體 ctest 仍綠

**Status**: Not Started
