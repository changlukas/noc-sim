# c_model — Next Session Handoff

**Goal**：開始撰寫 NMU / NSU feature classes（Layer B / Stage 2）。
**Status (2026-05-28)**：Layer A bootstrap 已完成（`Flit`、`RegisterFile`）；spec_validate 三 domain 已純 symbolic，0 error / 0 warning；GitHub Action drift gate 已上線。

---

## Pick this feature first

**`FEAT-NMU-AXI_SLAVE_PORT`** — 起點理由：
- 是 NMU 七個 minimum features 中最 irreducible（沒它沒有 AXI 流量）
- 跟既有 `Flit` 互動最直接（packet 化的源頭）
- AXI slave port 行為由 AXI library 處理（不該由 c_model 重新設計，per [[dont-redesign-protocol-spec]]）

走完 AXI_SLAVE_PORT 一個 feature 後再批次推進剩下 6 NMU + 5 NSU。

---

## File layout convention

依 `FEATURE_INVENTORY.md` 已建立的 mapping（`gen_inventory.py:_expected_header`）：

```
c_model/include/nmu/axi_slave_port.hpp     <- FEAT-NMU-AXI_SLAVE_PORT
c_model/include/nmu/addr_trans.hpp         <- FEAT-NMU-ADDR_TRANS
c_model/include/nmu/packetize.hpp          <- FEAT-NMU-PACKETIZE
c_model/include/nmu/vc_mapping.hpp         <- FEAT-NMU-VC_MAPPING
c_model/include/nmu/vc_arb.hpp             <- FEAT-NMU-VC_ARB
c_model/include/nmu/rob.hpp                <- FEAT-NMU-ROB
c_model/include/nmu/depacketize.hpp        <- FEAT-NMU-DEPACKETIZE

c_model/include/nsu/axi_master_port.hpp    <- FEAT-NSU-AXI_MASTER_PORT
c_model/include/nsu/depacketize.hpp        <- FEAT-NSU-DEPACKETIZE
c_model/include/nsu/meta_buffer.hpp        <- FEAT-NSU-META_BUFFER
c_model/include/nsu/packetize.hpp          <- FEAT-NSU-PACKETIZE
c_model/include/nsu/vc_arb.hpp             <- FEAT-NSU-VC_ARB
```

`src/` 與 `tests/` 對應同樣 layout（如 `tests/nmu/test_axi_slave_port.cpp`）。

---

## Drift gates — every commit must pass

```
cd spec_validate
py -3 -m pytest -q                         # 159 tests
py -3 tools/codegen.py --check             # byte-identical .h / .sv
py -3 tools/gen_inventory.py --check       # FEATURE_INVENTORY drift
cd ../c_model && cmake --build build       # build + ctest
```

GitHub Action 自動跑前 3 條 + `py -3 -m ni_spec ../spec/ni/doc`（要求 0 error / 0 warning）。本機 pre-commit hook 啟用：`git config core.hooksPath scripts/git-hooks`。

---

## Inputs the next session should consult

- `spec_validate/authored/ni_function_blocks.json` — feature catalog（minimum NMU 7 + NSU 5）
- `spec_validate/docs/guide/NI.jpg` — 規範 block diagram
- `spec_validate/docs/guide/json-to-code-examples.md` — JSON → .h / .sv 轉換 pattern
- `c_model/include/flit.hpp` / `register_file.hpp` — Layer A 既有 class，仿照 style
- `c_model/include/ni_spec.hpp` — umbrella include（elaborated 常數來源）

---

## Process expectations (from saved memories)

- **OSS-first survey** 寫任何 source / test 前先看有沒有 OSS 可用，避免 reinvent wheel
- **Subagent-driven**：每個 feature 任務分派給 implementer subagent + 用 reviewer subagent review
- **Karpathy 4-lens review** 任務完成後跑：overcomplication / surgical / surface assumptions / verifiable success
- **Concise doc style**：寫文件 ~200 行內、不放展開的設計理由
- **不重新設計 protocol**：AXI outstanding / OoO / ordering 都該由 AXI library 完成，不寫進 NMU/NSU class
- **Don't bypass commit hooks**（no `--no-verify`）
