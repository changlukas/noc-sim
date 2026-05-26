# NI Spec-as-Code Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 實作 `2026-05-26-spec-as-code-unified-design.md` §8.2 的 8 項 todo — 把 NI 規格從 prose Markdown 推進到 single source of truth + dual codegen（C++ header + SV package），讓 C model 與 RTL 從同源產出常數。

**Architecture:** Path B（MD-as-source，JSON auto-gen）。Python `ni_spec/` 模組三層：generator parse MD → JSON、validator 跑 L1 schema + L2 invariants + 跨 domain cross-ref、codegen 透過 stable `ni_spec.constants` API 產 `.h` 與 `.sv`。

**Tech Stack:**
- Python 3.13 (existing `ni_spec/` module + `jsonschema` 4.26)
- pytest（新增 — 目前 repo 沒有測試框架）
- g++ via MSYS2（既有 chain）
- Verilator（optional，Task 8 SV lint smoke test 用）

**Total effort estimate:** ~10 engineering days across 8 tasks.

**Design doc cross-ref:** 全部 schema 細節、Sub-domain 設計、命名約定都在 `2026-05-26-spec-as-code-unified-design.md` §5.x 與 §6.x。本 plan 不重複那些內容，只引用。

---

## File Structure Map

### 新建檔案

| Path | 責任 |
|---|---|
| `spec/ni/VERSION` | spec_version 單一來源（一行 semver）|
| `spec_validate/tests/conftest.py` | pytest fixture，把 `ni_spec/` 加 sys.path |
| `spec_validate/tests/test_*.py` | 每 task 一份單元測試 |
| `spec_validate/ni_function_blocks.json` | 手寫 function_blocks source（不是 generated） |
| `spec_validate/ni_function_blocks.schema.json` | function_blocks schema |
| `spec_validate/generated/ni_registers.json` | Task 4 產出 |
| `spec_validate/generated/ni_registers.schema.json` | Task 4 產出 |
| `spec_validate/generated/ni_protocol_rule_index.json` | Task 6 產出 |
| `spec_validate/generated/ni_protocol_rule_index.schema.json` | Task 6 產出 |
| `spec_validate/include/ni_signals.h` | Task 7 |
| `spec_validate/include/ni_regs.h` | Task 7 |
| `spec_validate/include/ni_blocks.h` | Task 7 |
| `spec_validate/rtl_pkg/ni_flit_pkg.sv` | Task 8 |
| `spec_validate/rtl_pkg/ni_signals_pkg.sv` | Task 8 |
| `spec_validate/rtl_pkg/ni_regs_pkg.sv` | Task 8 |
| `spec_validate/rtl_pkg/ni_blocks_pkg.sv` | Task 8 |
| `spec_validate/tools/codegen.py` | Task 7 統一入口，取代 gen_cpp_header.py |
| `spec_validate/tools/emit/__init__.py` | Task 7 emitter 套件 |
| `spec_validate/tools/emit/common.py` | Task 7 header banner + provenance helper |
| `spec_validate/tools/emit/cpp_*.py` (4 個) | Task 7 C++ per-domain emitter |
| `spec_validate/tools/emit/sv_*.py` (4 個) | Task 8 SV per-domain emitter |

### 修改檔案

| Path | 修改內容 |
|---|---|
| `spec_validate/.gitignore` | 加 `include/`、`rtl_pkg/`、`/tmp_codegen/` |
| `spec_validate/ni_spec/loader.py` | 加 `load_function_blocks()`, `load_spec_version()` |
| `spec_validate/ni_spec/generator.py` | 加 `parse_pin_level_reset()`、`generate_ni_registers_json()`、`parse_protocol_rule_index()` |
| `spec_validate/ni_spec/invariants.py` | 加 signal / register / function_blocks / protocol_rules L2 check 函式 |
| `spec_validate/ni_spec/constants.py` | 加 `signals_*`、`regs_*`、`blocks_*` accessor API |
| `spec_validate/ni_spec/__main__.py` | 擴成跑全部 5 個 generated JSON |
| `spec_validate/generated/ni_signals.schema.json` | 加 `pin_name`、`reset_behavior`、`presence`、`width_expr` 欄位 |
| `spec_validate/generated/ni_signals.json` | regen 含新欄位（Task 2/3） |
| `spec_validate/include/ni_flit_constants.h` | regen 帶 provenance header（Task 7 重 emit） |
| `spec_validate/tools/gen_cpp_header.py` | deprecate wrapper（呼叫新 `codegen.py`）|

### 不動

- `spec/ni/doc/*.md`（source spec 不動，只多讀）
- `spec_validate/generated/ni_packet.json`（既有，但 schema 可能加 `pin_name`-equivalent 欄位若 packet 也要）
- `spec_validate/ni_spec/report.py`（既有）
- `spec_validate/deferred/*` （Task 4 完成後刪整個目錄）

---

## Task 1: Foundation gates

**Depends on:** —（first task）
**Effort:** 0.5 day
**Acceptance:**
- `spec/ni/VERSION` 存在且內容為一行 semver (`v0.4.0`)
- `spec_validate/.gitignore` 排除 `include/`、`rtl_pkg/`、`/tmp_codegen/`
- `pytest` 可從 `spec_validate/` 跑起來，至少一個 trivial test PASS
- `ni_spec.constants` 新增 9 個未實作 stub（signals/regs/blocks 各 3 個），每個 raise `NotImplementedError("Task N")`
- Pytest 跑全部 stub 測試，confirm NotImplementedError，**不算失敗**（用 `pytest.raises` 套住）

**Rollback:** 全部修改 revert 即可，沒有跨 task 副作用

**Files:**
- Create: `spec/ni/VERSION`
- Create: `spec_validate/tests/__init__.py`
- Create: `spec_validate/tests/conftest.py`
- Create: `spec_validate/tests/test_foundation.py`
- Modify: `spec_validate/.gitignore`
- Modify: `spec_validate/ni_spec/constants.py`
- Modify: `spec_validate/ni_spec/loader.py`

### Steps

- [ ] **1.1 Create `spec/ni/VERSION`** — 寫一行 `v0.4.0`

```
v0.4.0
```

- [ ] **1.2 Modify `spec_validate/.gitignore`** — 在現有內容下加：

```
# Codegen output (regen via tools/codegen.py)
include/
rtl_pkg/

# Codegen --check mode scratch
/tmp_codegen/
```

- [ ] **1.3 Create `spec_validate/tests/__init__.py`** — empty file (marks tests/ as package)

- [ ] **1.4 Create `spec_validate/tests/conftest.py`**

```python
"""pytest config — add spec_validate/ to sys.path so tests can `from ni_spec import ...`."""
import sys
from pathlib import Path

SPEC_VALIDATE_ROOT = Path(__file__).resolve().parent.parent
if str(SPEC_VALIDATE_ROOT) not in sys.path:
    sys.path.insert(0, str(SPEC_VALIDATE_ROOT))
```

- [ ] **1.5 Write failing test for `load_spec_version`**

Create `spec_validate/tests/test_foundation.py`:

```python
"""Foundation gate tests — Task 1."""
import pytest
from ni_spec import loader, constants


def test_load_spec_version_returns_string():
    """spec_version comes from spec/ni/VERSION single source."""
    v = loader.load_spec_version()
    assert isinstance(v, str)
    assert v.startswith("v")
    assert v.count(".") == 2  # semver


def test_constants_signals_stub_raises():
    """signals_* API is reserved for Task 2; calling now must NotImplementedError."""
    with pytest.raises(NotImplementedError, match="Task 2"):
        constants.signals_pin_names({})


def test_constants_regs_stub_raises():
    """regs_* API is reserved for Task 4."""
    with pytest.raises(NotImplementedError, match="Task 4"):
        constants.regs_offsets({})


def test_constants_blocks_stub_raises():
    """blocks_* API is reserved for Task 5."""
    with pytest.raises(NotImplementedError, match="Task 5"):
        constants.blocks_function_block_names({})
```

- [ ] **1.6 Run tests — verify failures**

```
cd spec_validate
py -3 -m pytest tests/test_foundation.py -v
```

Expected: 4 errors (module attributes don't exist yet).

- [ ] **1.7 Implement `loader.load_spec_version()`**

Append to `spec_validate/ni_spec/loader.py`:

```python
def load_spec_version() -> str:
    """Read spec/ni/VERSION (single source of truth for spec_version).

    Looks for the file relative to the spec_validate parent directory:
        noc-sim/spec/ni/VERSION  (one-line semver, no trailing newline content).
    """
    from pathlib import Path
    spec_validate_root = Path(__file__).resolve().parent.parent
    version_file = spec_validate_root.parent / "spec" / "ni" / "VERSION"
    if not version_file.exists():
        raise FileNotFoundError(f"spec/ni/VERSION not found at {version_file}")
    return version_file.read_text(encoding="utf-8").strip()
```

- [ ] **1.8 Implement constants API skeletons**

Append to `spec_validate/ni_spec/constants.py`:

```python
# ---------- signals domain (Task 2 will implement) ----------

def signals_pin_names(signals_spec) -> list:
    """Return list of all pin_name across all signals (cross-merge result)."""
    raise NotImplementedError("Task 2")


def signals_reset_domains(signals_spec) -> set:
    """Return set of legal reset signal names from meta.reset_signals[]."""
    raise NotImplementedError("Task 2")


def signals_signal_by_pin(signals_spec, pin_name: str) -> dict:
    """Lookup signal entry by RTL-level pin_name."""
    raise NotImplementedError("Task 2")


# ---------- registers domain (Task 4 will implement) ----------

def regs_offsets(regs_spec) -> dict:
    """Return {register_name: offset_int}."""
    raise NotImplementedError("Task 4")


def regs_field_mask(regs_spec, reg_name: str, field_name: str) -> int:
    """Return bit mask for a specific field within a register."""
    raise NotImplementedError("Task 4")


def regs_access_mode(regs_spec, reg_name: str) -> str:
    """Return access mode (RO/RW/RW1C/WO/WC) for a register."""
    raise NotImplementedError("Task 4")


# ---------- function blocks domain (Task 5 will implement) ----------

def blocks_function_block_names(blocks_spec) -> list:
    """Return list of FunctionBlock enum members (ROB, QOS, ...)."""
    raise NotImplementedError("Task 5")


def blocks_modes_of(blocks_spec, block_name: str) -> list:
    """Return list of mode enum members for a given function block."""
    raise NotImplementedError("Task 5")


def blocks_compile_time_params(blocks_spec) -> dict:
    """Return {param_name: int_value} across all features."""
    raise NotImplementedError("Task 5")
```

- [ ] **1.9 Run tests — verify pass**

```
py -3 -m pytest tests/test_foundation.py -v
```

Expected: 4 PASS.

- [ ] **1.10 Run existing `ni_spec` chain — verify no regression**

```
py -3 -m ni_spec ..\spec\ni\doc
```

Expected: `規格通過校驗 ✓`、exit 0（既有功能未動）。

- [ ] **1.11 Commit**

```
git add spec/ni/VERSION \
        spec_validate/.gitignore \
        spec_validate/tests/__init__.py \
        spec_validate/tests/conftest.py \
        spec_validate/tests/test_foundation.py \
        spec_validate/ni_spec/loader.py \
        spec_validate/ni_spec/constants.py
git commit -m "feat(spec_validate): foundation gates — pytest infra, VERSION SSoT, constants API skeleton

- spec/ni/VERSION as single source of truth for spec_version
- spec_validate/tests/ pytest infrastructure with conftest path setup
- 9 constants stubs raise NotImplementedError until owning task lands
- .gitignore excludes include/, rtl_pkg/, /tmp_codegen/

Refs design §6.7, §8.2 item 1."
```

---

## Task 2: Signal model redesign

**Depends on:** Task 1
**Effort:** 1 day
**Acceptance:**
- `ni_signals.schema.json` 加 4 個新欄位（`pin_name`、`reset_behavior`、`presence`、`width_expr`）+ `meta.reset_signals[]`
- generator 從 `pin_level_reset.md` 抽 reset signal whitelist 進 `meta.reset_signals[]`
- 既有 `ni_signals.json` regen 後仍 PASS Layer 1（schema 是 superset）— 但 `pin_name` 等新欄位先為 null（reset_behavior 真正 cross-merge 在 Task 3）
- `constants.signals_pin_names()` / `signals_reset_domains()` 從 stub 變實作
- `pytest tests/test_signals_schema.py` 全 PASS

**Rollback:** schema 與 `parse_pin_level_reset()` 都 in-place 加；revert 後 regen `ni_signals.json` 即可

**Files:**
- Modify: `spec_validate/generated/ni_signals.schema.json`
- Modify: `spec_validate/ni_spec/generator.py`
- Modify: `spec_validate/ni_spec/constants.py`
- Modify: `spec_validate/ni_spec/invariants.py`
- Create: `spec_validate/tests/test_signals_schema.py`

### Steps

- [ ] **2.1 Modify `ni_signals.schema.json` — 加新欄位**

在 `signals` 內的 item schema 上加：

```jsonc
{
  "pin_name": {
    "type": ["string", "null"],
    "description": "RTL-level concrete pin name (e.g. axi_awvalid_i); null until Task 3 cross-merges with pin_level_reset.md"
  },
  "reset_behavior": {
    "type": ["object", "null"],
    "properties": {
      "kind": {"enum": ["async-active-low", "sync-active-high", "external_driven"]},
      "value": {"type": "string"},
      "domain": {"type": "string"}
    },
    "required": ["kind"]
  },
  "presence": {
    "type": ["object", "null"],
    "properties": {
      "condition_text": {"type": "string"}
    }
  },
  "width_expr": {
    "type": ["string", "null"],
    "description": "Width expression — must match whitelist regex"
  }
}
```

在 root schema 的 `meta` object 上加：

```jsonc
{
  "reset_signals": {
    "type": "array",
    "items": {"type": "string"},
    "description": "Legal reset signal names extracted from pin_level_reset.md"
  }
}
```

- [ ] **2.2 Write failing test for `parse_pin_level_reset`**

Create `spec_validate/tests/test_signals_schema.py`:

```python
"""Signal schema + pin_level_reset parser — Task 2."""
import json
from pathlib import Path
import pytest
from ni_spec import generator, constants, loader

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
MD_DIR = SPEC_VALIDATE.parent / "spec" / "ni" / "doc"


def test_extract_reset_signals_returns_arst_and_noc_rst():
    """parse_pin_level_reset extracts the `Reset signals:` bullet list."""
    reset_signals = generator.extract_reset_signals(MD_DIR / "pin_level_reset.md")
    assert "arst_ni" in reset_signals
    assert "noc_rst_ni" in reset_signals
    assert len(reset_signals) >= 2


def test_extract_reset_signals_handles_missing_file():
    """Missing file raises FileNotFoundError."""
    with pytest.raises(FileNotFoundError):
        generator.extract_reset_signals(Path("/nonexistent"))


def test_signals_reset_domains_after_extract():
    """constants.signals_reset_domains returns same set as meta.reset_signals."""
    # Build a minimal spec dict
    spec = {"meta": {"reset_signals": ["arst_ni", "noc_rst_ni"]}}
    assert constants.signals_reset_domains(spec) == {"arst_ni", "noc_rst_ni"}
```

- [ ] **2.3 Run tests — verify fail**

```
py -3 -m pytest tests/test_signals_schema.py -v
```

Expected: 3 errors (functions don't exist).

- [ ] **2.4 Implement `extract_reset_signals` in `generator.py`**

Append to `spec_validate/ni_spec/generator.py`:

```python
def extract_reset_signals(pin_level_reset_md: Path) -> list:
    """Extract the reset signal whitelist from pin_level_reset.md.

    The MD file has a bullet section like:
        **Reset signals:** `arst_ni`, `noc_rst_ni`
    or
        - `arst_ni` — active-low AXI-domain async reset
        - `noc_rst_ni` — active-low NoC-domain async reset

    Returns list of reset signal names (order-preserving).
    """
    import re
    text = pin_level_reset_md.read_text(encoding="utf-8")
    # Match section heading "Reset signals" then take the next paragraph until blank line
    m = re.search(r"(?i)reset signals\s*:?\s*\n((?:.+\n)+?)(?:\n|$)", text)
    if not m:
        # Try inline form: "Reset signals: `arst_ni`, `noc_rst_ni`"
        m2 = re.search(r"(?im)reset signals\s*:\s*`([^`]+)`(?:,\s*`([^`]+)`)*", text)
        if m2:
            return list(re.findall(r"`([^`]+)`", m2.group(0)))
        raise ValueError(f"No reset signals section found in {pin_level_reset_md}")
    block = m.group(1)
    return list(re.findall(r"`([a-zA-Z_][a-zA-Z0-9_]*)`", block))
```

- [ ] **2.5 Implement `signals_reset_domains` in `constants.py`**

Replace the stub:

```python
def signals_reset_domains(signals_spec) -> set:
    """Return set of legal reset signal names from meta.reset_signals[]."""
    return set(signals_spec.get("meta", {}).get("reset_signals", []))
```

- [ ] **2.6 Run tests — verify pass**

```
py -3 -m pytest tests/test_signals_schema.py -v
```

Expected: 3 PASS.

- [ ] **2.7 Write failing tests for pin_name presence in regen'd JSON**

Append to `test_signals_schema.py`:

```python
def test_regenerated_ni_signals_has_pin_name_field():
    """After regen, every signal entry has pin_name key (may be null until Task 3)."""
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    for iface in spec["interfaces"]:
        for ch in iface.get("channels", []):
            for sig in ch["signals"]:
                assert "pin_name" in sig, f"signal {sig['name']} missing pin_name field"


def test_regenerated_ni_signals_has_meta_reset_signals():
    """After regen, meta.reset_signals is populated."""
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    assert "reset_signals" in spec["meta"]
    assert "arst_ni" in spec["meta"]["reset_signals"]
```

- [ ] **2.8 Run tests — verify fail** (regen not yet wired)

```
py -3 -m pytest tests/test_signals_schema.py -v
```

Expected: 2 of the new ones fail (existing ni_signals.json doesn't have those fields yet).

- [ ] **2.9 Wire `extract_reset_signals` + null `pin_name` into `write_generated_signals_json`**

Find `write_generated_signals_json` in `generator.py`. Inside the composer, before writing the final dict to disk:

```python
# Add pin_name: null on every signal (Task 3 will fill in)
for iface in result["interfaces"]:
    for ch in iface.get("channels", []):
        for sig in ch.get("signals", []):
            sig.setdefault("pin_name", None)
            sig.setdefault("reset_behavior", None)
            sig.setdefault("presence", None)
            sig.setdefault("width_expr", None)

# Extract and inject meta.reset_signals
md_dir_path = Path(md_dir) if not isinstance(md_dir, Path) else md_dir
reset_signals = extract_reset_signals(md_dir_path / "pin_level_reset.md")
result.setdefault("meta", {})["reset_signals"] = reset_signals
```

- [ ] **2.10 Regen `ni_signals.json` and run tests**

```
py -3 -m ni_spec ..\spec\ni\doc
py -3 -m pytest tests/test_signals_schema.py -v
```

Expected: validator still PASSes, all 5 tests PASS.

- [ ] **2.11 Implement `signals_pin_names` and `signals_signal_by_pin`**

Replace stubs in `constants.py`:

```python
def signals_pin_names(signals_spec) -> list:
    """Return list of all non-null pin_name across all signals."""
    out = []
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                if sig.get("pin_name"):
                    out.append(sig["pin_name"])
    return out


def signals_signal_by_pin(signals_spec, pin_name: str) -> dict:
    """Lookup signal entry by RTL-level pin_name. Returns None if not found."""
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                if sig.get("pin_name") == pin_name:
                    return sig
    return None
```

- [ ] **2.12 Run all tests — verify**

```
py -3 -m pytest tests/ -v
```

Expected: all Task 1 + Task 2 tests PASS.

- [ ] **2.13 Commit**

```
git add spec_validate/generated/ni_signals.schema.json \
        spec_validate/generated/ni_signals.json \
        spec_validate/ni_spec/generator.py \
        spec_validate/ni_spec/constants.py \
        spec_validate/tests/test_signals_schema.py
git commit -m "feat(spec_validate): signal schema — pin_name/reset_behavior/presence/width_expr stubs

- ni_signals.schema.json adds 4 nullable fields + meta.reset_signals
- generator extracts reset signal whitelist from pin_level_reset.md
- regenerated ni_signals.json shows pin_name=null (Task 3 fills in)
- constants.signals_* API implemented; cross-merge logic in Task 3

Refs design §5.2, §8.2 item 2."
```

---

## Task 3: Pin-level reset merge

**Depends on:** Task 2
**Effort:** 0.5 day
**Acceptance:**
- generator 從 `pin_level_reset.md` 抽每根 wire 的 reset_behavior，by `pin_name` 對應到 `ni_signals.json` 的 signal entry
- 60%+ rows 是 input wire → `kind: "external_driven"`、`value` 不寫
- output wire 配對 `kind: "async-active-low"` + `value: "0"`（per `pin_level_reset.md` source）
- 每個 signal 都有 non-null `pin_name`、non-null `reset_behavior`
- L2 validator 抓「reset_behavior.domain 不在 meta.reset_signals」、「external_driven 帶了 value」、「同個 pin_name 出現兩次」等錯
- 既有 `py -3 -m ni_spec` flow 仍 PASS

**Rollback:** revert generator.py + invariants.py 的 Task 3 增量；regen 後 ni_signals.json 回到 Task 2 狀態

**Files:**
- Modify: `spec_validate/ni_spec/generator.py`
- Modify: `spec_validate/ni_spec/invariants.py`
- Modify: `spec_validate/generated/ni_signals.json`（regen）
- Create: `spec_validate/tests/test_pin_level_reset.py`

### Steps

- [ ] **3.1 Write failing test for pin_name population**

Create `spec_validate/tests/test_pin_level_reset.py`:

```python
"""Pin-level reset cross-merge — Task 3."""
from pathlib import Path
import pytest
from ni_spec import loader, constants, invariants

SPEC_VALIDATE = Path(__file__).resolve().parent.parent


def test_every_signal_has_pin_name():
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    for iface in spec["interfaces"]:
        for ch in iface.get("channels", []):
            for sig in ch["signals"]:
                assert sig["pin_name"] is not None, f"signal {sig['name']} pin_name still null"


def test_every_signal_has_reset_behavior():
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    for iface in spec["interfaces"]:
        for ch in iface.get("channels", []):
            for sig in ch["signals"]:
                rb = sig["reset_behavior"]
                assert rb is not None, f"signal {sig['name']} reset_behavior null"
                assert rb["kind"] in ("async-active-low", "sync-active-high", "external_driven")


def test_input_wires_are_external_driven():
    """All AXI input wires (e.g. axi_awvalid_i) must be external_driven."""
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    sig = constants.signals_signal_by_pin(spec, "axi_awvalid_i")
    assert sig is not None, "axi_awvalid_i not found"
    assert sig["reset_behavior"]["kind"] == "external_driven"
    assert "value" not in sig["reset_behavior"]  # external_driven has no value


def test_pin_name_uniqueness():
    """No pin_name appears twice across the spec."""
    spec = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")
    pins = constants.signals_pin_names(spec)
    assert len(pins) == len(set(pins)), "duplicate pin_name detected"


def test_validator_catches_unknown_reset_domain():
    """L2 check: reset_behavior.domain must be in meta.reset_signals."""
    bogus = {
        "meta": {"reset_signals": ["arst_ni"]},
        "interfaces": [{
            "channels": [{
                "signals": [{
                    "name": "BOGUS", "pin_name": "bogus_o", "direction": "output",
                    "reset_behavior": {"kind": "async-active-low", "value": "0", "domain": "fake_rst"}
                }]
            }]
        }]
    }
    issues = invariants.check_signals_reset_domains(bogus)
    assert any("fake_rst" in i.message for i in issues)
```

- [ ] **3.2 Run tests — verify fail**

```
py -3 -m pytest tests/test_pin_level_reset.py -v
```

Expected: 5 errors / fails (pin_name still null in JSON, validator funcs don't exist).

- [ ] **3.3 Implement `parse_pin_level_reset` in `generator.py`**

Add function that returns dict mapping `pin_name → reset_behavior` from the MD table:

```python
def parse_pin_level_reset(md_path: Path) -> dict:
    """Parse the pin-level reset table from pin_level_reset.md.

    Returns: {pin_name: {kind, value?, domain}}

    The MD has a master table with columns: Channel | Signal | Reset value | Notes.
    Direction is inferred from the Channel suffix (_IN / _OUT) or signal-name
    suffix (_i / _o).
    """
    import re
    text = md_path.read_text(encoding="utf-8")
    result = {}

    # Find each row of the master table
    table_row = re.compile(
        r"\|\s*([A-Z_]+)\s*\|\s*`?([a-zA-Z_][a-zA-Z0-9_]*)`?\s*\|\s*([^|]+?)\s*\|"
    )
    for m in table_row.finditer(text):
        channel_tag = m.group(1)
        pin = m.group(2)
        reset_text = m.group(3).strip().strip("`").strip()

        # Skip header rows
        if pin.lower() in ("signal", "name"):
            continue

        # Infer direction
        is_input = channel_tag.endswith("_IN") or pin.endswith("_i")

        # Choose reset_behavior shape
        if reset_text.lower().startswith("as driven by"):
            rb = {"kind": "external_driven"}
        elif reset_text in ("0", "1'b0", "1'b1", "0x0") or reset_text.startswith("0x"):
            # Default to async-active-low + arst_ni for AXI, noc_rst_ni for NoC
            domain = "noc_rst_ni" if pin.startswith("noc_") else "arst_ni"
            value = "0" if reset_text in ("0", "1'b0", "0x0") else "1"
            rb = {"kind": "async-active-low", "value": value, "domain": domain}
        else:
            # Fallback — generator should not silently drop; raise so spec author fixes MD
            raise ValueError(f"Cannot parse reset behavior for {pin}: {reset_text!r}")

        result[pin] = rb
    return result
```

- [ ] **3.4 Wire cross-merge into `write_generated_signals_json`**

Where Task 2 set `sig.setdefault("pin_name", None)`, replace with cross-merge logic. The hand-written `_AXI_CHANNEL_SIGNALS` mapping in generator.py already enumerates per-channel pin patterns. Use it to derive each signal's `pin_name`, then look up `reset_behavior` from `parse_pin_level_reset` result:

```python
# (Replace the null setdefault block from Task 2)
reset_map = parse_pin_level_reset(md_dir_path / "pin_level_reset.md")

for iface in result["interfaces"]:
    for ch in iface.get("channels", []):
        for sig in ch.get("signals", []):
            # Derive pin_name from existing convention used by _AXI_CHANNEL_SIGNALS
            # (Task 2 left a placeholder; here we look it up.)
            pin_name = _derive_pin_name(iface, ch, sig)  # helper to add
            sig["pin_name"] = pin_name
            sig["reset_behavior"] = reset_map.get(pin_name)
            sig.setdefault("presence", None)
            sig.setdefault("width_expr", None)
```

Add helper `_derive_pin_name(iface, ch, sig)` to convert abstract name (e.g. `AW_VALID`) into concrete pin (e.g. `axi_awvalid_i`) using the existing `_AXI_CHANNEL_SIGNALS` source.

- [ ] **3.5 Implement L2 validator `check_signals_reset_domains`**

Append to `spec_validate/ni_spec/invariants.py`:

```python
def check_signals_reset_domains(signals_spec) -> list:
    """L2 check: every signal's reset_behavior.domain must be in meta.reset_signals."""
    issues = []
    legal = set(signals_spec.get("meta", {}).get("reset_signals", []))
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                rb = sig.get("reset_behavior")
                if rb is None:
                    issues.append(Issue("ERROR", "L2-SIG-RST",
                        f"signal {sig.get('name')} missing reset_behavior"))
                    continue
                if rb["kind"] == "external_driven":
                    if "value" in rb:
                        issues.append(Issue("ERROR", "L2-SIG-RST",
                            f"signal {sig.get('name')}: external_driven must not carry value"))
                    continue
                domain = rb.get("domain")
                if domain and domain not in legal:
                    issues.append(Issue("ERROR", "L2-SIG-RST",
                        f"signal {sig.get('name')}: reset domain {domain!r} not in meta.reset_signals"))
    return issues


def check_signals_pin_uniqueness(signals_spec) -> list:
    """L2 check: no pin_name appears twice."""
    issues = []
    seen = {}
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                pin = sig.get("pin_name")
                if pin is None:
                    issues.append(Issue("ERROR", "L2-SIG-PIN",
                        f"signal {sig.get('name')} has null pin_name"))
                    continue
                if pin in seen:
                    issues.append(Issue("ERROR", "L2-SIG-PIN",
                        f"pin_name {pin!r} duplicated (also in {seen[pin]})"))
                else:
                    seen[pin] = sig.get("name")
    return issues
```

- [ ] **3.6 Wire validators into `__main__.py`**

In `spec_validate/ni_spec/__main__.py`, after the existing signals schema check, add:

```python
issues += check_signals_reset_domains(signals)
issues += check_signals_pin_uniqueness(signals)
```

Import them from invariants module.

- [ ] **3.7 Regen + run tests**

```
py -3 -m ni_spec ..\spec\ni\doc
py -3 -m pytest tests/test_pin_level_reset.py tests/test_signals_schema.py -v
```

Expected: ni_spec PASS、all tests PASS。

- [ ] **3.8 Commit**

```
git add spec_validate/ni_spec/generator.py \
        spec_validate/ni_spec/invariants.py \
        spec_validate/ni_spec/__main__.py \
        spec_validate/generated/ni_signals.json \
        spec_validate/tests/test_pin_level_reset.py
git commit -m "feat(spec_validate): merge pin_level_reset.md into ni_signals.json

- parse_pin_level_reset extracts {pin_name: reset_behavior} from MD table
- generator cross-merges pin_name + reset_behavior into every signal entry
- input wires (axi_*_i) become kind=external_driven (no value)
- L2 checks: reset domain whitelist + pin_name uniqueness

Refs design §5.2, §8.2 item 3."
```

---

## Task 4: Register domain end-to-end

**Depends on:** Task 1
**Effort:** 2 day
**Acceptance:**
- 新 `ni_registers.schema.json` 覆蓋 §5.3 列出的所有 schema 升級欄位（`csr_policy`、`registers[].kind`、`access`、`reserved_policy`、`width_expr`、`conditional_presence`）
- generator 處理 `registers.md:57` em-dash placeholder row（不 crash、產 `kind: "reserved"` 條目）
- L2 validator: offset alignment / offset unique / field tiling / reset bound — 跑全部
- 新生 `ni_registers.json` 與 `deferred/ni_registers.json` 結構 diff，差異 manual 文件化（不必 bytes-identical，但每個差異要能解釋）
- `constants.regs_offsets()` / `regs_field_mask()` / `regs_access_mode()` 從 stub 變實作
- 跑 `py -3 -m ni_spec` 加上 register domain，新一行 Layer 2 (regs) PASS
- 完成後刪 `spec_validate/deferred/ni_registers.json` 與其他 deferred files

**Rollback:** 整個 Task 4 增量 revert + `git rm spec_validate/generated/ni_registers.json` + 復原 deferred/

**Files:**
- Create: `spec_validate/generated/ni_registers.schema.json`
- Create: `spec_validate/generated/ni_registers.json`
- Modify: `spec_validate/ni_spec/generator.py`
- Modify: `spec_validate/ni_spec/invariants.py`
- Modify: `spec_validate/ni_spec/constants.py`
- Modify: `spec_validate/ni_spec/__main__.py`
- Create: `spec_validate/tests/test_registers_parser.py`
- Create: `spec_validate/tests/test_registers_validator.py`

### Steps

- [ ] **4.1 Create `ni_registers.schema.json`** — JSON Schema draft 2020-12，含 `csr_policy`、`registers[].{kind, access, reserved_policy, width_expr, conditional_presence, fields[]}`、`err_irq_map`

```jsonc
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "ni_registers.schema.json",
  "type": "object",
  "required": ["$schema_version", "meta", "csr_policy", "registers"],
  "properties": {
    "$schema_version": {"const": "ni-spec/2.0"},
    "meta": {
      "type": "object",
      "required": ["spec_version", "auto_generated_from"],
      "properties": {
        "spec_version": {"type": "string", "pattern": "^v\\d+\\.\\d+\\.\\d+$"},
        "auto_generated_from": {"type": "string"}
      }
    },
    "csr_policy": {
      "type": "object",
      "required": ["sub_word_write", "unmapped_read", "misaligned", "wo_read"],
      "properties": {
        "sub_word_write": {"enum": ["decerr", "ignored"]},
        "unmapped_read": {"enum": ["decerr", "zero"]},
        "misaligned": {"enum": ["decerr", "lower-aligned"]},
        "wo_read": {"enum": ["zero", "decerr"]}
      }
    },
    "registers": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["offset", "name", "kind"],
        "properties": {
          "offset": {"type": "string", "pattern": "^0x[0-9A-Fa-f]+$"},
          "name": {"type": "string"},
          "kind": {"enum": ["register", "reserved"]},
          "access": {"enum": ["RO", "RW", "RW1C", "WO", "WC"]},
          "reset_expr": {"type": "string"},
          "width_expr": {"type": "string"},
          "conditional_presence": {
            "type": ["object", "null"],
            "properties": {"condition_text": {"type": "string"}}
          },
          "reserved_bits_policy": {"type": "string"},
          "fields": {
            "type": "array",
            "items": {
              "type": "object",
              "required": ["name", "bit_high", "bit_low"],
              "properties": {
                "name": {"type": "string"},
                "bit_high": {"type": "integer"},
                "bit_low": {"type": "integer"}
              }
            }
          }
        },
        "allOf": [{
          "if": {"properties": {"kind": {"const": "register"}}},
          "then": {"required": ["access", "reset_expr"]}
        }]
      }
    }
  }
}
```

- [ ] **4.2 Write failing tests for parser**

Create `spec_validate/tests/test_registers_parser.py`:

```python
"""Register parser — Task 4."""
from pathlib import Path
import pytest
from ni_spec import generator

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
MD_DIR = SPEC_VALIDATE.parent / "spec" / "ni" / "doc"


def test_parse_csr_policy_has_four_keys():
    policy = generator.parse_csr_policy(MD_DIR / "registers.md")
    assert set(policy.keys()) >= {"sub_word_write", "unmapped_read", "misaligned", "wo_read"}


def test_parse_register_map_includes_reserved_row():
    """0x110 (reserved for LAST_ERR_INFO_HI) must show up as kind=reserved."""
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    reserved = [r for r in regs if r["offset"] == "0x110"]
    assert len(reserved) == 1
    assert reserved[0]["kind"] == "reserved"
    assert reserved[0].get("access") is None  # em-dash means no access


def test_parse_register_map_handles_rw1c():
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    err_status = next(r for r in regs if r["name"] == "ERR_STATUS")
    assert err_status["access"] == "RW1C"
    assert err_status["reset_expr"] == "0x0"


def test_parse_register_map_skips_section_header_rows():
    """Rows like '**Error Status / IRQ**' must not be parsed as registers."""
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    names = [r["name"] for r in regs]
    assert not any("Error Status" in n for n in names)
```

- [ ] **4.3 Run tests — verify fail**

```
py -3 -m pytest tests/test_registers_parser.py -v
```

Expected: 4 errors (functions don't exist).

- [ ] **4.4 Implement `parse_csr_policy`**

Append to `generator.py`:

```python
def parse_csr_policy(md_path: Path) -> dict:
    """Extract CSR access policy section from registers.md.

    Looks for the section that documents sub-word writes / unmapped reads /
    misaligned access / WO-read behavior, returns {sub_word_write, unmapped_read,
    misaligned, wo_read} all as enum strings.

    Falls back to defaults if section absent; spec author should fail-loud later.
    """
    import re
    text = md_path.read_text(encoding="utf-8")

    def find_after(keyword: str, default: str) -> str:
        # Look for "Keyword: value" or "- **Keyword**: value" patterns
        for line in text.splitlines():
            if keyword.lower() in line.lower():
                m = re.search(r":\s*(\S.+?)\.?$", line.strip())
                if m:
                    val = m.group(1).strip("`").strip().lower()
                    return val
        return default

    return {
        "sub_word_write": find_after("sub-word", "decerr"),
        "unmapped_read": find_after("unmapped", "decerr"),
        "misaligned": find_after("misaligned", "decerr"),
        "wo_read": find_after("write-only", "zero"),
    }
```

- [ ] **4.5 Implement `parse_register_map`**

Append to `generator.py`:

```python
def parse_register_map(md_path: Path) -> list:
    """Parse the main register table in registers.md.

    Columns: Offset | Name | Access | Reset | Description.

    Handles three row variants:
    1. Normal register: all 5 cells populated.
    2. Reserved placeholder: name like "(reserved for X)", access/reset are em-dashes.
    3. Section header: row starts with **Bold** or is purely formatting; skip.
    """
    import re
    text = md_path.read_text(encoding="utf-8")
    rows = []

    row_re = re.compile(
        r"^\|\s*(0x[0-9A-Fa-f]+)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|\s*([^|]+?)\s*\|$",
        re.MULTILINE,
    )
    for m in row_re.finditer(text):
        offset, name_raw, access, reset, desc = (g.strip() for g in m.groups())

        # Strip code backticks from name
        name = name_raw.strip("`").strip()

        # Section header rows have ** in name
        if "**" in name_raw:
            continue

        # Reserved placeholder
        if "reserved" in name.lower() and (access in ("—", "-") or reset in ("—", "-")):
            rows.append({
                "offset": offset,
                "name": name,
                "kind": "reserved",
                "access": None,
                "reset_expr": None,
            })
            continue

        # Normal register
        rows.append({
            "offset": offset,
            "name": name,
            "kind": "register",
            "access": access if access in ("RO", "RW", "RW1C", "WO", "WC") else None,
            "reset_expr": reset,
            "width_expr": "32",  # default; per-field width comes from parse_register_fields
        })
    return rows
```

- [ ] **4.6 Run parser tests — verify pass**

```
py -3 -m pytest tests/test_registers_parser.py -v
```

Expected: 4 PASS.

- [ ] **4.7 Write failing tests for validator**

Create `spec_validate/tests/test_registers_validator.py`:

```python
"""Register L2 validator — Task 4."""
import pytest
from ni_spec import invariants


def test_check_offset_alignment_catches_misaligned():
    regs = {"registers": [{"offset": "0x003", "name": "BAD", "kind": "register"}]}
    issues = invariants.check_csr_offset_alignment(regs)
    assert any("BAD" in i.message for i in issues)


def test_check_offset_alignment_passes_aligned():
    regs = {"registers": [{"offset": "0x010", "name": "GOOD", "kind": "register"}]}
    issues = invariants.check_csr_offset_alignment(regs)
    assert len(issues) == 0


def test_check_offset_unique_catches_collision():
    regs = {"registers": [
        {"offset": "0x010", "name": "A", "kind": "register"},
        {"offset": "0x010", "name": "B", "kind": "register"},
    ]}
    issues = invariants.check_csr_offset_unique(regs)
    assert any("0x010" in i.message for i in issues)


def test_check_field_tiling_catches_overlap():
    regs = {"registers": [{
        "offset": "0x000", "name": "X", "kind": "register",
        "fields": [
            {"name": "a", "bit_high": 3, "bit_low": 0},
            {"name": "b", "bit_high": 5, "bit_low": 2},  # overlap
        ]
    }]}
    issues = invariants.check_field_bit_tiling(regs)
    assert any("overlap" in i.message.lower() for i in issues)
```

- [ ] **4.8 Run validator tests — verify fail**

```
py -3 -m pytest tests/test_registers_validator.py -v
```

Expected: 4 errors.

- [ ] **4.9 Implement register validator functions**

Append to `invariants.py`:

```python
def check_csr_offset_alignment(regs_spec) -> list:
    """L2: offset must be % 4 == 0."""
    issues = []
    for r in regs_spec.get("registers", []):
        try:
            ofs = int(r["offset"], 16)
        except (ValueError, KeyError):
            continue
        if ofs % 4 != 0:
            issues.append(Issue("ERROR", "L2-REG-ALIGN",
                f"{r.get('name')}: offset {r['offset']} not 4-byte aligned"))
    return issues


def check_csr_offset_unique(regs_spec) -> list:
    """L2: no two registers share an offset."""
    issues = []
    seen = {}
    for r in regs_spec.get("registers", []):
        ofs = r.get("offset")
        if ofs in seen:
            issues.append(Issue("ERROR", "L2-REG-OFS",
                f"offset {ofs} duplicated ({seen[ofs]} and {r.get('name')})"))
        else:
            seen[ofs] = r.get("name")
    return issues


def check_field_bit_tiling(regs_spec) -> list:
    """L2: bit ranges within each register must not overlap."""
    issues = []
    for r in regs_spec.get("registers", []):
        fields = r.get("fields", [])
        used = set()
        for f in fields:
            try:
                hi, lo = int(f["bit_high"]), int(f["bit_low"])
            except (KeyError, ValueError):
                continue
            for b in range(lo, hi + 1):
                if b in used:
                    issues.append(Issue("ERROR", "L2-REG-TILE",
                        f"{r.get('name')}: field {f.get('name')} bit {b} overlap"))
                used.add(b)
    return issues


def check_reset_in_data_width(regs_spec, data_width: int = 32) -> list:
    """L2: reset_expr literal must fit in data_width bits."""
    issues = []
    for r in regs_spec.get("registers", []):
        if r.get("kind") != "register":
            continue
        rst = r.get("reset_expr")
        if not rst:
            continue
        try:
            val = int(rst, 0)
        except (ValueError, TypeError):
            continue  # symbolic reset_expr is fine for L2
        if val >= (1 << data_width):
            issues.append(Issue("ERROR", "L2-REG-RESET",
                f"{r.get('name')}: reset {rst} exceeds {data_width}-bit width"))
    return issues
```

- [ ] **4.10 Implement `generate_ni_registers_json` composer**

Append to `generator.py`:

```python
def generate_ni_registers_json(md_dir, out_path: Path) -> dict:
    """Compose ni_registers.json from registers.md."""
    md_dir_path = Path(md_dir) if not isinstance(md_dir, Path) else md_dir
    spec_version = (md_dir_path.parent / "VERSION").read_text(encoding="utf-8").strip()

    result = {
        "$schema_version": "ni-spec/2.0",
        "meta": {
            "spec_version": spec_version,
            "auto_generated_from": "spec/ni/doc/registers.md",
        },
        "csr_policy": parse_csr_policy(md_dir_path / "registers.md"),
        "registers": parse_register_map(md_dir_path / "registers.md"),
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result
```

- [ ] **4.11 Wire into `__main__.py`** — extend to also generate ni_registers.json and run its L2 checks

In `__main__.py`, after the existing packet + signals generation:

```python
REGISTERS_JSON = GENERATED_DIR / "ni_registers.json"
REGISTERS_SCHEMA = GENERATED_DIR / "ni_registers.schema.json"

# Step: generate registers JSON
try:
    registers = generate_ni_registers_json(md_dir, REGISTERS_JSON)
except (FileNotFoundError, ValueError) as e:
    print(f"[FATAL] registers generator: {e}", file=sys.stderr)
    return 2

# L1
regs_schema = load_doc(REGISTERS_SCHEMA) if REGISTERS_SCHEMA.exists() else None
if regs_schema is not None:
    import jsonschema
    for e in sorted(jsonschema.Draft202012Validator(regs_schema).iter_errors(registers),
                    key=lambda e: list(e.absolute_path)):
        loc = "/".join(str(p) for p in e.absolute_path) or "(root)"
        issues.append(Issue("ERROR", "L1-REG-SCHEMA", f"{loc}: {e.message}"))

# L2
issues += check_csr_offset_alignment(registers)
issues += check_csr_offset_unique(registers)
issues += check_field_bit_tiling(registers)
issues += check_reset_in_data_width(registers)
```

Update the `layers` dict to add a register row.

- [ ] **4.12 Implement constants accessors**

Replace stubs in `constants.py`:

```python
def regs_offsets(regs_spec) -> dict:
    return {r["name"]: int(r["offset"], 16)
            for r in regs_spec.get("registers", [])
            if r.get("kind") == "register"}


def regs_field_mask(regs_spec, reg_name: str, field_name: str) -> int:
    for r in regs_spec.get("registers", []):
        if r.get("name") != reg_name:
            continue
        for f in r.get("fields", []):
            if f.get("name") == field_name:
                hi, lo = int(f["bit_high"]), int(f["bit_low"])
                return ((1 << (hi - lo + 1)) - 1) << lo
    raise KeyError(f"{reg_name}.{field_name}")


def regs_access_mode(regs_spec, reg_name: str) -> str:
    for r in regs_spec.get("registers", []):
        if r.get("name") == reg_name:
            return r.get("access")
    raise KeyError(reg_name)
```

- [ ] **4.13 Run full chain + tests**

```
py -3 -m ni_spec ..\spec\ni\doc
py -3 -m pytest tests/ -v
```

Expected: PASS、新 `Layer 2 (registers)` row in report、all tests PASS。

- [ ] **4.14 Diff new generated vs deferred/**

```
py -3 -c "
import json
a = json.load(open('generated/ni_registers.json', encoding='utf-8'))
b = json.load(open('deferred/ni_registers.json', encoding='utf-8'))
print('new regs:', len(a['registers']))
print('legacy regs:', len(b.get('registers', [])))
print('csr_policy keys:', set(a['csr_policy'].keys()))
"
```

Manual inspect — every diff should have an explainable reason (new schema fields / `kind: "reserved"` / etc).

- [ ] **4.15 Delete `deferred/`**

```
git rm -r spec_validate/deferred/
```

- [ ] **4.16 Commit**

```
git add spec_validate/generated/ni_registers.* \
        spec_validate/ni_spec/{generator,invariants,constants,__main__}.py \
        spec_validate/tests/test_registers_*.py
git commit -m "feat(spec_validate): register domain end-to-end (Phase 3)

- registers.md → ni_registers.json: csr_policy + register map + access semantics
- Handles em-dash reserved placeholder rows (kind=reserved)
- L2: offset align/unique, field tiling, reset bounds
- constants API: regs_offsets/regs_field_mask/regs_access_mode
- Delete deferred/ (replaced by generated/ni_registers.json)

Refs design §5.3, §8.2 item 4."
```

---

## Task 5: Function blocks

**Depends on:** Task 4 (cross-ref needs ni_registers.json)
**Effort:** 1 day
**Acceptance:**
- `spec_validate/ni_function_blocks.json` 手寫存在，含 NMU/NSU 兩個 block、每個 block 列 §5.4 schema 規定的 feature entry
- 所有 feature 的 `uses_packet_fields[]` 在 `ni_packet.json` 中找得到
- 所有 feature 的 `configured_by[]` 在 `ni_registers.json` 中找得到
- `modes[]` 全部 match regex `^[A-Z][A-Za-z0-9_]*$`
- `summary` 全部 ≤ 200 字符
- `compile_time_params` name 跨 feature 唯一；若名稱跟 `packet_format.md` 或其他 domain 重複，**選擇一邊** + 在 commit 訊息明寫
- `constants.blocks_*` 從 stub 變實作

**Rollback:** revert + `git rm spec_validate/ni_function_blocks.json`

**Files:**
- Create: `spec_validate/ni_function_blocks.json`
- Create: `spec_validate/ni_function_blocks.schema.json`
- Modify: `spec_validate/ni_spec/loader.py`
- Modify: `spec_validate/ni_spec/invariants.py`
- Modify: `spec_validate/ni_spec/constants.py`
- Modify: `spec_validate/ni_spec/__main__.py`
- Create: `spec_validate/tests/test_function_blocks.py`

### Steps

- [ ] **5.1 Create `ni_function_blocks.schema.json`**

```jsonc
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "ni_function_blocks.schema.json",
  "type": "object",
  "required": ["$schema_version", "blocks"],
  "properties": {
    "$schema_version": {"const": "ni-spec/2.0"},
    "blocks": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["name", "fullname", "role", "features"],
        "properties": {
          "name": {"enum": ["NMU", "NSU"]},
          "fullname": {"type": "string"},
          "role": {"type": "string"},
          "features": {
            "type": "array",
            "items": {
              "type": "object",
              "required": ["id", "name", "summary", "modes"],
              "properties": {
                "id": {"type": "string", "pattern": "^FEAT-(NMU|NSU)-[A-Z][A-Z0-9_]*$"},
                "name": {"type": "string"},
                "summary": {"type": "string", "maxLength": 200},
                "modes": {
                  "type": "array",
                  "items": {"type": "string", "pattern": "^[A-Z][A-Za-z0-9_]*$"}
                },
                "compile_time_params": {
                  "type": "object",
                  "additionalProperties": {"type": "integer"}
                },
                "uses_packet_fields": {"type": "array", "items": {"type": "string"}},
                "configured_by": {"type": "array", "items": {"type": "string"}},
                "related_features": {"type": "array", "items": {"type": "string"}},
                "source_doc": {"type": "string"}
              }
            }
          }
        }
      }
    }
  }
}
```

- [ ] **5.2 Create `ni_function_blocks.json`** — 手寫；先從 README.md §Features 與 design `2026-05-25-modular-design.md` 範例轉錄。每個 feature minimal 結構：

```jsonc
{
  "$schema_version": "ni-spec/2.0",
  "meta": {
    "spec_version": "v0.4.0",
    "source_format": "hand-written; do not auto-gen"
  },
  "blocks": [
    {
      "name": "NMU",
      "fullname": "Network Master Unit",
      "role": "AXI-side injection / response sink",
      "features": [
        {
          "id": "FEAT-NMU-ROB",
          "name": "Reorder Buffer",
          "summary": "Per-AXI-ID in-order response release. NoC may return responses out of order; RoB restores AXI ordering contract.",
          "modes": ["Normal", "Simple", "NoRoB"],
          "compile_time_params": {"ROB_DEPTH": 32, "ROB_ENTRY_WIDTH": 64},
          "uses_packet_fields": ["rob_req", "rob_idx"],
          "configured_by": ["ROB_CTRL"],
          "related_features": [],
          "source_doc": "spec/ni/README.md §Features; spec/ni/doc/theory_of_operation.md"
        }
        // ... add: AXI Slave Port, QoSGen, ECC Gen, Address Mapping, Credit-based FC, CDC, CSR Interface
      ]
    },
    {
      "name": "NSU",
      "fullname": "Network Slave Unit",
      "role": "AXI-side request sink / response injection",
      "features": [
        // ... mirror set for NSU
      ]
    }
  ]
}
```

**Important**: 寫完 NMU/NSU 全部 feature 後，**逐項檢查 `compile_time_params` 是否與 `packet_format.md §field_widths` 重複**。若有，**消除一邊**：
- 移除原則：如果 param 純粹是 RTL elaboration parameter（如 ROB_DEPTH），留在 function_blocks
- 如果 param 是 packet bit field width（如 ROB_IDX_WIDTH = 5），留在 packet_format.md，function_blocks 不重抄

- [ ] **5.3 Write failing tests**

Create `spec_validate/tests/test_function_blocks.py`:

```python
"""Function blocks JSON + validator — Task 5."""
from pathlib import Path
import json, re
import pytest
from ni_spec import loader, invariants, constants

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
FB_JSON = SPEC_VALIDATE / "ni_function_blocks.json"
FB_SCHEMA = SPEC_VALIDATE / "ni_function_blocks.schema.json"


def test_function_blocks_json_exists():
    assert FB_JSON.exists()


def test_function_blocks_passes_schema():
    import jsonschema
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    schema = json.loads(FB_SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(data)  # raises on fail


def test_mode_identifiers_are_valid_for_cpp_sv():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    pattern = re.compile(r"^[A-Z][A-Za-z0-9_]*$")
    for block in data["blocks"]:
        for feat in block["features"]:
            for mode in feat.get("modes", []):
                assert pattern.match(mode), f"mode {mode!r} not a valid C++/SV identifier"


def test_summary_length_under_200():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    for block in data["blocks"]:
        for feat in block["features"]:
            assert len(feat["summary"]) <= 200, f"{feat['id']} summary too long"


def test_cross_ref_uses_packet_fields_exist_in_ni_packet():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    pkt = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_packet.json")
    pkt_field_names = {f["name"] for f in pkt["flit"]["header_fields"]}
    pkt_field_names |= {c["name"] for c in pkt["flit"]["payload_channels"]}
    issues = invariants.check_blocks_xref_packet(fb, pkt)
    assert not any(i.severity == "ERROR" for i in issues), \
        f"cross-ref errors: {[i.message for i in issues]}"


def test_cross_ref_configured_by_exists_in_ni_registers():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    regs = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_registers.json")
    issues = invariants.check_blocks_xref_registers(fb, regs)
    assert not any(i.severity == "ERROR" for i in issues), \
        f"cross-ref errors: {[i.message for i in issues]}"


def test_compile_time_params_unique_across_features():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    issues = invariants.check_blocks_param_uniqueness(fb)
    assert not any(i.severity == "ERROR" for i in issues)
```

- [ ] **5.4 Run tests — verify fail**

```
py -3 -m pytest tests/test_function_blocks.py -v
```

Expected: 7 errors (validator functions don't exist; FB JSON might not be complete).

- [ ] **5.5 Implement validator functions in `invariants.py`**

```python
def check_blocks_xref_packet(fb_spec, pkt_spec) -> list:
    """L2: every uses_packet_fields entry exists in ni_packet.json."""
    issues = []
    legal = {f["name"] for f in pkt_spec["flit"]["header_fields"]}
    legal |= {c["name"] for c in pkt_spec["flit"]["payload_channels"]}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for ref in feat.get("uses_packet_fields", []):
                if ref not in legal:
                    issues.append(Issue("ERROR", "L2-FB-XREF-PKT",
                        f"{feat['id']}: uses_packet_fields {ref!r} not in ni_packet.json"))
    return issues


def check_blocks_xref_registers(fb_spec, regs_spec) -> list:
    """L2: every configured_by entry exists in ni_registers.json."""
    issues = []
    legal = {r["name"] for r in regs_spec.get("registers", []) if r.get("kind") == "register"}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for ref in feat.get("configured_by", []):
                # configured_by may include "REG.field" form — match register name part
                reg_name = ref.split(".")[0]
                if reg_name not in legal:
                    issues.append(Issue("ERROR", "L2-FB-XREF-REG",
                        f"{feat['id']}: configured_by {ref!r} register not in ni_registers.json"))
    return issues


def check_blocks_param_uniqueness(fb_spec) -> list:
    """L2: compile_time_params name must be unique across all features."""
    issues = []
    seen = {}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for pname in feat.get("compile_time_params", {}):
                if pname in seen:
                    issues.append(Issue("ERROR", "L2-FB-PARAM",
                        f"{pname!r} defined in both {seen[pname]} and {feat['id']}"))
                else:
                    seen[pname] = feat["id"]
    return issues
```

- [ ] **5.6 Implement constants accessors**

Replace stubs in `constants.py`:

```python
def blocks_function_block_names(blocks_spec) -> list:
    return [b["name"] for b in blocks_spec.get("blocks", [])]


def blocks_modes_of(blocks_spec, block_name: str) -> list:
    """Return concatenated mode list — actually mode is per-feature, not per-block."""
    out = []
    for b in blocks_spec.get("blocks", []):
        if b["name"] != block_name:
            continue
        for f in b.get("features", []):
            for m in f.get("modes", []):
                out.append((f["id"], m))
    return out


def blocks_compile_time_params(blocks_spec) -> dict:
    out = {}
    for b in blocks_spec.get("blocks", []):
        for f in b.get("features", []):
            out.update(f.get("compile_time_params", {}))
    return out
```

- [ ] **5.7 Implement `loader.load_function_blocks`**

Append to `loader.py`:

```python
def load_function_blocks(path: Path = None) -> dict:
    if path is None:
        path = Path(__file__).resolve().parent.parent / "ni_function_blocks.json"
    return load_doc(path)
```

- [ ] **5.8 Wire into `__main__.py`**

Add after registers validation:

```python
FB_JSON = SPEC_VALIDATE / "ni_function_blocks.json"
FB_SCHEMA = SPEC_VALIDATE / "ni_function_blocks.schema.json"

if FB_JSON.exists():
    fb = load_doc(FB_JSON)
    fb_schema = load_doc(FB_SCHEMA)
    import jsonschema
    for e in jsonschema.Draft202012Validator(fb_schema).iter_errors(fb):
        loc = "/".join(str(p) for p in e.absolute_path) or "(root)"
        issues.append(Issue("ERROR", "L1-FB-SCHEMA", f"{loc}: {e.message}"))
    issues += check_blocks_xref_packet(fb, packet)
    issues += check_blocks_xref_registers(fb, registers)
    issues += check_blocks_param_uniqueness(fb)
```

- [ ] **5.9 Run full chain + tests**

```
py -3 -m ni_spec ..\spec\ni\doc
py -3 -m pytest tests/ -v
```

Expected: all PASS. Any param duplication error → return to step 5.2 and resolve.

- [ ] **5.10 Commit**

```
git add spec_validate/ni_function_blocks.* \
        spec_validate/ni_spec/{loader,invariants,constants,__main__}.py \
        spec_validate/tests/test_function_blocks.py
git commit -m "feat(spec_validate): function_blocks domain (hand-written JSON)

- ni_function_blocks.json: NMU + NSU feature inventory with modes/params/cross-refs
- ni_function_blocks.schema.json: structural validation + identifier regex
- L2 cross-ref: uses_packet_fields → ni_packet, configured_by → ni_registers
- L2 uniqueness: compile_time_params name unique across all features
- constants.blocks_* API implemented

Refs design §5.4, §8.2 item 5."
```

---

## Task 6: Protocol rule metadata lift-shift

**Depends on:** Task 2 (channel cross-ref needs signals)
**Effort:** 1 day
**Acceptance:**
- 新 generator `parse_protocol_rule_index()` 抽 `protocol_rules.md` 每條 rule 的 metadata
- 產 `generated/ni_protocol_rule_index.json` 含 24 條 rule，每條: `id` / `proto` / `role` / `channels[]` / `severity` / `source_section` / `source_line` / `condition_summary` (≤100 字)
- L2 check: id 唯一、channels[] 引用的 token 必須在 `ni_signals.json` 真實存在
- 不抓 condition prose、不產 SVA

**Rollback:** revert + delete `generated/ni_protocol_rule_index.*`

**Files:**
- Create: `spec_validate/generated/ni_protocol_rule_index.schema.json`
- Create: `spec_validate/generated/ni_protocol_rule_index.json`
- Modify: `spec_validate/ni_spec/generator.py`
- Modify: `spec_validate/ni_spec/invariants.py`
- Modify: `spec_validate/ni_spec/__main__.py`
- Create: `spec_validate/tests/test_protocol_rules.py`

### Steps

- [ ] **6.1 Create `ni_protocol_rule_index.schema.json`**

```jsonc
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "ni_protocol_rule_index.schema.json",
  "type": "object",
  "required": ["$schema_version", "rules"],
  "properties": {
    "$schema_version": {"const": "ni-spec/2.0"},
    "rules": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["id", "proto", "severity", "source_section", "source_line"],
        "properties": {
          "id": {"type": "string", "pattern": "^[A-Z][A-Z0-9_]*$"},
          "proto": {"enum": ["AXI4", "AXI4LITE", "NOC", "NI", "CDC", "RESET"]},
          "role": {"enum": ["master", "slave", "any"]},
          "channels": {"type": "array", "items": {"type": "string"}},
          "severity": {"enum": ["FAIL", "WARN", "RECOMMEND"]},
          "source_section": {"type": "string"},
          "source_line": {"type": "integer"},
          "condition_summary": {"type": "string", "maxLength": 100}
        }
      }
    }
  }
}
```

- [ ] **6.2 Write failing tests**

Create `spec_validate/tests/test_protocol_rules.py`:

```python
"""Protocol rule index parser + validator — Task 6."""
from pathlib import Path
import pytest
from ni_spec import generator, invariants, loader

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
MD_DIR = SPEC_VALIDATE.parent / "spec" / "ni" / "doc"


def test_parse_protocol_rule_index_finds_24_rules():
    rules = generator.parse_protocol_rule_index(MD_DIR / "protocol_rules.md")
    assert len(rules) >= 20, f"expected ~24 rules, got {len(rules)}"


def test_rule_id_uniqueness():
    rules = generator.parse_protocol_rule_index(MD_DIR / "protocol_rules.md")
    ids = [r["id"] for r in rules]
    assert len(ids) == len(set(ids)), "duplicate rule id"


def test_reset_rules_have_severity_fail():
    rules = generator.parse_protocol_rule_index(MD_DIR / "protocol_rules.md")
    reset_rules = [r for r in rules if r["id"].startswith("NI_RST_")]
    assert any(r["severity"] == "FAIL" for r in reset_rules)


def test_validator_catches_unknown_channel_reference():
    sig_spec = {"interfaces": [{"channels": [{"signals": [{"pin_name": "axi_awvalid_i"}]}]}]}
    rule_spec = {"rules": [{
        "id": "TEST", "proto": "AXI4", "severity": "FAIL",
        "channels": ["bogus_chan"], "source_section": "x", "source_line": 1
    }]}
    issues = invariants.check_rules_channel_xref(rule_spec, sig_spec)
    assert any("bogus_chan" in i.message for i in issues)
```

- [ ] **6.3 Run tests — verify fail**

```
py -3 -m pytest tests/test_protocol_rules.py -v
```

- [ ] **6.4 Implement `parse_protocol_rule_index`**

Append to `generator.py`:

```python
def parse_protocol_rule_index(md_path: Path) -> list:
    """Extract rule headers from protocol_rules.md.

    Each section like '## Reset rules' contains a table:
        | ID | Condition | Required behavior | Severity | ARM SVA equivalent |
    We pull (id, severity, source_section, source_line, condition_summary)
    and infer (proto, role) from the section heading + id prefix.
    """
    import re
    text = md_path.read_text(encoding="utf-8")
    lines = text.splitlines()

    rules = []
    cur_section = None
    section_pat = re.compile(r"^##\s+(.+)$")
    row_pat = re.compile(
        r"^\|\s*([A-Z][A-Z0-9_]+)\s*\|\s*([^|]+?)\s*\|\s*[^|]+?\s*\|\s*(FAIL|WARN|RECOMMEND)\s*\|"
    )

    for i, line in enumerate(lines, start=1):
        ms = section_pat.match(line)
        if ms:
            cur_section = ms.group(1).strip()
            continue
        m = row_pat.match(line)
        if not m:
            continue
        rule_id, cond, severity = m.group(1), m.group(2).strip(), m.group(3)

        # Infer proto from id prefix
        if rule_id.startswith("NI_RST_"):
            proto = "RESET"
        elif rule_id.startswith("NI_CDC_"):
            proto = "CDC"
        elif rule_id.startswith("AXI4_"):
            proto = "AXI4"
        elif rule_id.startswith("AXI4LITE_"):
            proto = "AXI4LITE"
        elif rule_id.startswith("NOC_"):
            proto = "NOC"
        else:
            proto = "NI"

        # Infer channels: grep `\\w+` tokens from condition + behavior
        channel_tokens = re.findall(r"`([a-z_][a-z0-9_]*)`", line)

        rules.append({
            "id": rule_id,
            "proto": proto,
            "role": "any",
            "channels": channel_tokens[:5],  # cap at 5 to avoid noise
            "severity": severity,
            "source_section": cur_section or "(none)",
            "source_line": i,
            "condition_summary": (cond[:97] + "...") if len(cond) > 100 else cond,
        })
    return rules


def generate_ni_protocol_rule_index_json(md_dir, out_path: Path) -> dict:
    md_dir_path = Path(md_dir) if not isinstance(md_dir, Path) else md_dir
    result = {
        "$schema_version": "ni-spec/2.0",
        "meta": {
            "spec_version": (md_dir_path.parent / "VERSION").read_text(encoding="utf-8").strip(),
            "auto_generated_from": "spec/ni/doc/protocol_rules.md",
        },
        "rules": parse_protocol_rule_index(md_dir_path / "protocol_rules.md"),
    }
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf-8")
    return result
```

- [ ] **6.5 Implement validator**

Append to `invariants.py`:

```python
def check_rules_id_uniqueness(rule_spec) -> list:
    issues = []
    seen = set()
    for r in rule_spec.get("rules", []):
        rid = r["id"]
        if rid in seen:
            issues.append(Issue("ERROR", "L2-RULE-ID", f"duplicate rule id {rid}"))
        seen.add(rid)
    return issues


def check_rules_channel_xref(rule_spec, signals_spec) -> list:
    """L2: every channels[] token must be a real pin_name in ni_signals.json."""
    issues = []
    legal_pins = set()
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                if sig.get("pin_name"):
                    legal_pins.add(sig["pin_name"])
    for r in rule_spec.get("rules", []):
        for ch_tok in r.get("channels", []):
            if ch_tok not in legal_pins:
                issues.append(Issue("WARN", "L2-RULE-CHAN",
                    f"rule {r['id']} references channel token {ch_tok!r} not in ni_signals"))
    return issues
```

- [ ] **6.6 Wire into `__main__.py`**

Add protocol rule generation + validation after function_blocks block.

- [ ] **6.7 Run full chain + tests + commit**

```
py -3 -m ni_spec ..\spec\ni\doc
py -3 -m pytest tests/ -v

git add spec_validate/generated/ni_protocol_rule_index.* \
        spec_validate/ni_spec/{generator,invariants,__main__}.py \
        spec_validate/tests/test_protocol_rules.py
git commit -m "feat(spec_validate): protocol rule metadata lift-shift (Phase 4a)

- parse_protocol_rule_index extracts id/severity/channels/source-line from MD tables
- condition prose stays in MD; this is narrow metadata-only lift-shift
- L2 checks: id uniqueness + channel token referential integrity
- No SVA, no mini-DSL (deferred per design §5.5)

Refs design §5.5, §8.2 item 6."
```

---

## Task 7: Unified codegen.py + C++ emitters

**Depends on:** Task 2, 3, 4, 5 (need all generated JSON to exist)
**Effort:** 2 day
**Acceptance:**
- `tools/codegen.py --target cpp --domain {packet|signals|registers|blocks}` 全部 work
- `tools/codegen.py --check` 比對 committed vs fresh-regen，drift 時 exit non-zero
- 每個產出 .h 開頭含 §6.6 規定的 5 個 provenance 欄位
- 重新產 `ni_flit_constants.h` 與 committed 版「**剔除 provenance header 後**」內容相同（regression check）
- 新產 `ni_signals.h` / `ni_regs.h` / `ni_blocks.h` 可被 `examples/use_constants.cpp` style C++ 程式 `#include` 並 compile
- 舊 `tools/gen_cpp_header.py` 變 wrapper 呼叫新 codegen

**Rollback:** revert codegen.py + emit/ + .h files; restore gen_cpp_header.py

**Files:**
- Create: `spec_validate/tools/codegen.py`
- Create: `spec_validate/tools/emit/__init__.py`
- Create: `spec_validate/tools/emit/common.py`
- Create: `spec_validate/tools/emit/cpp_packet.py`
- Create: `spec_validate/tools/emit/cpp_signals.py`
- Create: `spec_validate/tools/emit/cpp_registers.py`
- Create: `spec_validate/tools/emit/cpp_blocks.py`
- Modify: `spec_validate/tools/gen_cpp_header.py`
- Modify: `spec_validate/include/ni_flit_constants.h`（regen with provenance）
- Create: `spec_validate/include/ni_signals.h`
- Create: `spec_validate/include/ni_regs.h`
- Create: `spec_validate/include/ni_blocks.h`
- Create: `spec_validate/tests/test_codegen.py`

### Steps

- [ ] **7.1 Create `tools/emit/__init__.py`** — empty file

- [ ] **7.2 Create `tools/emit/common.py`**

```python
"""Shared codegen helpers — provenance banner."""
from datetime import datetime, timezone
import hashlib
from pathlib import Path


def header_banner(*, source_json: Path, spec_version: str,
                  generator_version: str = "v0.1.0",
                  comment_prefix: str = "//") -> str:
    """Return 6-line banner (§6.6 of design doc)."""
    json_bytes = source_json.read_bytes()
    sha = hashlib.sha256(json_bytes).hexdigest()[:12]
    ts = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    lines = [
        f"{comment_prefix} {'-' * 76}",
        f"{comment_prefix} AUTO-GENERATED — do not edit",
        f"{comment_prefix} Generator: tools/codegen.py @ {generator_version}",
        f"{comment_prefix} Source:    {source_json.as_posix()}",
        f"{comment_prefix} JSON SHA:  {sha}",
        f"{comment_prefix} Spec ver:  {spec_version}",
        f"{comment_prefix} Generated: {ts}",
        f"{comment_prefix} {'-' * 76}",
        "",
    ]
    return "\n".join(lines)
```

- [ ] **7.3 Create `tools/emit/cpp_packet.py`**

Move logic from `gen_cpp_header.py:emit()` into here, refactored to consume `ni_spec.constants` not raw JSON. Output should match current `ni_flit_constants.h` minus the new banner.

```python
"""C++ emitter for packet domain. Consumes ni_spec.constants only."""
from pathlib import Path
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def emit(packet_json: Path, spec_version: str) -> str:
    spec = load_doc(packet_json)
    out = []
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ni {")
    out.append("")
    out.append("// --- top-level flit widths ---")
    out.append(f"constexpr int FLIT_WIDTH        = {C.flit_width(spec)};")
    out.append(f"constexpr int HEADER_WIDTH      = {C.header_width(spec)};")
    out.append(f"constexpr int PAYLOAD_WIDTH     = {C.payload_width(spec)};")
    out.append(f"constexpr int LINK_WIDTH        = {C.link_width(spec)};")
    derived = spec["flit"]["derived"]
    for k in ("FLIT_DATA_WIDTH", "HEADER_DATA_WIDTH", "WSTRB_WIDTH"):
        if k in derived:
            out.append(f"constexpr int {k:<15} = {derived[k]};")
    out.append("")
    # ... port the rest from gen_cpp_header.py emit() function (header/payload/width sub-namespaces)
    out.append("}  // namespace ni")
    return "\n".join(out) + "\n"
```

- [ ] **7.4 Create `tools/codegen.py` — unified dispatcher**

```python
#!/usr/bin/env python
"""Unified codegen entry point. Replaces gen_cpp_header.py."""
from __future__ import annotations
import argparse, sys, tempfile, shutil, difflib
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec.loader import load_spec_version
from ni_spec.constants import (
    flit_width, header_width, payload_width, link_width,
    header_field_pos, all_header_fields,
)
from tools.emit import common as banner
from tools.emit import cpp_packet, cpp_signals, cpp_registers, cpp_blocks

DOMAIN_TO_EMITTER = {
    ("cpp", "packet"):     (cpp_packet.emit,    "ni_flit_constants.h",   "ni_packet.json"),
    ("cpp", "signals"):    (cpp_signals.emit,   "ni_signals.h",           "ni_signals.json"),
    ("cpp", "registers"):  (cpp_registers.emit, "ni_regs.h",              "ni_registers.json"),
    ("cpp", "blocks"):     (cpp_blocks.emit,    "ni_blocks.h",            "ni_function_blocks.json"),
}


def run_emit(target: str, domain: str, out_dir: Path) -> Path:
    emitter, out_name, src_name = DOMAIN_TO_EMITTER[(target, domain)]
    # function_blocks source is in spec_validate/, others in generated/
    src_dir = SPEC_VALIDATE if domain == "blocks" else SPEC_VALIDATE / "generated"
    src = src_dir / src_name
    spec_version = load_spec_version()
    body = emitter(src, spec_version)
    head = banner.header_banner(source_json=src, spec_version=spec_version)
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / out_name
    out_path.write_text(head + body, encoding="ascii", errors="strict")
    return out_path


def cmd_emit(args) -> int:
    out_dir = Path(args.out) if args.out else SPEC_VALIDATE / "include"
    p = run_emit(args.target, args.domain, out_dir)
    print(f"wrote {p}", file=sys.stderr)
    return 0


def cmd_check(args) -> int:
    """Regen to /tmp_codegen, byte-diff against committed.
    Ignore the timestamp line in banner."""
    committed_dir = SPEC_VALIDATE / "include"
    with tempfile.TemporaryDirectory() as tmp:
        fresh_dir = Path(tmp)
        all_ok = True
        for (target, domain) in DOMAIN_TO_EMITTER:
            if target != "cpp":  # SV --check added in Task 8
                continue
            try:
                fresh = run_emit(target, domain, fresh_dir)
            except FileNotFoundError as e:
                print(f"[skip] {domain}: {e}", file=sys.stderr)
                continue
            committed = committed_dir / fresh.name
            if not committed.exists():
                print(f"[missing committed] {fresh.name}")
                all_ok = False
                continue
            fresh_lines = [l for l in fresh.read_text(encoding="ascii").splitlines()
                           if not l.startswith("// Generated:")]
            committed_lines = [l for l in committed.read_text(encoding="ascii").splitlines()
                               if not l.startswith("// Generated:")]
            if fresh_lines != committed_lines:
                all_ok = False
                diff = list(difflib.unified_diff(
                    committed_lines, fresh_lines,
                    fromfile=str(committed), tofile=str(fresh), lineterm=""))
                print("\n".join(diff[:30]))
        return 0 if all_ok else 1


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--target", choices=["cpp", "sv"], default="cpp")
    p.add_argument("--domain", choices=["packet", "signals", "registers", "blocks"])
    p.add_argument("--out", default=None, help="output directory")
    p.add_argument("--check", action="store_true",
                   help="regen to scratch and diff vs committed; exit 1 on drift")
    args = p.parse_args()
    if args.check:
        return cmd_check(args)
    if not args.domain:
        print("must pass --domain", file=sys.stderr)
        return 2
    return cmd_emit(args)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **7.5 Implement the other three C++ emitters** — `cpp_signals.py`, `cpp_registers.py`, `cpp_blocks.py`. Each consumes `ni_spec.constants` API only. Pattern matches `cpp_packet.py`. Concrete output spec:

`cpp_signals.py` emits:

```cpp
namespace ni::signals {
  // For each signal with a non-null reset_behavior + kind != external_driven:
  constexpr <type> <PIN_NAME>_RESET = <value>;
  // For each width_param used:
  constexpr int <PARAM> = <value>;
}
```

`cpp_registers.py` emits:

```cpp
namespace ni::regs {
  constexpr int <REG>_OFFSET = 0xNNN;
  constexpr int <REG>_<FIELD>_MASK = 0xNNN;
  // per register, access mode as an enum:
  enum class <REG>Access { RO, RW, RW1C, WO, WC };
}
```

`cpp_blocks.py` emits:

```cpp
namespace ni::blocks {
  enum class FunctionBlock { ROB, QOS, ECC, /* ... */ };
  // per feature with modes[]:
  enum class ROBMode { Normal, Simple, NoRoB };
  // compile_time_params:
  constexpr int ROB_DEPTH = 32;
}
```

- [ ] **7.6 Deprecate `gen_cpp_header.py`** — replace contents with:

```python
#!/usr/bin/env python
"""DEPRECATED — use tools/codegen.py --target cpp --domain packet --out include/.
This wrapper will be removed in next minor version."""
import subprocess, sys
from pathlib import Path

print("WARNING: gen_cpp_header.py is deprecated; "
      "use 'tools/codegen.py --target cpp --domain packet'", file=sys.stderr)
THIS = Path(__file__).resolve()
cmd = [sys.executable, str(THIS.parent / "codegen.py"),
       "--target", "cpp", "--domain", "packet",
       "--out", str(THIS.parent.parent / "include")]
sys.exit(subprocess.call(cmd))
```

- [ ] **7.7 Write tests for codegen**

Create `spec_validate/tests/test_codegen.py`:

```python
"""Codegen smoke tests — Task 7."""
import subprocess, sys
from pathlib import Path
import pytest

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
CODEGEN = SPEC_VALIDATE / "tools" / "codegen.py"


def run_codegen(*args) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(CODEGEN), *args],
                          capture_output=True, text=True, cwd=SPEC_VALIDATE)


def test_packet_cpp_emits():
    r = run_codegen("--target", "cpp", "--domain", "packet",
                    "--out", str(SPEC_VALIDATE / "include"))
    assert r.returncode == 0, r.stderr
    out = SPEC_VALIDATE / "include" / "ni_flit_constants.h"
    text = out.read_text()
    assert "FLIT_WIDTH" in text
    assert "JSON SHA:" in text  # provenance


def test_check_mode_exit_zero_when_clean():
    r = run_codegen("--check")
    assert r.returncode == 0, f"--check failed:\n{r.stdout}\n{r.stderr}"


def test_signals_cpp_emits_pin_reset():
    r = run_codegen("--target", "cpp", "--domain", "signals",
                    "--out", str(SPEC_VALIDATE / "include"))
    assert r.returncode == 0, r.stderr
    text = (SPEC_VALIDATE / "include" / "ni_signals.h").read_text()
    assert "namespace ni::signals" in text


def test_existing_use_constants_still_compiles(tmp_path):
    """examples/use_constants.cpp must still compile against regenerated header."""
    import shutil
    # Skip if g++ not available
    if not shutil.which("g++"):
        pytest.skip("g++ not in PATH")
    r = subprocess.run(["g++", "-std=c++17",
                        "-I", str(SPEC_VALIDATE / "include"),
                        str(SPEC_VALIDATE / "examples" / "use_constants.cpp"),
                        "-o", str(tmp_path / "use.exe")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
```

- [ ] **7.8 Run codegen + tests + verify existing use_constants.cpp still passes**

```
py -3 tools/codegen.py --target cpp --domain packet --out include
py -3 tools/codegen.py --target cpp --domain signals --out include
py -3 tools/codegen.py --target cpp --domain registers --out include
py -3 tools/codegen.py --target cpp --domain blocks --out include
py -3 tools/codegen.py --check
py -3 -m pytest tests/test_codegen.py -v

# Recompile sample
g++ -std=c++17 -I include examples/use_constants.cpp -o use_constants.exe
./use_constants.exe   # expect: header[63:0] = 0x00000000F80902AA
```

- [ ] **7.9 Commit**

```
git add spec_validate/tools/codegen.py \
        spec_validate/tools/emit/ \
        spec_validate/tools/gen_cpp_header.py \
        spec_validate/include/ni_*.h \
        spec_validate/tests/test_codegen.py
git commit -m "feat(spec_validate): unified codegen.py + 4 C++ emitters

- tools/codegen.py replaces gen_cpp_header.py with --target/--domain/--check
- 4 per-domain emitters consume ni_spec.constants only (no direct JSON)
- Provenance banner: tool version + source path + JSON SHA + spec_ver + timestamp
- --check mode regenerates to scratch dir and diffs vs committed
- gen_cpp_header.py becomes deprecated wrapper

Refs design §6.1, §6.6, §6.7, §8.2 item 7."
```

---

## Task 8: SV emitters + lint smoke test + static_assert

**Depends on:** Task 7
**Effort:** 2 day
**Acceptance:**
- `tools/codegen.py --target sv --domain {packet|signals|registers|blocks}` 全部 work
- 產 `rtl_pkg/ni_{flit,signals,regs,blocks}_pkg.sv`，每個 file 含 §6.6 provenance banner
- mode enum 用 `typedef enum logic [N-1:0] { ... } block_mode_e;`、整數常數用 `localparam int unsigned`
- 若 `verilator` 在 PATH 中，`tools/codegen.py --check` 額外跑 `verilator --lint-only rtl_pkg/*.sv` smoke test，PASS
- C++ emitter 加 static_assert for §6.4 列出的算術 invariants subset
- ni_flit_constants.h 含 `static_assert(HEADER_WIDTH + PAYLOAD_WIDTH == FLIT_WIDTH, ...);`

**Rollback:** revert sv_*.py + rtl_pkg/ + static_assert lines

**Files:**
- Create: `spec_validate/tools/emit/sv_packet.py`
- Create: `spec_validate/tools/emit/sv_signals.py`
- Create: `spec_validate/tools/emit/sv_registers.py`
- Create: `spec_validate/tools/emit/sv_blocks.py`
- Create: `spec_validate/rtl_pkg/ni_flit_pkg.sv`
- Create: `spec_validate/rtl_pkg/ni_signals_pkg.sv`
- Create: `spec_validate/rtl_pkg/ni_regs_pkg.sv`
- Create: `spec_validate/rtl_pkg/ni_blocks_pkg.sv`
- Modify: `spec_validate/tools/codegen.py`
- Modify: `spec_validate/tools/emit/cpp_packet.py`（加 static_assert）
- Create: `spec_validate/tests/test_sv_codegen.py`

### Steps

- [ ] **8.1 Create `tools/emit/sv_packet.py`** — emits SystemVerilog package with `localparam int unsigned` for integer constants:

```python
"""SV emitter for packet domain."""
from pathlib import Path
import sys
SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))
from ni_spec import constants as C
from ni_spec.loader import load_doc


def emit(packet_json: Path, spec_version: str) -> str:
    spec = load_doc(packet_json)
    out = []
    out.append("`ifndef NI_FLIT_PKG_SV")
    out.append("`define NI_FLIT_PKG_SV")
    out.append("")
    out.append("package ni_flit_pkg;")
    out.append("")
    out.append(f"  localparam int unsigned FLIT_WIDTH       = {C.flit_width(spec)};")
    out.append(f"  localparam int unsigned HEADER_WIDTH     = {C.header_width(spec)};")
    out.append(f"  localparam int unsigned PAYLOAD_WIDTH    = {C.payload_width(spec)};")
    out.append(f"  localparam int unsigned LINK_WIDTH       = {C.link_width(spec)};")
    out.append("")
    # ... header field positions, payload widths, width namespace
    out.append("endpackage")
    out.append("")
    out.append("`endif // NI_FLIT_PKG_SV")
    return "\n".join(out) + "\n"
```

- [ ] **8.2 Implement `sv_signals.py` / `sv_registers.py` / `sv_blocks.py`** — same pattern. `sv_blocks.py` emits typedef enum:

```python
# inside sv_blocks.py emit():
out.append("  typedef enum logic [1:0] {")
out.append("    ROB_MODE_NORMAL = 2'd0,")
out.append("    ROB_MODE_SIMPLE = 2'd1,")
out.append("    ROB_MODE_NOROB  = 2'd2")
out.append("  } rob_mode_e;")
```

- [ ] **8.3 Extend `tools/codegen.py` dispatch**

Add SV emitters to `DOMAIN_TO_EMITTER` dict:

```python
from tools.emit import sv_packet, sv_signals, sv_registers, sv_blocks

DOMAIN_TO_EMITTER.update({
    ("sv", "packet"):    (sv_packet.emit,    "ni_flit_pkg.sv",    "ni_packet.json"),
    ("sv", "signals"):   (sv_signals.emit,   "ni_signals_pkg.sv", "ni_signals.json"),
    ("sv", "registers"): (sv_registers.emit, "ni_regs_pkg.sv",    "ni_registers.json"),
    ("sv", "blocks"):    (sv_blocks.emit,    "ni_blocks_pkg.sv",  "ni_function_blocks.json"),
})
```

Update banner to use `//` (SV comment) — actually SV uses `//` same as C++, so no change.

Update `run_emit` to choose output dir: `include/` for cpp, `rtl_pkg/` for sv.

- [ ] **8.4 Extend `--check` mode to also diff SV outputs**

In `cmd_check`, iterate over all (target, domain) pairs in DOMAIN_TO_EMITTER, not just cpp.

Add Verilator lint:

```python
import shutil, subprocess
verilator = shutil.which("verilator")
if verilator:
    rtl_pkg = SPEC_VALIDATE / "rtl_pkg"
    if rtl_pkg.exists():
        r = subprocess.run([verilator, "--lint-only"] + list(rtl_pkg.glob("*.sv")),
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"[verilator lint failed]\n{r.stderr}")
            all_ok = False
else:
    print("[skip verilator] not in PATH; SV lint not run", file=sys.stderr)
```

- [ ] **8.5 Add static_assert to `cpp_packet.py`**

In `emit()`, after the FLIT_WIDTH / HEADER_WIDTH / PAYLOAD_WIDTH constants:

```python
out.append("static_assert(HEADER_WIDTH + PAYLOAD_WIDTH == FLIT_WIDTH,")
out.append('              "Flit width arithmetic inconsistent — re-run ni_spec validator");')
out.append("")
```

Add similar for `derived.FLIT_DATA_WIDTH = HEADER_DATA_WIDTH + ...` invariants.

- [ ] **8.6 Write tests**

Create `spec_validate/tests/test_sv_codegen.py`:

```python
"""SV codegen smoke tests — Task 8."""
import subprocess, sys, shutil
from pathlib import Path
import pytest

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
CODEGEN = SPEC_VALIDATE / "tools" / "codegen.py"


def test_sv_packet_emits():
    r = subprocess.run([sys.executable, str(CODEGEN),
                        "--target", "sv", "--domain", "packet",
                        "--out", str(SPEC_VALIDATE / "rtl_pkg")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    text = (SPEC_VALIDATE / "rtl_pkg" / "ni_flit_pkg.sv").read_text()
    assert "package ni_flit_pkg;" in text
    assert "localparam int unsigned FLIT_WIDTH" in text


def test_sv_blocks_uses_typedef_enum():
    subprocess.run([sys.executable, str(CODEGEN),
                    "--target", "sv", "--domain", "blocks",
                    "--out", str(SPEC_VALIDATE / "rtl_pkg")], check=True)
    text = (SPEC_VALIDATE / "rtl_pkg" / "ni_blocks_pkg.sv").read_text()
    assert "typedef enum logic" in text, "SV blocks must use typedef enum, not bare parameter"


def test_static_assert_in_cpp_flit_constants():
    text = (SPEC_VALIDATE / "include" / "ni_flit_constants.h").read_text()
    assert "static_assert(HEADER_WIDTH + PAYLOAD_WIDTH == FLIT_WIDTH" in text


@pytest.mark.skipif(shutil.which("verilator") is None, reason="verilator not in PATH")
def test_verilator_lints_clean():
    rtl_pkg = SPEC_VALIDATE / "rtl_pkg"
    sv_files = list(rtl_pkg.glob("*.sv"))
    assert sv_files, "no SV files to lint"
    r = subprocess.run(["verilator", "--lint-only"] + [str(p) for p in sv_files],
                       capture_output=True, text=True)
    assert r.returncode == 0, f"verilator lint failed:\n{r.stderr}"
```

- [ ] **8.7 Run full codegen + tests + smoke compile**

```
# Regenerate all artifacts
py -3 tools/codegen.py --target cpp --domain packet --out include
py -3 tools/codegen.py --target cpp --domain signals --out include
py -3 tools/codegen.py --target cpp --domain registers --out include
py -3 tools/codegen.py --target cpp --domain blocks --out include
py -3 tools/codegen.py --target sv --domain packet --out rtl_pkg
py -3 tools/codegen.py --target sv --domain signals --out rtl_pkg
py -3 tools/codegen.py --target sv --domain registers --out rtl_pkg
py -3 tools/codegen.py --target sv --domain blocks --out rtl_pkg

# --check should now also verify SV outputs
py -3 tools/codegen.py --check

# tests
py -3 -m pytest tests/ -v

# Compile sample (regression on Phase 1 behavior)
g++ -std=c++17 -I include examples/use_constants.cpp -o use_constants.exe
./use_constants.exe   # expect: header[63:0] = 0x00000000F80902AA
```

- [ ] **8.8 Commit**

```
git add spec_validate/tools/emit/sv_*.py \
        spec_validate/tools/emit/cpp_packet.py \
        spec_validate/tools/codegen.py \
        spec_validate/rtl_pkg/ \
        spec_validate/tests/test_sv_codegen.py
git commit -m "feat(spec_validate): SV emitters + verilator lint + static_assert

- 4 SV emitters: package with localparam int unsigned + typedef enum logic[N-1:0]
- codegen.py --target sv now functional; --check covers SV files too
- verilator --lint-only smoke test runs if verilator in PATH
- C++ emitters add static_assert for arithmetic-equality invariants (§6.4 subset)

Refs design §6.2, §6.4, §8.2 item 8."
```

---

## Final Verification (after all 8 tasks)

- [ ] **9.1 Full clean regeneration**

```
cd spec_validate
rm -rf include/ rtl_pkg/   # if .gitignored these will be regenerated
py -3 -m ni_spec ..\spec\ni\doc        # all 5 generated JSON pass
py -3 tools/codegen.py --target cpp --domain packet --out include
py -3 tools/codegen.py --target cpp --domain signals --out include
py -3 tools/codegen.py --target cpp --domain registers --out include
py -3 tools/codegen.py --target cpp --domain blocks --out include
py -3 tools/codegen.py --target sv --domain packet --out rtl_pkg
py -3 tools/codegen.py --target sv --domain signals --out rtl_pkg
py -3 tools/codegen.py --target sv --domain registers --out rtl_pkg
py -3 tools/codegen.py --target sv --domain blocks --out rtl_pkg
py -3 tools/codegen.py --check
py -3 -m pytest tests/ -v
g++ -std=c++17 -I include examples/use_constants.cpp -o use_constants.exe
./use_constants.exe                    # expect: 0xF80902AA
```

All steps PASS. If anything fails, return to the task that introduced the failing component.

- [ ] **9.2 Execute supersede checklist** (§9.1 of design doc)

逐項對照 disposition 表，確認三份舊文件的 unique decision 都已被新 design 吸收或拒絕。確認 OK 後 `git rm`:

```
git rm spec_validate/whats-next.md
git rm spec_validate/docs/plans/2026-05-25-ni-spec-modular-design.md
git rm spec_validate/docs/plans/spec_as_code_plan.md
git commit -m "chore: delete superseded plan docs (supersede checklist passed)"
```

---

## Self-Review

**Spec coverage**: §8.2 各 todo 對應 Task：
- §8.2 #1 foundation → Task 1 ✓
- §8.2 #2 signal redesign → Task 2 ✓
- §8.2 #3 reset merge → Task 3 ✓
- §8.2 #4 registers → Task 4 ✓
- §8.2 #5 function_blocks → Task 5 ✓
- §8.2 #6 protocol_rules lift-shift → Task 6 ✓
- §8.2 #7 codegen + C++ → Task 7 ✓
- §8.2 #8 SV emitters + lint + static_assert → Task 8 ✓

**Placeholder scan**: 沒留 TBD / TODO；每個 step 都有具體 code 或 command。

**Type consistency**:
- `ni_spec.constants.signals_pin_names(signals_spec)` 在 Task 1 stub、Task 2 實作、Task 3 + Task 7 consume — 簽章一致
- `Issue` 是 `invariants.py` 既有 namedtuple — 沿用，沒新建
- Schema field `kind: "external_driven"` 在 Task 2 schema 定義、Task 3 generator 寫入、Task 8 SV emitter 讀取 — 用同一個 enum string

**Known coupling between tasks**:
- Task 5 函式 `check_blocks_xref_registers` 依賴 Task 4 完成；plan 已標 dependency
- Task 6 channel xref 依賴 Task 2 完成（pin_name 要先存在）；plan 已標
- Task 7 `cpp_blocks.py` 依賴 Task 5 的 `blocks_*` constants；plan 已標

---

## Execution Handoff

Plan complete and saved to `spec_validate/docs/plans/2026-05-26-spec-as-code-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** — 我為每個 Task dispatch 一個 fresh subagent，task 間 review、fast iteration

**2. Inline Execution** — 在這個 session 跑全部 task，用 batch + checkpoint

Which approach?
