# c_model — Next Session Handoff

**Status (2026-05-29)**：Stage 2 Phase A 完工（pure AXI subsystem 4 class + 12 integration fixtures + scoreboard）；78/78 tests pass；spec_validate 三 domain 純 symbolic，0 error / 0 warning；GitHub Action drift gate 在線。

---

## 完成清單

- **Layer A**（bootstrap）：`Flit` / `RegisterFile` / `ni_spec.hpp`
- **Stage 2 Phase A**（pure AXI subsystem）：`c_model/include/axi/{types,memory_port,memory,axi_slave,axi_master,scenario_parser,scoreboard}.hpp` + `ATTRIBUTION.md`、`c_model/tests/axi/test_*.cpp` + 12 YAML fixtures
- **Stage 1 AxiSlavePort 已 superseded**：原 `c_model/include/nmu/axi_slave_port.hpp` + tests 已刪；新設計改用 `c_model/include/axi/axi_slave.hpp`（normal AXI slave controller，不是 NMU forwarder）

---

## 下一步：Stage 2 Phase B

依 `docs/superpowers/specs/2026-05-29-pure-axi-subsystem-design.md` Future roadmap：

**Slave-side**：
- WRAP / FIXED burst（AxiSlave addr 計算擴展）
- Unaligned start address（first beat WSTRB 標部分有效）
- Sparse WSTRB byte-merge（Memory 已支援、AxiMaster + Scoreboard 擴展）

**Master-side**（per cocotbext-axi port audit）：
- 4KB cross detection + auto-segmentation（~80 行 port）
- Narrow transfer 處理（AxSIZE < log2(DATA_BYTES)，~70 行 port）
- Runtime protocol validation（WLAST timing / ID match / ordering rules，~100-150 行 port）

**YAML schema extend**：optional `strb_file`；parser 不再 reject 4KB cross

預估 Phase B 增量 ~400 行（c_model 從 771 → ~1100-1200 行）

---

## Stage 2 Phase C（之後）

- Exclusive access（AxLOCK + EXOKAY）：AxiSlave 加 exclusive monitor（per-ID + addr range tracking）
- YAML schema extend：optional `lock` field

---

## Future NoC 整合（Stage 3+）

- 本 `c_model/include/axi/*` 維持不變，當 NSU 的 AXI master + 可選 testbench 用 AXI slave
- NMU 重新設計 AXI slave forwarder（架構與本 axi 子系統解耦）— 屆時 FEATURE_INVENTORY.md 的 `c_model/include/nmu/axi_slave_port.hpp` 才會被實作

---

## Drift gates — every commit must pass

```
cd spec_validate
py -3 -m pytest -q                         # 159 tests
py -3 tools/codegen.py --check             # byte-identical .h / .sv
py -3 tools/gen_inventory.py --check       # FEATURE_INVENTORY drift
cd ../c_model && cmake --build build && ctest  # 78/78
```

GitHub Action 自動跑前 3 條 + `py -3 -m ni_spec ../spec/ni/doc`（要求 0 error / 0 warning）。本機 pre-commit hook 啟用：`git config core.hooksPath scripts/git-hooks`。

---

## Inputs the next session should consult

- `docs/superpowers/specs/2026-05-29-pure-axi-subsystem-design.md` — design spec + Phase B/C roadmap
- `docs/superpowers/plans/2026-05-29-pure-axi-subsystem.md` — Phase A 完整實作 plan（reference for Phase B planning style）
- `spec_validate/include/{ni_signals.h, ni_flit_constants.h}` — codegen 常數（`ni::WSTRB_WIDTH`、`ni::width::*`）
- `c_model/include/axi/ATTRIBUTION.md` — cocotbext-axi MIT port mapping
- `c_model/include/axi/*.hpp` — Phase A 既有 class，仿照 style 擴展 Phase B

---

## Process expectations (from saved memories)

- **OSS-first survey**：寫 source / test 前先看 OSS 對應實作（Phase A 已 port cocotbext-axi；Phase B 對應的 4KB cross / narrow transfer / validation 在 cocotbext-axi `axi_master.py` 與 `axi_slave.py` 內）
- **Subagent-driven**：每 stage 派 implementer subagent + spec reviewer + code quality reviewer
- **Karpathy 4-lens review** 每 task 完成後跑：overcomplication / surgical / surface assumptions / verifiable success
- **Concise doc style**：spec ~200-400 行內、不放展開的設計理由
- **不重新設計 protocol**：AXI standard behavior 不該由 c_model implementer 設計；runtime validation 是「監控合規」、不算重新設計（Phase B 會加）
- **Don't bypass commit hooks**（no `--no-verify`）
