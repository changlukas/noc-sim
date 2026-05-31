# c_model — Next Session Handoff

**Status (2026-05-31)**：Stage 2 Phase B 完工（pure AXI subsystem 全 AXI4 功能擴展）；149/149 tests pass (sequential)；spec_validate 三 domain 純 symbolic，0 error / 0 warning；GitHub Action drift gate 在線。

---

## 完成清單

- **Layer A**（bootstrap）：`Flit` / `RegisterFile` / `ni_spec.hpp`
- **Stage 2 Phase A**（pure AXI subsystem 基底）：`c_model/include/axi/{types,memory_port,memory,axi_slave,axi_master,scenario_parser,scoreboard}.hpp` + `ATTRIBUTION.md`、`c_model/tests/axi/test_*.cpp` + 12 YAML fixtures、78 tests
- **Stage 2 Phase B**（pure AXI subsystem 全 AXI4 擴展）：
  - B-1 Sparse WSTRB（`strb_file` YAML field + Scoreboard 多 beat byte-merge）
  - B-2 Unaligned start address（first beat WSTRB lane mask）
  - B-3a Bus semantics refactor（AXI4 IHI 0022 lane-positioned，Memory/Scoreboard/master read accumulator 同步改）
  - B-3b Narrow transfer（AxiMaster narrow byte-lane W push + 2 fixtures）
  - B-4 WRAP + FIXED burst（`beat_addr` 共用 helper + WRAP-aware OOB + parser 接受/拒絕約束 + 3 fixtures）
  - B-5a 4KB cross + OperationContext + per-ID FIFO refactor（AxiSlave `map<id, deque<state>>`、AxiMaster `OperationContext` + `split_into_sub_bursts`、1 WriteResult per scenario_txn）
  - B-5b Runtime protocol validation（`protocol_rules.hpp` + `AXI_PROTOCOL_ASSERT` macro + ~22 helpers + 27 EXPECT_DEATH tests + 2 combined fixtures）
- **Stage 1 AxiSlavePort 已 superseded**：原 `c_model/include/nmu/axi_slave_port.hpp` + tests 已刪；新設計改用 `c_model/include/axi/axi_slave.hpp`（normal AXI slave controller，不是 NMU forwarder）

---

## 已知限制（merge 後 follow-up）

- **`split_into_sub_bursts` `beats_to_4kb == 0` fallback**：unaligned tail-of-page INCR multi-beat 會產生 4KB-crossing sub-burst。Debug build `check_4kb_cross` runtime assert (at AxiSlave) 會 catch；source-level fix 是 follow-up。
- **Unaligned size=5 len=0 1-beat squeeze**：AXI4 spec 要求 2 beats，c_model 只送 1 beat + trailing 0 padding。已在 `unaligned_start.yaml` 註解標明。
- **Parallel ctest tempfile collision**：`ScenarioParser` 系列 tests 共用 `testing::TempDir() + "/scenario.yaml"`，`-j N` 會 flake。Sequential ctest 100% clean；fix 是改 per-test unique 名。
- **AxiMaster same-id concurrent operations 仍 disallowed**：sub-burst stacking 只發生在同一個 operation 內，跨 operations 還是 1-per-id。需要時可以擴展。

---

## 下一步：Stage 2 Phase C

依 `docs/superpowers/specs/2026-05-31-pure-axi-subsystem-phase-b-design.md` Out-of-plan follow-ups：

- **Exclusive access**（AxLOCK + EXOKAY）：AxiSlave 加 exclusive monitor（per-ID + addr range tracking）
- YAML schema extend：optional `lock` field

---

## Future NoC 整合（Stage 3+）

- 本 `c_model/include/axi/*` 維持不變，當 NSU 的 AXI master + 可選 testbench 用 AXI slave
- NMU 重新設計 AXI slave forwarder（架構與本 axi 子系統解耦）
- DPI bridge → 解鎖 handshake-level rules (`*_VALID_STABLE` 等)；SV testbench + Verilator integration

---

## Drift gates — every commit must pass

```
cd spec_validate
py -3 -m pytest -q                         # 159 tests
py -3 tools/codegen.py --check             # byte-identical .h / .sv
py -3 tools/gen_inventory.py --check       # FEATURE_INVENTORY drift
cd ../c_model && cmake --build build && ctest --test-dir build  # 149/149 sequential
```

GitHub Action 自動跑前 3 條 + `py -3 -m ni_spec ../spec/ni/doc`（要求 0 error / 0 warning）。本機 pre-commit hook 啟用：`git config core.hooksPath scripts/git-hooks`。

---

## Inputs the next session should consult

- `docs/superpowers/specs/2026-05-31-pure-axi-subsystem-phase-b-design.md` — Phase B design spec + Phase C roadmap
- `docs/superpowers/plans/2026-05-31-pure-axi-subsystem-phase-b.md` — Phase B 完整實作 plan（reference for Phase C planning style）
- `spec_validate/include/{ni_signals.h, ni_flit_constants.h}` — codegen 常數（`ni::WSTRB_WIDTH`、`ni::width::*`）
- `c_model/include/axi/ATTRIBUTION.md` — cocotbext-axi MIT port mapping
- `c_model/include/axi/*.hpp` — Phase B 既有 class，仿照 style 擴展 Phase C

---

## Process expectations (from saved memories)

- **OSS-first survey**：寫 source / test 前先看 OSS 對應實作
- **Subagent-driven**：每 stage 派 implementer subagent + spec reviewer + code quality reviewer
- **Karpathy 4-lens review** 每 task 完成後跑：overcomplication / surgical / surface assumptions / verifiable success
- **Concise doc style**：spec ~200-400 行內、不放展開的設計理由
- **不重新設計 protocol**：AXI standard behavior 不該由 c_model implementer 設計；runtime validation 是「監控合規」、不算重新設計
- **AXI4 lane-positioned bus semantics**：byte_lane = bus_addr & (DATA_BYTES-1)；narrow/unaligned 一律遵守
- **Don't bypass commit hooks**（no `--no-verify`）
