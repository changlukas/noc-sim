# IMPLEMENTATION_PLAN — Pure AXI Subsystem (Stage 2 Phase A)

> Stage tracker per CLAUDE.md。詳版 task-by-task plan: `docs/superpowers/plans/2026-05-29-pure-axi-subsystem.md`
>
> Spec: `docs/superpowers/specs/2026-05-29-pure-axi-subsystem-design.md`
>
> Remove this file when all stages complete.

---

## Stage 1: Cleanup + Scaffold

**Goal**: 刪除 Stage 1 AxiSlavePort 全部檔案；建立 `c_model/include/axi/` namespace tree；加 yaml-cpp dependency；寫 ATTRIBUTION.md；scaffold smoke test 跑通。

**Success Criteria**:
- `c_model/include/nmu/` 與 `c_model/tests/nmu/` 完全移除
- `c_model/include/axi/{types.hpp,ATTRIBUTION.md}` 存在；含 codegen-sourced constants + static_assert
- `c_model/tests/axi/{CMakeLists.txt,test_scaffold.cpp}` 存在
- `ctest -R AxiScaffold` 通過；4 條 drift gates 全綠

**Status**: Not Started

---

## Stage 2: Memory class

**Goal**: 實作 `IMemoryPort` interface + `Memory` class（configurable latency、OOB DECERR、WSTRB byte mask、backpressure）。

**Success Criteria**:
- `test_memory.cpp` ≥10 cases 全綠
- submit/pop / latency countdown / in-bounds OKAY / OOB DECERR / WSTRB byte merge / queue full backpressure 全 covered

**Status**: Not Started

---

## Stage 3: AxiSlave class

**Goal**: 實作 AxiSlave controller — per-ID active burst tracking、tag correlation、AW/W independence、burst-atomic OOB、backpressure retry。Unit test 用 `MockMemoryPort` 隔離。

**Success Criteria**:
- `test_axi_slave.cpp` ≥12 cases 全綠
- 涵蓋 single-in-flight、multi-in-flight (per-ID)、AW-before-W、W-before-AW、OOB burst → DECERR、Memory full → AxiSlave retry

**Status**: Not Started

---

## Stage 4: AxiMaster + ScenarioParser + Scoreboard

**Goal**: 實作 YAML scenario parser (yaml-cpp 後端)、AxiMaster (max_outstanding 參數化)、Scoreboard byte-map。Unit test 用 `MockSlave` 隔離。

**Success Criteria**:
- `test_axi_master.cpp` ≥10 cases、`test_scoreboard.cpp` ≥5 cases 全綠
- YAML parse / unknown field throw / max_outstanding admission / read accumulator / file dump / callback fire / byte_map OKAY-only update / mismatch detection 全 covered

**Status**: Not Started

---

## Stage 5: Integration

**Goal**: 4 class 接起來跑完整 loop。12 個 YAML fixtures (per spec Testing section)。TEST_P integration test。file diff + scoreboard byte-map 雙驗證。

**Success Criteria**:
- `test_integration.cpp` TEST_P 12 個 instance 全綠
- 4 條 drift gates 全綠
- Branch ready for final code review + merge
- 移除 `c_model/IMPLEMENTATION_PLAN.md`（per CLAUDE.md 「all stages done」）

**Status**: Not Started
