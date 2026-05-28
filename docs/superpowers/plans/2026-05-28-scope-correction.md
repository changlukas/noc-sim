# Scope Correction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Narrow codegen scope (delete blocks domain elaboration) + resolve 4 of 7 sufficiency findings (F-002, F-004, F-005, F-006) by extending codegen and removing c_model workarounds.

**Architecture:** 5 sequential phases per spec doc, with hard phase-gate rule between them (each phase ends green on pytest + ctest + `tools/codegen.py --check` before next starts). Codegen-side changes precede c_model consume within each phase.

**Tech Stack:** Python 3 + pytest (codegen + L1/L2 tests), C++17 (c_model), CMake + GoogleTest, SystemVerilog packages.

**Invariants (from spec, must hold throughout):**
1. Codegen規範: top-level pin interface (signals), over-wire packet, CSR registers ONLY
2. c_model 不 hardcode 規格值 — reference codegen symbols
3. Describe codegen action as "elaborate", not "emit"
5. `ni_function_blocks.json` kept as feature inventory; no longer drives codegen

**Reference:** `docs/superpowers/specs/2026-05-28-scope-correction-design.md`

**Working dir:** `E:/05_NoC/noc-sim/.worktrees/c-model-bootstrap`. Worktree branch: `feat/c-model-bootstrap` (already 19 commits in; this plan adds ~9 more).

---

## Task 1: Phase X.1 — Blocks domain removal

**Files:**
- Modify: `c_model/include/ni_spec.hpp:8` (drop `#include "ni_blocks.h"`)
- Modify: `spec_validate/ni_spec/invariants.py` (delete `check_mode_enum_name_unique`)
- Modify: `spec_validate/ni_spec/__main__.py` (drop the check call)
- Modify: `spec_validate/ni_spec/constants.py:208-231` (delete blocks accessors)
- Modify: `spec_validate/tests/test_function_blocks.py` (drop 2 mode-enum tests)
- Modify: `spec_validate/tests/test_codegen.py` (drop blocks references)
- Modify: `spec_validate/tests/test_codegen_sv.py` (drop blocks references)
- Modify: `spec_validate/tools/codegen.py` (drop blocks from DOMAIN_TO_EMITTER, --domain choices, docstring, --check loop)
- Modify: `spec_validate/tools/README.md` (dataflow + per-domain table)
- Delete: `spec_validate/tools/elaborate/cpp_blocks.py`
- Delete: `spec_validate/tools/elaborate/sv_blocks.py`
- Delete: `spec_validate/include/ni_blocks.h`
- Delete: `spec_validate/rtl_pkg/ni_blocks_pkg.sv`

- [ ] **Step 1: Drop ni_blocks.h include from c_model umbrella**

Edit `c_model/include/ni_spec.hpp` — remove line 8 (`#include "ni_blocks.h"`) and the trailing comment if present. Final file should `#include` only the 3 remaining codegen headers (`ni_flit_constants.h`, `ni_signals.h`, `ni_regs.h`).

- [ ] **Step 2: Find all references to ni::blocks::* to confirm none remain in c_model code**

Run from worktree root:

```bash
grep -rn "ni::blocks\|ni_blocks\.h" c_model/ 2>&1
```

Expected: no matches (or only matches in `c_model/SUFFICIENCY_FINDINGS.md` documentation — leave those alone for now, X.5 will edit them).

If any source/header has `ni::blocks::*` reference, stop and report BLOCKED — this means c_model has consumers that weren't in scope when blocks domain was added.

- [ ] **Step 3: Delete the L2 invariant function in invariants.py**

In `spec_validate/ni_spec/invariants.py`, delete the function `check_mode_enum_name_unique` (around line 355) and its docstring. Keep all sibling `check_blocks_*` functions.

- [ ] **Step 4: Drop the L2 invariant call from the dispatcher**

In `spec_validate/ni_spec/__main__.py`, find the section that calls `check_mode_enum_name_unique(fb_spec)` (around line 153). Remove that call and the surrounding `for msg in ... : issues.append(Issue("ERROR", "L2-FB-MODE-ENUM", msg))` loop. Other `check_blocks_*` calls remain.

- [ ] **Step 5: Delete blocks accessors in constants.py**

In `spec_validate/ni_spec/constants.py`, delete the section that contains `blocks_function_block_names`, `blocks_modes_of`, `blocks_compile_time_params` (around lines 208-231). These served only the now-deleted `cpp_blocks.py` / `sv_blocks.py` elaborators.

- [ ] **Step 6: Drop 2 mode-enum tests from test_function_blocks.py**

In `spec_validate/tests/test_function_blocks.py`, delete:
- `def test_mode_enum_names_unique_after_block_prefix(...)` (and its docstring)
- `def test_l2_mode_enum_unique_check_fires_on_collision(...)` (and its docstring)

Keep all other tests (schema validation, cross-ref checks).

- [ ] **Step 7: Drop blocks tests from test_codegen.py and test_codegen_sv.py**

Run:

```bash
grep -n "blocks" spec_validate/tests/test_codegen.py spec_validate/tests/test_codegen_sv.py
```

For each match in `test_codegen.py` and `test_codegen_sv.py`:
- If it's a test function named `test_*_blocks_*` or `test_blocks_*` — delete the function
- If it's inside a `for domain in [...]` loop, remove `"blocks"` from the list
- If it's an `assert ... blocks ...` line that's part of a generic test, remove that assert

After edit, re-grep to verify zero `blocks` references in those two test files.

- [ ] **Step 8: Remove blocks from codegen.py CLI + dispatcher**

In `spec_validate/tools/codegen.py`:
- Remove the two `--domain blocks` lines from the docstring (lines 4-12)
- In `DOMAIN_TO_EMITTER` dict, delete the two entries: `("cpp", "blocks"): ...` and `("sv", "blocks"): ...`
- In the `argparse.ArgumentParser` setup, find the `--domain` choices list and remove `"blocks"`
- In the `--check` mode handler, find any per-domain regen loop and remove `"blocks"` from it
- Also remove the now-unused `from tools.elaborate import cpp_blocks, sv_blocks` imports at the top (if present)

- [ ] **Step 9: Delete elaborator files and committed artifacts**

```bash
git rm spec_validate/tools/elaborate/cpp_blocks.py \
       spec_validate/tools/elaborate/sv_blocks.py \
       spec_validate/include/ni_blocks.h \
       spec_validate/rtl_pkg/ni_blocks_pkg.sv
```

- [ ] **Step 10: Update tools/README.md**

In `spec_validate/tools/README.md`:
- Find the dataflow ASCII diagram showing `4 + 4 elaborators` — change to `3 + 3 elaborators`
- Find the per-domain table listing `cpp_blocks.py`, `sv_blocks.py` — delete those rows
- Find any prose mentioning the "blocks" domain — update to reflect 3 domains (packet, signals, registers)
- The "core principle" sentence about ni_function_blocks.json should be revised to note: "blocks JSON kept as feature inventory + cross-domain consistency check; no longer drives codegen"

- [ ] **Step 11: Verify Phase X.1 gate — all 3 green**

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
cd c_model/build && cmake --build . && ctest --output-on-failure 2>&1 | tail -5
```

Expected:
- pytest: all tests pass (count will be lower than 111 baseline because we dropped tests; just verify "0 failed")
- `--check`: exit 0
- ctest: 21/21 passed (no regression in c_model since we only dropped the umbrella include of `ni_blocks.h`, which c_model code did not reference)

- [ ] **Step 12: Verify no ni::blocks reference remains anywhere except docs**

```bash
grep -rn "ni::blocks\|ni_blocks_pkg\|ni_blocks\.h" \
  --include="*.cpp" --include="*.hpp" --include="*.h" --include="*.sv" --include="*.py" \
  . 2>&1
```

Expected: zero matches. (Docs/spec/plan files may still reference blocks for historical context — those don't need cleanup here; X.5 final disposition will edit them.)

- [ ] **Step 13: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
refactor(spec_validate,c_model): remove blocks-domain codegen (Phase X.1)

Per scope-correction design doc: codegen narrows to standard interface
(signals, packet) + CSR (registers). Internal unit modes / compile-time
params are implementer decisions, not codegen output. Deletes 4 files
(elaborators + 2 elaborated artifacts), strips blocks references from
dispatcher / CLI / tests / accessors / docs.

ni_function_blocks.json + schema + check_blocks_* validators preserved
as feature inventory.

Refs design doc Phase X.1.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Phase X.1.5 — Feature inventory generator + drift gate

**Files:**
- Create: `spec_validate/tools/gen_inventory.py` (standalone generator, not part of codegen.py)
- Create: `c_model/FEATURE_INVENTORY.md` (committed elaborated artifact)
- Create: `spec_validate/tests/test_feature_inventory.py` (new pytest)

**Goal**: replace deleted blocks-domain codegen with a lightweight markdown inventory generator. Provides slide-friendly NMU / NSU feature lists + drift gate (committed MD must stay in sync with `ni_function_blocks.json`). Does NOT generate any C++ / SV code — class shape stays implementer-decided.

- [ ] **Step 1: Write the generator script**

Create `spec_validate/tools/gen_inventory.py`:

```python
#!/usr/bin/env python
"""Generate c_model/FEATURE_INVENTORY.md from ni_function_blocks.json.

Standalone (not part of tools/codegen.py): inventory is documentation, not
C++/SV code. Run after editing ni_function_blocks.json to keep the MD in sync.

Drift gate: tests/test_feature_inventory.py regenerates to a tempfile and
diffs against the committed MD (timestamp banner line excluded).
"""
from __future__ import annotations
import argparse
import datetime
import json
import sys
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
JSON_PATH    = SPEC_VALIDATE / "ni_function_blocks.json"
DEFAULT_OUT  = SPEC_VALIDATE.parent / "c_model" / "FEATURE_INVENTORY.md"


def _expected_header(feat_id: str) -> str:
    """FEAT-NMU-AXI_SLAVE_PORT -> c_model/include/nmu/axi_slave_port.hpp"""
    parts = feat_id.split("-")
    if len(parts) < 3 or parts[0] != "FEAT":
        return "(unknown id format)"
    block = parts[1].lower()
    short = "_".join(parts[2:]).lower()
    return f"c_model/include/{block}/{short}.hpp"


def _format_modes(modes):
    if not modes:
        return "—"
    return ", ".join(modes)


def render(spec, when):
    out = []
    out.append(f"<!-- AUTO-GENERATED by spec_validate/tools/gen_inventory.py — DO NOT EDIT -->")
    out.append(f"<!-- Source: spec_validate/ni_function_blocks.json -->")
    out.append(f"<!-- Generated at: {when} -->")
    out.append("")
    out.append("# NI Feature Inventory")
    out.append("")
    out.append("Inventory of NMU / NSU features defined in `ni_function_blocks.json`. ")
    out.append("Per Invariant 1, the JSON does not drive codegen of internal unit classes; ")
    out.append("this file is documentation + a drift gate.")
    out.append("")
    out.append("Column **Expected c_model header** points to the conventional path for that ")
    out.append("feature's c_model implementation. Existence is **not enforced** — most features ")
    out.append("won't have an implementation until Layer B / Stage 2.")
    out.append("")
    for block in spec.get("blocks", []):
        name = block["name"]
        full = block.get("fullname", "")
        role = block.get("role", "")
        out.append(f"## {name} — {full}")
        out.append("")
        if role:
            out.append(f"_Role_: {role}")
            out.append("")
        out.append("| Feature ID | Summary | Modes | Expected c_model header |")
        out.append("|---|---|---|---|")
        for feat in block.get("features", []):
            fid     = feat["id"]
            summary = feat.get("summary", "").replace("|", "\\|")
            modes   = _format_modes(feat.get("modes"))
            hdr     = _expected_header(fid)
            out.append(f"| `{fid}` | {summary} | {modes} | `{hdr}` |")
        out.append("")
    return "\n".join(out) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Generate c_model/FEATURE_INVENTORY.md")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true",
                    help="Verify committed MD matches regen (timestamp excluded). Exit 1 on drift.")
    args = ap.parse_args()
    spec = json.loads(JSON_PATH.read_text())
    when = datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    body = render(spec, when)
    if args.check:
        if not args.out.exists():
            print(f"FAIL: committed inventory missing at {args.out}", file=sys.stderr)
            sys.exit(1)
        existing = args.out.read_text()
        # Strip timestamp comment line from both sides
        strip = lambda t: "\n".join(
            l for l in t.splitlines() if not l.startswith("<!-- Generated at:"))
        if strip(body) != strip(existing):
            print(f"FAIL: inventory drift at {args.out}", file=sys.stderr)
            sys.exit(1)
        sys.exit(0)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(body)
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run the generator to produce the inventory MD**

```bash
cd /e/05_NoC/noc-sim/.worktrees/c-model-bootstrap
py -3 spec_validate/tools/gen_inventory.py
```

Expected: `Wrote .../c_model/FEATURE_INVENTORY.md`.

Inspect:

```bash
head -30 c_model/FEATURE_INVENTORY.md
```

Expected: shows banner + `## NMU — Network Master Unit` heading + first NMU feature row in markdown table.

- [ ] **Step 3: Verify the inventory MD content matches JSON**

```bash
py -3 -c "
import json
d = json.load(open('spec_validate/ni_function_blocks.json'))
for block in d['blocks']:
    print(f\"{block['name']}: {len(block['features'])} features\")
    for f in block['features']:
        print(f\"  {f['id']}\")"
```

Compare against the `FEATURE_INVENTORY.md` content — every feature in JSON should appear in MD.

- [ ] **Step 4: Write pytest for drift detection**

Create `spec_validate/tests/test_feature_inventory.py`:

```python
"""Drift gate for the feature inventory MD.

Mirrors the tools/codegen.py --check pattern: regenerate inventory to memory
and ensure the committed c_model/FEATURE_INVENTORY.md matches (timestamp
banner line excluded).
"""
from __future__ import annotations
import json
import subprocess
import sys
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
WORKTREE_ROOT = SPEC_VALIDATE.parent
INVENTORY_MD  = WORKTREE_ROOT / "c_model" / "FEATURE_INVENTORY.md"
GENERATOR     = SPEC_VALIDATE / "tools" / "gen_inventory.py"


def test_inventory_md_exists():
    assert INVENTORY_MD.exists(), \
        f"committed inventory missing at {INVENTORY_MD}; run gen_inventory.py"


def test_inventory_md_up_to_date():
    """Run `gen_inventory.py --check` — exit 0 means committed MD matches regen."""
    r = subprocess.run(
        [sys.executable, str(GENERATOR), "--check"],
        capture_output=True, text=True,
    )
    assert r.returncode == 0, \
        f"FEATURE_INVENTORY.md drift detected.\nstdout: {r.stdout}\nstderr: {r.stderr}"


def test_inventory_covers_all_features_in_json():
    """Sanity: every feature in JSON appears in the MD by id."""
    spec = json.loads((SPEC_VALIDATE / "ni_function_blocks.json").read_text())
    md   = INVENTORY_MD.read_text()
    for block in spec["blocks"]:
        for feat in block["features"]:
            assert feat["id"] in md, f"feature {feat['id']} missing from inventory MD"
```

- [ ] **Step 5: Run the 3 new pytest cases**

```bash
cd spec_validate && py -3 -m pytest tests/test_feature_inventory.py -v 2>&1 | tail -10
```

Expected: 3 PASSED.

- [ ] **Step 6: Verify full pytest + drift gates**

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "codegen_check_exit=$?"
cd spec_validate && py -3 tools/gen_inventory.py --check; echo "inventory_check_exit=$?"
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5
```

Expected: pytest all pass; both `--check` exit 0; ctest all pass.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/tools/gen_inventory.py \
        spec_validate/tests/test_feature_inventory.py \
        c_model/FEATURE_INVENTORY.md
git commit -m "$(cat <<'EOF'
feat(spec_validate,c_model): feature inventory generator + drift gate

Phase X.1.5: replace deleted blocks-domain codegen with a lightweight
markdown inventory generator. Reads ni_function_blocks.json, produces
c_model/FEATURE_INVENTORY.md (NMU + NSU feature tables with expected
header path per feature).

Standalone tool (not codegen.py); kept separate because inventory is
documentation, not C++/SV code. Drift gate via pytest mirrors codegen.py
--check pattern.

Does NOT generate any C++ class shells — class shape stays implementer-
decided per Invariant 1. Inventory header-path column is informational
only; existence is not enforced until Layer B starts.

Refs design doc Phase X.1.5.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Phase X.2 — Codegen elaborate `<REG>_RESET` + `ALL_OFFSETS[]`

**Files:**
- Modify: `spec_validate/tools/elaborate/cpp_registers.py`
- Modify: `spec_validate/tools/elaborate/sv_registers.py`
- Modify: `spec_validate/tests/test_registers_parser.py` (add new pytest)
- Regenerate: `spec_validate/include/ni_regs.h`, `spec_validate/rtl_pkg/ni_regs_pkg.sv`

- [ ] **Step 1: Inspect JSON for reset value coverage + reserved row**

```bash
py -3 -c "import json; d=json.load(open('spec_validate/generated/ni_registers.json')); \
  print('total:', len(d['registers'])); \
  print('non-reserved:', sum(1 for r in d['registers'] if r.get('kind') != 'reserved')); \
  for r in d['registers']:
    if r.get('reset_expr') is not None and r.get('reset_expr') != '0x0':
      print(r['name'], '=', r.get('reset_expr'))"
```

Note total register count, reserved count, and which registers have non-zero reset. Expected: `TXN_MIN_LATENCY = 0xFFFF` is at least one non-zero reset.

- [ ] **Step 2: Write failing pytest in test_registers_parser.py**

Append to `spec_validate/tests/test_registers_parser.py`:

```python
def test_per_register_reset_const_elaborated():
    """ni_regs.h must expose <REG>_RESET constexpr per non-reserved register."""
    from pathlib import Path
    text = (Path(__file__).resolve().parent.parent / "include" / "ni_regs.h").read_text()
    # Non-zero reset must appear with its actual value
    assert "constexpr uint32_t TXN_MIN_LATENCY_RESET = 0xFFFF" in text, \
        "non-zero reset for TXN_MIN_LATENCY missing or wrong"
    # Zero reset must also appear (verify general elaboration)
    assert "constexpr uint32_t PKT_PROBE_EN_RESET = 0x0" in text or \
           "constexpr uint32_t PKT_PROBE_EN_RESET = 0" in text, \
        "zero reset for PKT_PROBE_EN missing"


def test_all_offsets_array_elaborated():
    """ni_regs.h must expose constexpr uint32_t ALL_OFFSETS[] and ALL_OFFSETS_COUNT."""
    from pathlib import Path
    text = (Path(__file__).resolve().parent.parent / "include" / "ni_regs.h").read_text()
    assert "constexpr uint32_t ALL_OFFSETS[]" in text
    assert "constexpr std::size_t ALL_OFFSETS_COUNT" in text
```

- [ ] **Step 3: Run to verify both new tests fail**

```bash
cd spec_validate && py -3 -m pytest \
  tests/test_registers_parser.py::test_per_register_reset_const_elaborated \
  tests/test_registers_parser.py::test_all_offsets_array_elaborated -v 2>&1 | tail -10
```

Expected: 2 FAILED.

- [ ] **Step 4: Modify cpp_registers.py to elaborate <REG>_RESET and ALL_OFFSETS**

In `spec_validate/tools/elaborate/cpp_registers.py`, add a helper that emits per-register reset constants and ALL_OFFSETS array. Call it from within `emit()`. The added section:

```python
def _emit_per_reg_reset(spec) -> list[str]:
    out = []
    out.append("// --- per-register reset values ---")
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved":
            continue
        if r.get("reset_expr") is None:
            continue
        name = r["name"].upper()
        # Normalize reset_expr to a C++-parseable hex literal
        rst = r["reset_expr"]
        # If it looks like "0xFFFF" or "0x0" keep as-is; else cast via int() and re-hex
        try:
            int_val = int(rst, 0)
            rst_lit = f"0x{int_val:X}"
        except (TypeError, ValueError):
            rst_lit = rst  # leave literal as-is if not parseable
        out.append(f"constexpr uint32_t {name}_RESET = {rst_lit};")
    out.append("")
    return out


def _emit_all_offsets(spec) -> list[str]:
    out = []
    out.append("// --- ALL_OFFSETS array (excludes reserved rows) ---")
    offsets = []
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved":
            continue
        offsets.append(f"  {r['name'].upper()}_OFFSET")
    out.append("constexpr uint32_t ALL_OFFSETS[] = {")
    out.append(",\n".join(offsets))
    out.append("};")
    out.append(f"constexpr std::size_t ALL_OFFSETS_COUNT = {len(offsets)};")
    out.append("")
    return out
```

In the existing `emit()` function, call these two helpers inside `namespace ni { namespace regs { ... } }`, AFTER the existing field-mask section and BEFORE the existing `csr_policy` section. Pseudocode for `emit()`:

```python
def emit(registers_json, spec_version):
    spec = load_doc(registers_json)
    out = []
    out.append("#pragma once")
    out.append("#include <cstddef>")  # add this if not present (needed for std::size_t)
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ni {")
    out.append("namespace regs {")
    # ... existing offset constants
    # ... existing field mask constants
    out.extend(_emit_per_reg_reset(spec))     # NEW
    out.extend(_emit_all_offsets(spec))       # NEW
    # ... existing static_assert section
    # ... existing csr_policy section
    out.append("} // namespace regs")
    out.append("} // namespace ni")
    return "\n".join(out)
```

- [ ] **Step 5: Modify sv_registers.py similarly**

In `spec_validate/tools/elaborate/sv_registers.py`, add inside the package body (before `endpackage`):

```python
def _emit_sv_per_reg_reset(spec) -> list[str]:
    out = []
    out.append("  // --- per-register reset values ---")
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved" or r.get("reset_expr") is None:
            continue
        name = r["name"].upper()
        rst = r["reset_expr"]
        try:
            int_val = int(rst, 0)
            rst_lit = f"32'h{int_val:X}"
        except (TypeError, ValueError):
            rst_lit = rst
        out.append(f"  localparam int unsigned {name}_RESET = {rst_lit};")
    out.append("")
    return out


def _emit_sv_all_offsets(spec) -> list[str]:
    out = []
    out.append("  // --- ALL_OFFSETS array (excludes reserved rows) ---")
    offsets = []
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved":
            continue
        offsets.append(f"    {r['name'].upper()}_OFFSET")
    out.append(f"  localparam int unsigned ALL_OFFSETS [{len(offsets)}] = '{{")
    out.append(",\n".join(offsets))
    out.append("  };")
    out.append(f"  localparam int unsigned ALL_OFFSETS_COUNT = {len(offsets)};")
    out.append("")
    return out
```

Wire both helpers into the existing `emit()` in `sv_registers.py`, called from within `package ni_regs_pkg; ... endpackage`.

- [ ] **Step 6: Regenerate**

```bash
cd spec_validate && py -3 tools/codegen.py --target cpp --domain registers --out include/
cd spec_validate && py -3 tools/codegen.py --target sv  --domain registers --out rtl_pkg/
```

Inspect to confirm the new section is present:

```bash
grep -n "ALL_OFFSETS\|_RESET = " spec_validate/include/ni_regs.h | head -10
grep -n "ALL_OFFSETS\|_RESET" spec_validate/rtl_pkg/ni_regs_pkg.sv | head -10
```

Expected: shows `TXN_MIN_LATENCY_RESET = 0xFFFF`, `PKT_PROBE_EN_RESET = 0x0`, and `ALL_OFFSETS[]` array.

- [ ] **Step 7: Run both new pytest cases — verify pass**

```bash
cd spec_validate && py -3 -m pytest \
  tests/test_registers_parser.py::test_per_register_reset_const_elaborated \
  tests/test_registers_parser.py::test_all_offsets_array_elaborated -v 2>&1 | tail -10
```

Expected: 2 PASSED.

- [ ] **Step 8: Run full pytest + verify --check**

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: pytest passes (count = previous-pass-count + 2); `--check` exit 0.

- [ ] **Step 9: Commit**

```bash
git add spec_validate/tools/elaborate/cpp_registers.py \
        spec_validate/tools/elaborate/sv_registers.py \
        spec_validate/tests/test_registers_parser.py \
        spec_validate/include/ni_regs.h \
        spec_validate/rtl_pkg/ni_regs_pkg.sv
git commit -m "$(cat <<'EOF'
feat(spec_validate): elaborate per-reg <REG>_RESET + ALL_OFFSETS array

Phase X.2 codegen extension. Each non-reserved register elaborates
constexpr uint32_t <REG>_RESET = N; ALL_OFFSETS[] enumerates every
non-reserved offset (with ALL_OFFSETS_COUNT). Reserved rows (e.g.
0x110 with reset_expr: null) are skipped.

Refs design doc Phase X.2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Phase X.2 — c_model RegisterFile consumes F-004 + F-005

**Files:**
- Modify: `c_model/src/register_file.cpp`
- Modify: `c_model/tests/test_register_file.cpp`

- [ ] **Step 1: Write failing new tests in test_register_file.cpp**

Append to `c_model/tests/test_register_file.cpp`:

```cpp
TEST(RegisterFile, TxnMinLatencyResetIsNonZeroFromCodegen) {
  RegisterFile rf;
  auto r = rf.read32(ni::regs::TXN_MIN_LATENCY_OFFSET);
  EXPECT_EQ(r.status, AbiResult::Ok);
  EXPECT_EQ(r.data,   ni::regs::TXN_MIN_LATENCY_RESET);  // codegen says 0xFFFF
  EXPECT_EQ(r.data,   0xFFFFu);
}

TEST(RegisterFile, KnownOffsetsMatchCodegenAllOffsets) {
  RegisterFile rf;
  // Every offset in ALL_OFFSETS must be mapped (read32 returns Ok)
  for (std::size_t i = 0; i < ni::regs::ALL_OFFSETS_COUNT; ++i) {
    auto r = rf.read32(ni::regs::ALL_OFFSETS[i]);
    EXPECT_EQ(r.status, AbiResult::Ok)
        << "offset " << std::hex << ni::regs::ALL_OFFSETS[i] << " not mapped";
  }
  // Unmapped offset must return DecErr
  auto r = rf.read32(0xFFFC);
  EXPECT_EQ(r.status, AbiResult::DecErr);
}
```

- [ ] **Step 2: Delete the ResetValuesAreZeroForNow test**

Delete `TEST(RegisterFile, ResetValuesAreZeroForNow) { ... }` (around lines 14-19 of `test_register_file.cpp`). This test cements the F-004 bug.

- [ ] **Step 3: Build and verify the new tests fail**

```bash
cd c_model/build && cmake --build . --target test_register_file
ctest -R RegisterFile.TxnMinLatencyResetIsNonZeroFromCodegen --output-on-failure
```

Expected: FAIL because `RegisterFile::reset()` currently writes zero to every offset.

- [ ] **Step 4: Replace known_offsets() to use codegen ALL_OFFSETS**

In `c_model/src/register_file.cpp`, replace the entire `known_offsets()` function and its 31-entry hand-maintained set:

```cpp
#include "register_file.hpp"
#include "ni_spec.hpp"
#include <unordered_set>

namespace ni::cmodel {

namespace {
  // Set built once from codegen-elaborated ALL_OFFSETS array.
  const std::unordered_set<uint32_t>& known_offsets() {
    static const std::unordered_set<uint32_t> s{
        ni::regs::ALL_OFFSETS,
        ni::regs::ALL_OFFSETS + ni::regs::ALL_OFFSETS_COUNT};
    return s;
  }
}
```

The hand-maintained 31-entry initializer-list block is gone; the set is built from codegen output.

- [ ] **Step 5: Replace reset() body to use per-reg <REG>_RESET**

In the same file, replace `RegisterFile::reset()`:

```cpp
void RegisterFile::reset() {
  storage_.clear();
  // Reset values come from codegen <REG>_RESET constants.
  // Hand-list the (offset, reset-constant) pairs for each non-reserved register.
  // Adding a new register requires adding one line here AND codegen elaboration
  // of <REG>_RESET — both come from the same JSON SSoT.
  storage_[ni::regs::PKT_PROBE_EN_OFFSET]            = ni::regs::PKT_PROBE_EN_RESET;
  storage_[ni::regs::PKT_PROBE_MODE_OFFSET]          = ni::regs::PKT_PROBE_MODE_RESET;
  storage_[ni::regs::PKT_WINDOW_SIZE_OFFSET]         = ni::regs::PKT_WINDOW_SIZE_RESET;
  storage_[ni::regs::PKT_BYTE_COUNT_OFFSET]          = ni::regs::PKT_BYTE_COUNT_RESET;
  storage_[ni::regs::PKT_BANDWIDTH_OFFSET]           = ni::regs::PKT_BANDWIDTH_RESET;
  storage_[ni::regs::TXN_PROBE_EN_OFFSET]            = ni::regs::TXN_PROBE_EN_RESET;
  storage_[ni::regs::TXN_THRESHOLD_0_OFFSET]         = ni::regs::TXN_THRESHOLD_0_RESET;
  storage_[ni::regs::TXN_THRESHOLD_1_OFFSET]         = ni::regs::TXN_THRESHOLD_1_RESET;
  storage_[ni::regs::TXN_THRESHOLD_2_OFFSET]         = ni::regs::TXN_THRESHOLD_2_RESET;
  storage_[ni::regs::TXN_THRESHOLD_3_OFFSET]         = ni::regs::TXN_THRESHOLD_3_RESET;
  storage_[ni::regs::TXN_BIN_0_COUNT_OFFSET]         = ni::regs::TXN_BIN_0_COUNT_RESET;
  storage_[ni::regs::TXN_BIN_1_COUNT_OFFSET]         = ni::regs::TXN_BIN_1_COUNT_RESET;
  storage_[ni::regs::TXN_BIN_2_COUNT_OFFSET]         = ni::regs::TXN_BIN_2_COUNT_RESET;
  storage_[ni::regs::TXN_BIN_3_COUNT_OFFSET]         = ni::regs::TXN_BIN_3_COUNT_RESET;
  storage_[ni::regs::TXN_BIN_4_COUNT_OFFSET]         = ni::regs::TXN_BIN_4_COUNT_RESET;
  storage_[ni::regs::TXN_MIN_LATENCY_OFFSET]         = ni::regs::TXN_MIN_LATENCY_RESET;
  storage_[ni::regs::TXN_MAX_LATENCY_OFFSET]         = ni::regs::TXN_MAX_LATENCY_RESET;
  storage_[ni::regs::TXN_TOTAL_COUNT_OFFSET]         = ni::regs::TXN_TOTAL_COUNT_RESET;
  storage_[ni::regs::ERR_STATUS_OFFSET]              = ni::regs::ERR_STATUS_RESET;
  storage_[ni::regs::ECC_UNCORR_ERR_CNT_OFFSET]      = ni::regs::ECC_UNCORR_ERR_CNT_RESET;
  storage_[ni::regs::LAST_ERR_INFO_OFFSET]           = ni::regs::LAST_ERR_INFO_RESET;
  storage_[ni::regs::IRQ_ENABLE_OFFSET]              = ni::regs::IRQ_ENABLE_RESET;
  storage_[ni::regs::ECC_CORR_ERR_CNT_OFFSET]        = ni::regs::ECC_CORR_ERR_CNT_RESET;
  storage_[ni::regs::ROUTE_PAR_ERR_CNT_OFFSET]       = ni::regs::ROUTE_PAR_ERR_CNT_RESET;
  storage_[ni::regs::AXI_PARITY_ERR_CNT_OFFSET]      = ni::regs::AXI_PARITY_ERR_CNT_RESET;
  storage_[ni::regs::PENDING_R_COUNT_OFFSET]         = ni::regs::PENDING_R_COUNT_RESET;
  storage_[ni::regs::PENDING_W_COUNT_OFFSET]         = ni::regs::PENDING_W_COUNT_RESET;
  storage_[ni::regs::QUIESCE_CTRL_OFFSET]            = ni::regs::QUIESCE_CTRL_RESET;
  storage_[ni::regs::QUIESCE_STATUS_OFFSET]          = ni::regs::QUIESCE_STATUS_RESET;
  storage_[ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET]  = ni::regs::EXCLUSIVE_MONITOR_CTRL_RESET;
  storage_[ni::regs::EXCLUSIVE_MONITOR_STATUS_OFFSET]= ni::regs::EXCLUSIVE_MONITOR_STATUS_RESET;
  last_irq_         = false;
  last_rw1c_clear_  = false;
}
```

This hand-list of `(offset, RESET_const)` pairs is a known sufficiency limitation (see F-008-equivalent: codegen could in future elaborate a `RESET_TABLE[]` array of `{offset, value}` pairs to remove this last hand-list). Documented in the design doc Phase X.2 wording about "F-005 closure: implementation shape" — accept this hand-list because each entry references a codegen symbol, no spec value is hardcoded.

- [ ] **Step 6: Verify all new tests pass**

```bash
cd c_model/build && cmake --build . --target test_register_file
ctest -R RegisterFile --output-on-failure 2>&1 | tail -15
```

Expected: 12+ tests pass (original 12, minus 1 deleted, plus 2 new = 13). Specifically `TxnMinLatencyResetIsNonZeroFromCodegen` and `KnownOffsetsMatchCodegenAllOffsets` PASS.

- [ ] **Step 7: Verify full c_model suite + drift gate**

```bash
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: full c_model passes (2 pins_smoke + 7 Flit + ~13 RegisterFile); `--check` exit 0.

- [ ] **Step 8: Commit**

```bash
git add c_model/src/register_file.cpp c_model/tests/test_register_file.cpp
git commit -m "$(cat <<'EOF'
feat(c_model): RegisterFile consumes codegen <REG>_RESET + ALL_OFFSETS

Phase X.2 c_model side. reset() now writes per-register codegen RESET
constants (TXN_MIN_LATENCY = 0xFFFF instead of 0). known_offsets() built
from ni::regs::ALL_OFFSETS, dropping the 31-entry hand-maintained set.

Tests: replaced ResetValuesAreZeroForNow (asserted F-004 bug) with
TxnMinLatencyResetIsNonZeroFromCodegen + KnownOffsetsMatchCodegenAllOffsets.

Refs design doc Phase X.2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Phase X.3 — Codegen redesign access-mode emission

**Files:**
- Modify: `spec_validate/tools/elaborate/cpp_registers.py` (replace per-reg enum class section)
- Modify: `spec_validate/tools/elaborate/sv_registers.py` (add typedef + per-reg localparam)
- Modify: `spec_validate/tests/test_registers_parser.py` (new tests)
- Modify: `spec_validate/tests/test_codegen_sv.py` (new tests)
- Regenerate: `spec_validate/include/ni_regs.h`, `spec_validate/rtl_pkg/ni_regs_pkg.sv`

- [ ] **Step 1: Write failing pytest for new C++ AccessMode shape**

Append to `spec_validate/tests/test_registers_parser.py`:

```python
def test_access_mode_enum_class_emitted():
    """ni_regs.h must expose enum class AccessMode { RO, RW, RW1C, WO } and
    per-register <REG>_ACCESS constexpr instead of 30 single-value enum classes."""
    from pathlib import Path
    text = (Path(__file__).resolve().parent.parent / "include" / "ni_regs.h").read_text()
    # The single shared enum
    assert "enum class AccessMode { RO, RW, RW1C, WO };" in text, \
        "missing single AccessMode enum class"
    # Per-register constexpr
    assert "constexpr AccessMode ERR_STATUS_ACCESS              = AccessMode::RW1C;" in text or \
           "constexpr AccessMode ERR_STATUS_ACCESS = AccessMode::RW1C;" in text, \
        "missing ERR_STATUS_ACCESS = RW1C"
    assert "AccessMode::WO" in text, "missing WO usage (EXCLUSIVE_MONITOR_CTRL)"
    # Old single-value per-reg enums must be gone
    assert "enum class ERR_STATUSAccess" not in text, \
        "old per-reg single-value enum still present"
```

Append to `spec_validate/tests/test_codegen_sv.py`:

```python
def test_sv_access_mode_typedef_and_per_reg_emitted():
    """ni_regs_pkg.sv must expose typedef access_mode_e + per-reg <REG>_ACCESS."""
    from pathlib import Path
    text = (Path(__file__).resolve().parent.parent / "rtl_pkg" / "ni_regs_pkg.sv").read_text()
    assert "typedef enum logic [1:0] { ACCESS_RO, ACCESS_RW, ACCESS_RW1C, ACCESS_WO } access_mode_e;" in text, \
        "missing access_mode_e typedef"
    assert "localparam access_mode_e ERR_STATUS_ACCESS" in text and "ACCESS_RW1C" in text
```

- [ ] **Step 2: Run to verify both fail**

```bash
cd spec_validate && py -3 -m pytest \
  tests/test_registers_parser.py::test_access_mode_enum_class_emitted \
  tests/test_codegen_sv.py::test_sv_access_mode_typedef_and_per_reg_emitted -v 2>&1 | tail -10
```

Expected: 2 FAILED (the old per-reg single-value enum is what's currently elaborated).

- [ ] **Step 3: Modify cpp_registers.py — replace per-reg enum shape**

In `spec_validate/tools/elaborate/cpp_registers.py`, find the existing function that emits per-register access enum (around lines 94-105 of pre-edit version — emits `enum class <NAME>Access { RW1C };` etc.). Replace it with:

```python
def _emit_access_mode(spec) -> list[str]:
    out = []
    out.append("// --- access mode enum + per-register constexpr ---")
    out.append("enum class AccessMode { RO, RW, RW1C, WO };")
    out.append("")
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved":
            continue
        name = r["name"].upper()
        access = r.get("access", "RW")  # default RW if missing in JSON
        # Drop "WC" if present in JSON — current design excludes WC (no consumer)
        if access == "WC":
            access = "RW"  # safe fallback; document in spec doc Out-of-Scope
        out.append(f"constexpr AccessMode {name}_ACCESS = AccessMode::{access};")
    out.append("")
    return out
```

Call `_emit_access_mode` from within `emit()`. Delete the old per-reg single-value enum emission section.

- [ ] **Step 4: Modify sv_registers.py — emit typedef + per-reg localparam**

In `spec_validate/tools/elaborate/sv_registers.py`, add inside `package ni_regs_pkg`:

```python
def _emit_sv_access_mode(spec) -> list[str]:
    out = []
    out.append("  // --- access mode typedef + per-register localparam ---")
    out.append("  typedef enum logic [1:0] { ACCESS_RO, ACCESS_RW, ACCESS_RW1C, ACCESS_WO } access_mode_e;")
    out.append("")
    for r in spec.get("registers", []):
        if r.get("kind") == "reserved":
            continue
        name = r["name"].upper()
        access = r.get("access", "RW")
        if access == "WC":
            access = "RW"
        out.append(f"  localparam access_mode_e {name}_ACCESS = ACCESS_{access};")
    out.append("")
    return out
```

Wire into `emit()`.

- [ ] **Step 5: Regenerate**

```bash
cd spec_validate && py -3 tools/codegen.py --target cpp --domain registers --out include/
cd spec_validate && py -3 tools/codegen.py --target sv  --domain registers --out rtl_pkg/
```

Inspect:

```bash
grep -E "enum class AccessMode|_ACCESS = AccessMode|access_mode_e|_ACCESS = ACCESS_" \
  spec_validate/include/ni_regs.h spec_validate/rtl_pkg/ni_regs_pkg.sv | head -15
```

Expected: shows the single `AccessMode` enum + per-register `*_ACCESS` constants in both C++ and SV.

- [ ] **Step 6: Verify both new pytest cases pass + --check exit 0**

```bash
cd spec_validate && py -3 -m pytest \
  tests/test_registers_parser.py::test_access_mode_enum_class_emitted \
  tests/test_codegen_sv.py::test_sv_access_mode_typedef_and_per_reg_emitted -v 2>&1 | tail -10
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: 2 PASSED; full pytest no regressions; `--check` exit 0.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/tools/elaborate/cpp_registers.py \
        spec_validate/tools/elaborate/sv_registers.py \
        spec_validate/tests/test_registers_parser.py \
        spec_validate/tests/test_codegen_sv.py \
        spec_validate/include/ni_regs.h \
        spec_validate/rtl_pkg/ni_regs_pkg.sv
git commit -m "$(cat <<'EOF'
refactor(spec_validate): redesign access-mode emission (Phase X.3 codegen)

Replace 30 single-value enum classes (enum class ERR_STATUSAccess { RW1C };
etc.) with single enum class AccessMode { RO, RW, RW1C, WO } plus per-register
constexpr <REG>_ACCESS. SV side: first-time emission of access_mode_e typedef
plus per-register localparam.

WC dropped from enum (no current register uses it; can re-add when needed).

Refs design doc Phase X.3.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Phase X.3 — c_model RegisterFile consumes AccessMode

**Files:**
- Modify: `c_model/src/register_file.cpp`
- Modify: `c_model/tests/test_register_file.cpp`

- [ ] **Step 1: Write 4 new failing GTest cases**

Append to `c_model/tests/test_register_file.cpp`:

```cpp
TEST(RegisterFile, WriteOneToRW1CClearsBit) {
  RegisterFile rf;
  // Setup: write some bits into ERR_STATUS (which is RW1C)
  rf.write_field(ni::regs::ERR_STATUS_OFFSET, 0xFFFFFFFF, 0x7);  // bits 0, 1, 2 set
  // Now write 0b011 — should clear bits 0 and 1, leave bit 2 set
  auto r = rf.write32(ni::regs::ERR_STATUS_OFFSET, 0x3);
  EXPECT_EQ(r.status, AbiResult::Ok);
  auto read = rf.read32(ni::regs::ERR_STATUS_OFFSET);
  EXPECT_EQ(read.data & 0x7u, 0x4u);  // only bit 2 remains
  EXPECT_TRUE(rf.last_write_cleared_rw1c_field());
}

TEST(RegisterFile, WriteToROIsSilentlyIgnored) {
  RegisterFile rf;
  // PKT_BYTE_COUNT is RO (declared so in JSON / codegen)
  uint32_t before = rf.read32(ni::regs::PKT_BYTE_COUNT_OFFSET).data;
  auto r = rf.write32(ni::regs::PKT_BYTE_COUNT_OFFSET, 0xDEADBEEF);
  EXPECT_EQ(r.status, AbiResult::Ok);  // silent ignore, no error
  uint32_t after = rf.read32(ni::regs::PKT_BYTE_COUNT_OFFSET).data;
  EXPECT_EQ(after, before);
}

TEST(RegisterFile, ReadFromWOReturnsZero) {
  RegisterFile rf;
  // EXCLUSIVE_MONITOR_CTRL is WO. Write succeeds but storage isn't exposed via read.
  auto w = rf.write32(ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET, 0x12345678);
  EXPECT_EQ(w.status, AbiResult::Ok);
  auto r = rf.read32(ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET);
  EXPECT_EQ(r.status, AbiResult::Ok);
  EXPECT_EQ(r.data, 0u);  // WO read-as-zero per access policy
}

TEST(RegisterFile, LastWriteFlagResetOnEachWrite) {
  RegisterFile rf;
  // Non-RW1C write should leave last_rw1c_clear_ false
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0x1);
  EXPECT_FALSE(rf.last_write_cleared_rw1c_field());
  // RW1C write that actually clears a bit sets the flag
  rf.write_field(ni::regs::ERR_STATUS_OFFSET, 0xFFFFFFFF, 0x1);
  rf.write32(ni::regs::ERR_STATUS_OFFSET, 0x1);
  EXPECT_TRUE(rf.last_write_cleared_rw1c_field());
}
```

Also DELETE old tests that assume the wrong RO/WO behavior, if any:

```bash
grep -n "RO\|WO\|RW1C" c_model/tests/test_register_file.cpp
```

If the existing tests assert RO writes return DecErr or similar, they conflict with new spec semantics (RO = silent ignore). Update those tests' expectations (or delete if they're redundant with the new tests).

- [ ] **Step 2: Run new tests — verify all 4 fail**

```bash
cd c_model/build && cmake --build . --target test_register_file
ctest -R RegisterFile --output-on-failure 2>&1 | tail -20
```

Expected: at least the 4 new tests FAIL (current `write32` writes storage unconditionally, no RW1C/RO/WO dispatch).

- [ ] **Step 3: Add access_mode_of switch function in register_file.cpp**

In `c_model/src/register_file.cpp`, add this static helper at top of the anonymous namespace (alongside `known_offsets`):

```cpp
static ni::regs::AccessMode access_mode_of(uint32_t offset) {
  using AM = ni::regs::AccessMode;
  switch (offset) {
    case ni::regs::PKT_PROBE_EN_OFFSET:             return ni::regs::PKT_PROBE_EN_ACCESS;
    case ni::regs::PKT_PROBE_MODE_OFFSET:           return ni::regs::PKT_PROBE_MODE_ACCESS;
    case ni::regs::PKT_WINDOW_SIZE_OFFSET:          return ni::regs::PKT_WINDOW_SIZE_ACCESS;
    case ni::regs::PKT_BYTE_COUNT_OFFSET:           return ni::regs::PKT_BYTE_COUNT_ACCESS;
    case ni::regs::PKT_BANDWIDTH_OFFSET:            return ni::regs::PKT_BANDWIDTH_ACCESS;
    case ni::regs::TXN_PROBE_EN_OFFSET:             return ni::regs::TXN_PROBE_EN_ACCESS;
    case ni::regs::TXN_THRESHOLD_0_OFFSET:          return ni::regs::TXN_THRESHOLD_0_ACCESS;
    case ni::regs::TXN_THRESHOLD_1_OFFSET:          return ni::regs::TXN_THRESHOLD_1_ACCESS;
    case ni::regs::TXN_THRESHOLD_2_OFFSET:          return ni::regs::TXN_THRESHOLD_2_ACCESS;
    case ni::regs::TXN_THRESHOLD_3_OFFSET:          return ni::regs::TXN_THRESHOLD_3_ACCESS;
    case ni::regs::TXN_BIN_0_COUNT_OFFSET:          return ni::regs::TXN_BIN_0_COUNT_ACCESS;
    case ni::regs::TXN_BIN_1_COUNT_OFFSET:          return ni::regs::TXN_BIN_1_COUNT_ACCESS;
    case ni::regs::TXN_BIN_2_COUNT_OFFSET:          return ni::regs::TXN_BIN_2_COUNT_ACCESS;
    case ni::regs::TXN_BIN_3_COUNT_OFFSET:          return ni::regs::TXN_BIN_3_COUNT_ACCESS;
    case ni::regs::TXN_BIN_4_COUNT_OFFSET:          return ni::regs::TXN_BIN_4_COUNT_ACCESS;
    case ni::regs::TXN_MIN_LATENCY_OFFSET:          return ni::regs::TXN_MIN_LATENCY_ACCESS;
    case ni::regs::TXN_MAX_LATENCY_OFFSET:          return ni::regs::TXN_MAX_LATENCY_ACCESS;
    case ni::regs::TXN_TOTAL_COUNT_OFFSET:          return ni::regs::TXN_TOTAL_COUNT_ACCESS;
    case ni::regs::ERR_STATUS_OFFSET:               return ni::regs::ERR_STATUS_ACCESS;
    case ni::regs::ECC_UNCORR_ERR_CNT_OFFSET:       return ni::regs::ECC_UNCORR_ERR_CNT_ACCESS;
    case ni::regs::LAST_ERR_INFO_OFFSET:            return ni::regs::LAST_ERR_INFO_ACCESS;
    case ni::regs::IRQ_ENABLE_OFFSET:               return ni::regs::IRQ_ENABLE_ACCESS;
    case ni::regs::ECC_CORR_ERR_CNT_OFFSET:         return ni::regs::ECC_CORR_ERR_CNT_ACCESS;
    case ni::regs::ROUTE_PAR_ERR_CNT_OFFSET:        return ni::regs::ROUTE_PAR_ERR_CNT_ACCESS;
    case ni::regs::AXI_PARITY_ERR_CNT_OFFSET:       return ni::regs::AXI_PARITY_ERR_CNT_ACCESS;
    case ni::regs::PENDING_R_COUNT_OFFSET:          return ni::regs::PENDING_R_COUNT_ACCESS;
    case ni::regs::PENDING_W_COUNT_OFFSET:          return ni::regs::PENDING_W_COUNT_ACCESS;
    case ni::regs::QUIESCE_CTRL_OFFSET:             return ni::regs::QUIESCE_CTRL_ACCESS;
    case ni::regs::QUIESCE_STATUS_OFFSET:           return ni::regs::QUIESCE_STATUS_ACCESS;
    case ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET:   return ni::regs::EXCLUSIVE_MONITOR_CTRL_ACCESS;
    case ni::regs::EXCLUSIVE_MONITOR_STATUS_OFFSET: return ni::regs::EXCLUSIVE_MONITOR_STATUS_ACCESS;
    default: return AM::RW;  // unreachable if is_mapped_ guards
  }
}
```

- [ ] **Step 4: Update is_wo_ and is_rw1c_ to use access_mode_of**

In the same file:

```cpp
bool RegisterFile::is_wo_(uint32_t offset) const {
  return access_mode_of(offset) == ni::regs::AccessMode::WO;
}
bool RegisterFile::is_rw1c_(uint32_t offset) const {
  return access_mode_of(offset) == ni::regs::AccessMode::RW1C;
}
```

- [ ] **Step 5: Update read32 — WO returns 0**

In the same file:

```cpp
AbiResponse RegisterFile::read32(uint32_t offset) {
  if (offset % 4 != 0) {
    if constexpr (ni::regs::csr_policy::MISALIGNED_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    }
    return {AbiResult::DecErr, 0};
  }
  if (!is_mapped_(offset)) {
    if constexpr (ni::regs::csr_policy::UNMAPPED_READ_IS_DECERR) {
      return {AbiResult::DecErr, 0};
    }
    return {AbiResult::Ok, 0};
  }
  // WO read-as-zero per access policy
  if (access_mode_of(offset) == ni::regs::AccessMode::WO) {
    return {AbiResult::Ok, 0};
  }
  return {AbiResult::Ok, storage_[offset]};
}
```

- [ ] **Step 6: Update write32 — RO ignore, RW1C clear bits, WO accept**

Replace the existing `write32` body:

```cpp
AbiResponse RegisterFile::write32(uint32_t offset, uint32_t value, uint8_t wstrb) {
  if (offset % 4 != 0) {
    if constexpr (ni::regs::csr_policy::MISALIGNED_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    }
    return {AbiResult::DecErr, 0};
  }
  if (!is_mapped_(offset)) {
    if constexpr (ni::regs::csr_policy::UNMAPPED_READ_IS_DECERR) {
      return {AbiResult::DecErr, 0};
    }
    return {AbiResult::Ok, 0};
  }
  if (wstrb != 0b1111) {
    if constexpr (ni::regs::csr_policy::SUB_WORD_WRITE_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    }
    return {AbiResult::Ok, 0};
  }
  using AM = ni::regs::AccessMode;
  switch (access_mode_of(offset)) {
    case AM::RO:
      // silent ignore (AXI4-Lite convention)
      last_irq_         = false;
      last_rw1c_clear_  = false;
      return {AbiResult::Ok, 0};
    case AM::RW1C: {
      // For each bit set in value, clear corresponding bit in storage
      uint32_t before = storage_[offset];
      uint32_t after  = before & ~value;
      storage_[offset] = after;
      last_irq_        = false;
      last_rw1c_clear_ = (before != after);
      return {AbiResult::Ok, 0};
    }
    case AM::WO:
      // Write accepted, storage updated even though read returns 0
      storage_[offset] = value;
      last_irq_         = false;
      last_rw1c_clear_  = false;
      return {AbiResult::Ok, 0};
    case AM::RW:
    default:
      storage_[offset] = value;
      last_irq_         = false;
      last_rw1c_clear_  = false;
      return {AbiResult::Ok, 0};
  }
}
```

- [ ] **Step 7: Rebuild and verify all RegisterFile tests pass**

```bash
cd c_model/build && cmake --build . --target test_register_file
ctest -R RegisterFile --output-on-failure 2>&1 | tail -20
```

Expected: all RegisterFile tests pass, including the 4 new access-mode tests.

- [ ] **Step 8: Verify full c_model suite + drift gate**

```bash
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: full suite passes; `--check` exit 0.

- [ ] **Step 9: Commit**

```bash
git add c_model/src/register_file.cpp c_model/tests/test_register_file.cpp
git commit -m "$(cat <<'EOF'
feat(c_model): RegisterFile dispatches on AccessMode (Phase X.3)

Consumes new codegen ni::regs::AccessMode + per-register *_ACCESS
constants. access_mode_of(offset) switch dispatches:
  RO  -> silent ignore on write (AXI4-Lite convention)
  RW1C -> write 1 clears corresponding storage bit; sets last_rw1c_clear_
  WO  -> read returns 0 (regardless of storage); write accepted
  RW  -> standard read/write

is_wo_ / is_rw1c_ delegate to access_mode_of. Adds 4 GTest cases
covering RW1C clear, RO ignore, WO read-as-zero, last-write flag reset.

Refs design doc Phase X.3.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Phase X.4 — Codegen elaborate `PADDING_FIELDS[]`

**Files:**
- Modify: `spec_validate/tools/elaborate/cpp_packet.py`
- Modify: `spec_validate/tests/test_codegen.py` (new pytest)
- Regenerate: `spec_validate/include/ni_flit_constants.h`

- [ ] **Step 1: Inspect JSON for padding field width verification**

```bash
py -3 -c "
import json
d = json.load(open('spec_validate/generated/ni_packet.json'))
for f in d['flit']['header_fields']:
    if not f.get('enabled', True):
        print(f\"{f['name']}: width={f.get('width', '?')}, lsb={f.get('lsb', '?')}, msb={f.get('msb', '?')}\")"
```

Expected: 4 fields with `enabled: false` — verify which have `width > 0` (these become PADDING_FIELDS entries) and which have `width: 0` (these are reserved placeholders, excluded).

- [ ] **Step 2: Write failing pytest**

Append to `spec_validate/tests/test_codegen.py`:

```python
def test_padding_fields_array_elaborated():
    """ni_flit_constants.h must expose PaddingFieldPos struct + PADDING_FIELDS array."""
    from pathlib import Path
    text = (Path(__file__).resolve().parent.parent / "include" / "ni_flit_constants.h").read_text()
    assert "struct PaddingFieldPos" in text, "missing PaddingFieldPos struct"
    assert "constexpr PaddingFieldPos PADDING_FIELDS[]" in text, "missing PADDING_FIELDS array"
    assert "constexpr std::size_t PADDING_FIELDS_COUNT" in text, "missing PADDING_FIELDS_COUNT"
    # At least one known padding field must appear (those with width > 0 and enabled=false)
    # Confirm in JSON which exist; route_par is a typical candidate per ni_packet.json.
    assert "route_par" in text or "flit_ecc" in text, \
        "expected at least one of route_par / flit_ecc in PADDING_FIELDS"
```

- [ ] **Step 3: Run — verify fail**

```bash
cd spec_validate && py -3 -m pytest tests/test_codegen.py::test_padding_fields_array_elaborated -v 2>&1 | tail -10
```

Expected: FAIL.

- [ ] **Step 4: Modify cpp_packet.py to elaborate PADDING_FIELDS**

In `spec_validate/tools/elaborate/cpp_packet.py`, add a helper inside `namespace ni::header { ... }` (after the existing per-field LSB/MSB/WIDTH/ENABLED section):

```python
def _emit_padding_fields(spec) -> list[str]:
    out = []
    out.append("// --- padding fields list (enabled: false, width > 0) ---")
    out.append("struct PaddingFieldPos { const char* name; int lsb; int msb; };")
    entries = []
    for f in spec["flit"]["header_fields"]:
        if f.get("enabled", True):
            continue
        if f.get("width", 1) == 0:
            continue  # width=0 reserved placeholder — no bits to check
        entries.append((f["name"], f.get("lsb"), f.get("msb")))
    out.append(f"constexpr PaddingFieldPos PADDING_FIELDS[] = {{")
    if entries:
        out.append(",\n".join(
            f"  {{ \"{n}\", {lsb}, {msb} }}" for n, lsb, msb in entries))
    out.append("};")
    out.append(f"constexpr std::size_t PADDING_FIELDS_COUNT = {len(entries)};")
    out.append("")
    return out
```

In the existing `emit()`, call `_emit_padding_fields(spec)` from within `namespace header { ... }` block, after the per-field LSB/MSB section. Also ensure `<cstddef>` is included at top for `std::size_t`.

- [ ] **Step 5: Regenerate ni_flit_constants.h**

```bash
cd spec_validate && py -3 tools/codegen.py --target cpp --domain packet --out include/
```

Inspect:

```bash
grep -A 5 "PADDING_FIELDS\|PaddingFieldPos" spec_validate/include/ni_flit_constants.h | head -20
```

Expected: shows `struct PaddingFieldPos`, `PADDING_FIELDS[]` with at least one entry, `PADDING_FIELDS_COUNT`.

- [ ] **Step 6: Verify pytest pass + --check**

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: all pass; exit 0.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/tools/elaborate/cpp_packet.py \
        spec_validate/tests/test_codegen.py \
        spec_validate/include/ni_flit_constants.h
git commit -m "$(cat <<'EOF'
feat(spec_validate): elaborate PADDING_FIELDS[] for c_model padding check

Phase X.4 codegen extension. Each header field with enabled: false AND
width > 0 becomes a PaddingFieldPos { name, lsb, msb } entry in the
elaborated PADDING_FIELDS[] array. Width=0 reserved placeholders are
excluded (no bits to check).

Enables Flit::check_padding_is_zero to iterate the array instead of
returning a hardcoded true, while preserving Invariant 2 (no spec
values in c_model code).

Refs design doc Phase X.4.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Phase X.4 — c_model Flit consume + API quarantine

**Files:**
- Modify: `c_model/include/flit.hpp`
- Modify: `c_model/tests/test_flit.cpp`

- [ ] **Step 1: Write failing tightened PaddingFieldStaysZero + remove old**

In `c_model/tests/test_flit.cpp`:

Delete the current `TEST(Flit, PaddingFieldStaysZero) { ... }` (tautological, around line 42-46).

Add:

```cpp
TEST(Flit, PaddingBitSetCausesCheckToFail) {
  // Tighten F-002: drive a bit into a padding field range and assert check fails
  std::array<uint8_t, Flit::WIDTH_BYTES> raw{};
  ASSERT_GT(ni::header::PADDING_FIELDS_COUNT, 0u) << "no padding fields elaborated — codegen issue";
  int lsb = ni::header::PADDING_FIELDS[0].lsb;
  int byte = lsb / 8, off = lsb % 8;
  raw[byte] |= (1u << off);
  Flit f(raw);
  EXPECT_FALSE(f.check_padding_is_zero()) \
      << "padding bit at " << ni::header::PADDING_FIELDS[0].name << " was set, check should fail";
}

TEST(Flit, AllZeroRawPassesPaddingCheck) {
  Flit f;  // default-constructed, all zero
  EXPECT_TRUE(f.check_padding_is_zero());
}
```

- [ ] **Step 2: Run — verify new tests fail**

```bash
cd c_model/build && cmake --build . --target test_flit
ctest -R Flit.PaddingBitSetCausesCheckToFail --output-on-failure
```

Expected: FAIL because `check_padding_is_zero()` is currently the stub `return true;`.

- [ ] **Step 3: Update check_padding_is_zero in flit.hpp**

In `c_model/include/flit.hpp`, replace `inline bool Flit::check_padding_is_zero() const { return true; }` (around the existing stub line) with:

```cpp
inline bool Flit::check_padding_is_zero() const {
  // Iterate codegen-elaborated PADDING_FIELDS; each entry has {name, lsb, msb}.
  for (std::size_t i = 0; i < ni::header::PADDING_FIELDS_COUNT; ++i) {
    int lsb = ni::header::PADDING_FIELDS[i].lsb;
    int msb = ni::header::PADDING_FIELDS[i].msb;
    for (int bit = lsb; bit <= msb; ++bit) {
      int byte = bit / 8, off = bit % 8;
      if ((raw_[byte] >> off) & 1u) {
        return false;
      }
    }
  }
  return true;
}
```

No hand-listed field names in c_model code — every name comes from codegen `PADDING_FIELDS`.

- [ ] **Step 4: Remove set_payload_channel and get_payload_channel from flit.hpp**

In `c_model/include/flit.hpp`:
- Delete the two public method declarations (around lines 22-23): `void set_payload_channel(...)` and `std::vector<uint8_t> get_payload_channel(...)`
- Delete the two inline definitions further down: `inline void Flit::set_payload_channel(...)` and `inline std::vector<uint8_t> Flit::get_payload_channel(...)`
- Remove the now-unused `#include <vector>` at top if nothing else uses it

- [ ] **Step 5: Remove any test referencing payload channel API**

```bash
grep -n "payload_channel\|set_payload\|get_payload" c_model/tests/test_flit.cpp
```

If any TEST() macro tests `set_payload_channel` or `get_payload_channel`, delete it.

- [ ] **Step 6: Build and verify all Flit tests pass**

```bash
cd c_model/build && cmake --build . --target test_flit
ctest -R Flit --output-on-failure 2>&1 | tail -15
```

Expected: all Flit tests pass including the 2 new tightened tests.

- [ ] **Step 7: Verify full c_model + drift gate**

```bash
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: all pass; `--check` exit 0.

- [ ] **Step 8: Commit**

```bash
git add c_model/include/flit.hpp c_model/tests/test_flit.cpp
git commit -m "$(cat <<'EOF'
feat(c_model): Flit consumes PADDING_FIELDS + remove payload-channel stubs

Phase X.4 c_model side. check_padding_is_zero now iterates codegen-
elaborated ni::header::PADDING_FIELDS array (no hand-listed names).
Tightened PaddingFieldStaysZero test (was tautological) now constructs
a Flit with a bit set in a padding range and asserts the check fails.

Removes Flit::set_payload_channel and Flit::get_payload_channel from
public API. They were no-op stubs (F-003 deferred to Layer B). Re-
introduce when F-003 closes.

Refs design doc Phase X.4.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Phase X.5 — `SUFFICIENCY_FINDINGS.md` final disposition

**Files:**
- Modify: `c_model/SUFFICIENCY_FINDINGS.md`

- [ ] **Step 1: Rewrite SUFFICIENCY_FINDINGS.md per final disposition table**

Replace the entire content of `c_model/SUFFICIENCY_FINDINGS.md` with:

```markdown
# c_model First-Round Sufficiency Findings — Final Disposition

End-state: 0 PENDING, 5 RESOLVED, 2 DEFERRED with re-open triggers.

Each finding lists: gap, where surfaced, status, resolution evidence.

## F-001 — codegen does not elaborate `HeaderField` enum
- Surfaced: `Flit::set_header_field` in Task 10 (first round)
- Status: **DEFERRED**
- Re-open trigger: Layer B unit starts (any unit consuming Flit header field by name)
- Current workaround: hand-rolled string dispatch in `flit.hpp::detail::header_field_pos` (covers 6 named fields)

## F-002 — codegen does not elaborate padding-field list
- Surfaced: `Flit::check_padding_is_zero` was stub in Task 10
- Status: **RESOLVED** (Phase X.4)
- Resolution: codegen elaborates `ni::header::PADDING_FIELDS[]` array of `{name, lsb, msb}` for each header field with `enabled: false` AND `width > 0`. c_model `check_padding_is_zero` iterates this array (no hand-listed names).

## F-003 — codegen does not elaborate per-channel payload field positions
- Surfaced: `Flit::set_payload_channel` / `get_payload_channel` were stubs in Task 10
- Status: **DEFERRED**
- Re-open trigger: Layer B / Stage 2 payload pack/unpack work begins
- Action taken: removed `set_payload_channel` / `get_payload_channel` from `Flit` public API (Phase X.4 quarantine). Re-introduce when this finding closes.

## F-004 — codegen does not elaborate per-register reset value
- Surfaced: `RegisterFile::reset` was all-zero in Task 12
- Status: **RESOLVED** (Phase X.2)
- Resolution: codegen elaborates `constexpr uint32_t <REG>_RESET = N;` per non-reserved register. c_model `reset()` writes per-register codegen value. Reserved rows (e.g. `0x110` with `reset_expr: null`) are skipped.

## F-005 — codegen does not elaborate ALL_OFFSETS[] array
- Surfaced: `RegisterFile::known_offsets_` was a 31-entry hand-maintained set in Task 12
- Status: **RESOLVED** (Phase X.2)
- Resolution: codegen elaborates `ni::regs::ALL_OFFSETS[]` + `ALL_OFFSETS_COUNT`. c_model builds `known_offsets` from these.

## F-006 — codegen access-mode emission was awkward (per-register single-value enum class)
- Surfaced: `RegisterFile::is_wo_` / `is_rw1c_` were stubs returning false in Task 12. Codegen DID emit symbols but in an unusable shape.
- Status: **RESOLVED** (Phase X.3)
- Resolution: codegen redesigned to single `enum class AccessMode { RO, RW, RW1C, WO }` + per-register `constexpr <REG>_ACCESS`. c_model `access_mode_of(offset)` switch dispatches `read32` / `write32` per mode. RW1C clears bits, WO read-as-zero, RO silent-ignore writes.

## F-007 — RegisterFile ABI dispatch did not consume csr_policy sentinels
- Surfaced: `RegisterFile::read32` / `write32` had hardcoded DecErr for misaligned/sub-word
- Status: **RESOLVED** (first round, Task 12 fix `f6e0222`)
- Resolution: `if constexpr (ni::regs::csr_policy::*_IS_SLVERR)` dispatches on codegen-elaborated csr_policy sentinels.

---

## Process lesson recorded

The first-round Phase 1 sufficiency-findings policy did not require the implementer to `grep` codegen output before classifying a gap as "codegen does not elaborate X". F-002 and F-006 were misclassified — the symbols existed but the implementer wrote stubs anyway. Future rounds must include a **consume audit**: for each apparent missing symbol, `grep` the existing elaborated headers first.
```

- [ ] **Step 2: Verify all phase gates still green**

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "check_exit=$?"
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5
```

Expected: all green (no code changes in this step, only doc).

- [ ] **Step 3: Commit**

```bash
git add c_model/SUFFICIENCY_FINDINGS.md
git commit -m "$(cat <<'EOF'
docs(c_model): SUFFICIENCY_FINDINGS final disposition (Phase X.5)

0 PENDING / 5 RESOLVED / 2 DEFERRED (with re-open triggers documented):

 RESOLVED this round:
   F-002 padding fields (Phase X.4)
   F-004 per-reg reset    (Phase X.2)
   F-005 ALL_OFFSETS      (Phase X.2)
   F-006 access mode      (Phase X.3, redesign + consume)

 Already RESOLVED:
   F-007 csr_policy       (first round, Task 12 fix)

 DEFERRED:
   F-001 HeaderField enum         (trigger: Layer B starts)
   F-003 payload field positions  (trigger: Layer B / Stage 2 payload work)

Refs design doc Phase X.5.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## End-of-Plan Verification

After all 9 tasks, run from worktree root:

```bash
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3
cd spec_validate && py -3 tools/codegen.py --check; echo "codegen_check_exit=$?"
cd spec_validate && py -3 tools/gen_inventory.py --check; echo "inventory_check_exit=$?"
cd c_model/build && ctest --output-on-failure 2>&1 | tail -5

grep -rn "ni::blocks\|ni_blocks\.h" c_model/ spec_validate/include/ spec_validate/rtl_pkg/ 2>&1
grep -n "ALL_OFFSETS\|_RESET\|AccessMode\|PADDING_FIELDS" spec_validate/include/ni_regs.h spec_validate/include/ni_flit_constants.h | head -15
```

Expected:
- pytest passes (count = baseline minus blocks tests plus new tests for Phase X.2/X.3/X.4 codegen)
- `--check` exit 0
- c_model ctest 21+ tests pass (originally 21; X.2 +1 test/-1 test = 21; X.3 +4 = 25; X.4 +1/-1 = 25)
- Zero `ni::blocks` references in c_model or codegen output
- All new symbols present in regenerated headers

`SUFFICIENCY_FINDINGS.md` shows 0 PENDING.

---

## Self-Review Notes

- **Spec coverage**: Every requirement from spec doc Phase X.1 through X.5 has a corresponding task with explicit step-by-step instructions.
- **TDD pattern** applied to Tasks 2, 3, 4, 5, 6, 7 (codegen extensions + c_model consume).
- **Mechanical pattern** applied to Tasks 1, 8 (deletion + doc edit).
- **Phase-gate rule** enforced at end of each task (all 3 gates green check).
- **No placeholders**: every code block contains actual code; bash commands are complete with expected output; no "implement X" without showing X.
- **Type consistency**: `ni::regs::AccessMode`, `<REG>_ACCESS`, `<REG>_RESET`, `ALL_OFFSETS`, `PADDING_FIELDS`, `PaddingFieldPos`, `access_mode_of` — names used in later tasks match definitions in earlier tasks.
- **Carry-over discipline**: design doc decisions (RO silent ignore, WC dropped, access_mode_of as c_model switch, F-002 via codegen array not hand-list) flow through to specific code in tasks.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-28-scope-correction.md`.

Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, two-stage review between tasks, fast iteration. Same worktree.

**2. Inline Execution** — `executing-plans` skill, batch execution with checkpoints. Same worktree.

Which approach?
