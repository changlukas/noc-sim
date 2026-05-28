# Pure Parameterization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Refactor packet + signals spec JSON from "denormalized snapshot + validator cross-check" to "purely symbolic + on-the-fly elaborator helper eval". Elaborated `.h`/`.sv` output stays byte-identical (acceptance criterion).

**Architecture:** Sequential phases. Each phase has hard `byte-identical gate` (`tools/codegen.py --check` exit 0) before next starts. Order: build helpers → update elaborators → migrate schema → drop snapshot fields → simplify validator/generator.

**Tech Stack:** Python 3 (stdlib `ast` only — no external deps), pytest, CMake + GoogleTest (consumer tests untouched).

**Reference:** `docs/superpowers/specs/2026-05-28-pure-parameterization-design.md` + `cross-review/pure-param-REVIEW_AGGREGATE.md`

**Working dir:** `E:/05_NoC/noc-sim/.worktrees/pure-param-refactor` (new worktree from `feat/spec-as-code`, branch `feat/pure-param-refactor`)

**Invariants (must hold throughout):**
1. `tools/codegen.py --check` exit 0 after EVERY task (byte-identical)
2. `cd c_model/build && ctest` no regression (consumer untouched)
3. Elaborator iterates source declaration order; no `sorted()` / reorder
4. Describe codegen action as "elaborate" (not "emit")
5. OSS-first (stdlib only for this refactor)

**Pytest count baseline before refactor:** ~101 passed. Target after: ~120 passed.

---

## Task 1: Foundation — exceptions + golden fixtures + order-invariance test

**Goal**: lay safety nets before any actual refactor. Golden fixtures capture the current `.h`/`.sv` so byte-identical regression is detectable mechanically. Order-invariance test catches the K-1 tripwire (`sorted()` would break byte-identical silently).

**Files:**
- Create: `spec_validate/ni_spec/exceptions.py`
- Create: `spec_validate/tests/golden/ni_flit_constants.h.golden`
- Create: `spec_validate/tests/golden/ni_flit_pkg.sv.golden`
- Create: `spec_validate/tests/golden/ni_signals.h.golden`
- Create: `spec_validate/tests/golden/ni_signals_pkg.sv.golden`
- Create: `spec_validate/tests/golden/ni_regs.h.golden`
- Create: `spec_validate/tests/golden/ni_regs_pkg.sv.golden`
- Create: `spec_validate/tests/test_byte_identical_golden.py`
- Create: `spec_validate/tests/test_order_invariance.py`

- [ ] **Step 1: Create exceptions module**

Create `spec_validate/ni_spec/exceptions.py`:

```python
"""Resolver exception hierarchy for pure-parameterization refactor."""

class SpecResolveError(Exception):
    """Base class for all elaborator helper failures."""


class ExprSyntaxError(SpecResolveError):
    """ast.parse failed on a width_param expression."""


class ExprNameError(SpecResolveError):
    """A symbol in width_param was not found in any namespace
    (field_widths, port_parameters, derived totals)."""


class ExprNotAllowedError(SpecResolveError):
    """width_param contains a forbidden ast node
    (function call, attribute access, subscript, comprehension, lambda, ...)."""


class FieldNotFoundError(SpecResolveError):
    """A field name passed to a helper does not exist in the spec."""
```

- [ ] **Step 2: Capture golden fixtures from current elaborated output**

From worktree root, copy the current 6 elaborated artifacts into fixtures (timestamp banner line will be stripped at compare-time):

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor
mkdir -p spec_validate/tests/golden
cp spec_validate/include/ni_flit_constants.h    spec_validate/tests/golden/ni_flit_constants.h.golden
cp spec_validate/include/ni_signals.h           spec_validate/tests/golden/ni_signals.h.golden
cp spec_validate/include/ni_regs.h              spec_validate/tests/golden/ni_regs.h.golden
cp spec_validate/rtl_pkg/ni_flit_pkg.sv         spec_validate/tests/golden/ni_flit_pkg.sv.golden
cp spec_validate/rtl_pkg/ni_signals_pkg.sv      spec_validate/tests/golden/ni_signals_pkg.sv.golden
cp spec_validate/rtl_pkg/ni_regs_pkg.sv         spec_validate/tests/golden/ni_regs_pkg.sv.golden
```

- [ ] **Step 3: Write golden-output regression test**

Create `spec_validate/tests/test_byte_identical_golden.py`:

```python
"""Byte-identical regression gate for pure-parameterization refactor.

Captures pre-refactor elaborated artifacts as fixtures.
Re-elaborates each domain and diffs against golden (timestamp line excluded).
Any post-refactor task that changes elaborator output trips this test.
"""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
GOLDEN_DIR    = Path(__file__).resolve().parent / "golden"


def _strip_timestamp(text: str) -> str:
    """Remove the 'Generated at: <UTC>' line (only line that varies between regens)."""
    return re.sub(r"^//\s*Generated at:.*$\n?", "", text, flags=re.MULTILINE)


def _regen(target: str, domain: str, out_dir: Path) -> str:
    """Run codegen for one (target, domain) into a temp dir, return content."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [sys.executable, str(SPEC_VALIDATE / "tools" / "codegen.py"),
             "--target", target, "--domain", domain, "--out", tmp],
            capture_output=True, text=True, cwd=str(SPEC_VALIDATE),
        )
        assert result.returncode == 0, f"codegen failed: {result.stderr}"
        files = list(Path(tmp).iterdir())
        assert len(files) == 1, f"expected single output, got {files}"
        return files[0].read_text(encoding="utf-8")


def _golden(name: str) -> str:
    return (GOLDEN_DIR / f"{name}.golden").read_text(encoding="utf-8")


def test_golden_cpp_packet():
    assert _strip_timestamp(_regen("cpp", "packet", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_flit_constants.h"))


def test_golden_cpp_signals():
    assert _strip_timestamp(_regen("cpp", "signals", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_signals.h"))


def test_golden_cpp_registers():
    """Registers domain is out of scope but must not regress."""
    assert _strip_timestamp(_regen("cpp", "registers", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_regs.h"))


def test_golden_sv_packet():
    assert _strip_timestamp(_regen("sv", "packet", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_flit_pkg.sv"))


def test_golden_sv_signals():
    assert _strip_timestamp(_regen("sv", "signals", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_signals_pkg.sv"))


def test_golden_sv_registers():
    assert _strip_timestamp(_regen("sv", "registers", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_regs_pkg.sv"))
```

- [ ] **Step 4: Write order-invariance test (catches K-1 tripwire)**

Create `spec_validate/tests/test_order_invariance.py`:

```python
"""Verify the elaborator iterates source declaration order. Catches K-1 tripwire:
if anyone applies sorted() inside the helper or elaborator, byte-identical
golden tests still pass (alphabetic happens to match declaration in many
cases) but THIS test specifically asserts the names appear in declaration
order."""
from __future__ import annotations
import json
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
HEADER_PATH = SPEC_VALIDATE / "include" / "ni_flit_constants.h"
JSON_PATH   = SPEC_VALIDATE / "generated" / "ni_packet.json"


def test_field_widths_declaration_order_preserved_in_header():
    """ni_flit_constants.h emits `constexpr int <NAME>_WIDTH = ...` lines for
    each field_widths entry; their order must match field_widths{} insertion
    order in the JSON (= MD source declaration order)."""
    spec = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    text = HEADER_PATH.read_text(encoding="utf-8")
    # Extract `<NAME>_WIDTH` constants in order of appearance in header
    import re
    header_order = re.findall(r"constexpr\s+int\s+(\w+_WIDTH)\s*=", text)
    # Filter to field_widths keys (some _WIDTH consts come from elsewhere)
    fw_names_in_order = list(spec["flit"]["field_widths"].keys())
    # Subset of header_order that matches fw_names_in_order should appear in same order
    header_fw_only = [n for n in header_order if n in fw_names_in_order]
    assert header_fw_only == fw_names_in_order, (
        f"field_widths declaration order broken!\n"
        f"  expected: {fw_names_in_order}\n"
        f"  got:      {header_fw_only}"
    )


def test_header_fields_declaration_order_preserved():
    """Header fields (DST_ID_LSB, SRC_ID_LSB, ...) must appear in declaration order."""
    spec = json.loads(JSON_PATH.read_text(encoding="utf-8"))
    text = HEADER_PATH.read_text(encoding="utf-8")
    import re
    # Extract field-position consts in order of appearance
    lsb_order = re.findall(r"constexpr\s+int\s+(\w+)_LSB\s*=", text)
    # Expected: header_fields[].name (uppercased), excluding width=0 fields
    expected = []
    for f in spec["flit"]["header_fields"]:
        if f.get("width", 1) != 0:  # width=0 fields skip LSB emission
            expected.append(f["name"].upper())
    assert lsb_order == expected
```

- [ ] **Step 5: Run the 2 new test files — both should pass against current (pre-refactor) state**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest tests/test_byte_identical_golden.py tests/test_order_invariance.py -v 2>&1 | tail -20
```

Expected: 8 tests PASS (6 golden + 2 order). If any fail, golden fixtures or order assumptions are wrong — STOP and investigate before continuing.

- [ ] **Step 6: Full pytest + drift gate baseline check**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: pytest passes (count = previous baseline + 8 = ~109); `--check` exit 0.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/ni_spec/exceptions.py \
        spec_validate/tests/golden/ \
        spec_validate/tests/test_byte_identical_golden.py \
        spec_validate/tests/test_order_invariance.py
git commit -m "$(cat <<'EOF'
feat(spec_validate): foundation — exceptions + golden fixtures + order tests

Task 1 of pure-parameterization refactor.
 - ni_spec/exceptions.py: 4-class hierarchy for elaborator helper errors
 - tests/golden/*.golden: pre-refactor .h/.sv snapshots (6 files)
 - tests/test_byte_identical_golden.py: regen + diff against golden (timestamp
   stripped). Catches any byte-identical regression during refactor.
 - tests/test_order_invariance.py: asserts field_widths and header_fields
   appear in declaration order (catches sorted() tripwire).

These tests pass against current pre-refactor state; subsequent tasks must
keep them passing.

Refs spec doc Invariant 8 (order preservation) + Acceptance Criterion A.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Packet elaborator helpers in `constants.py` + unit tests

**Goal**: Add the eval engine + per-field helpers. They compute the same values that are currently stored in JSON. Test independently. Don't wire elaborators yet (Task 3).

**Files:**
- Modify: `spec_validate/ni_spec/constants.py` (add new helpers; keep existing thin getters for now)
- Create: `spec_validate/tests/test_constants_resolver.py`

- [ ] **Step 1: Add `packet_eval_expr` (ast safe-walk evaluator)**

Append to `spec_validate/ni_spec/constants.py`:

```python
import ast as _ast
from typing import Mapping as _Mapping
from .exceptions import (
    ExprSyntaxError, ExprNameError, ExprNotAllowedError, FieldNotFoundError,
)


# ─── ast safe-walk evaluator ──────────────────────────────
_ALLOWED_BINOPS = {_ast.Add, _ast.Sub, _ast.Mult, _ast.FloorDiv, _ast.Mod}
_ALLOWED_UNARYOPS = {_ast.UAdd, _ast.USub}


def _eval_ast(node, namespace: _Mapping[str, int]) -> int:
    if isinstance(node, _ast.Constant):
        if isinstance(node.value, int):
            return node.value
        raise ExprNotAllowedError(f"only integer literals allowed, got {type(node.value).__name__}")
    if isinstance(node, _ast.Name):
        if node.id in namespace:
            return int(namespace[node.id])
        raise ExprNameError(f"symbol '{node.id}' not found in namespace")
    if isinstance(node, _ast.BinOp):
        if type(node.op) not in _ALLOWED_BINOPS:
            raise ExprNotAllowedError(f"forbidden binop {type(node.op).__name__}")
        l = _eval_ast(node.left,  namespace)
        r = _eval_ast(node.right, namespace)
        op_map = {_ast.Add: int.__add__, _ast.Sub: int.__sub__, _ast.Mult: int.__mul__,
                  _ast.FloorDiv: int.__floordiv__, _ast.Mod: int.__mod__}
        return op_map[type(node.op)](l, r)
    if isinstance(node, _ast.UnaryOp):
        if type(node.op) not in _ALLOWED_UNARYOPS:
            raise ExprNotAllowedError(f"forbidden unaryop {type(node.op).__name__}")
        v = _eval_ast(node.operand, namespace)
        return v if isinstance(node.op, _ast.UAdd) else -v
    raise ExprNotAllowedError(f"forbidden ast node {type(node).__name__}")


def packet_eval_expr(spec: dict, expr) -> int:
    """Evaluate a width_param expression in the packet field_widths namespace.

    Handles:
      - integer literal (returned as-is)
      - the special string "derived" → caller must handle this case
        before calling packet_eval_expr (raises ExprNotAllowedError otherwise)
      - any other string: parsed with ast and walked with allowlist
    """
    if isinstance(expr, int):
        return expr
    if expr == "derived":
        raise ExprNotAllowedError(
            "width_param='derived' must be resolved by payload_field_width, not packet_eval_expr"
        )
    if not isinstance(expr, str):
        raise ExprSyntaxError(f"width_param must be str or int, got {type(expr).__name__}")
    try:
        tree = _ast.parse(expr, mode="eval")
    except SyntaxError as e:
        raise ExprSyntaxError(f"cannot parse '{expr}': {e}") from e
    namespace = spec.get("flit", {}).get("field_widths", {})
    return _eval_ast(tree.body, namespace)


def packet_param_value(spec: dict, name: str) -> int:
    """Look up a parameter in flit.field_widths."""
    fw = spec.get("flit", {}).get("field_widths", {})
    if name not in fw:
        raise ExprNameError(f"parameter '{name}' not in field_widths")
    return int(fw[name])
```

- [ ] **Step 2: Add per-header-field helpers**

Continue appending to `spec_validate/ni_spec/constants.py`:

```python
# ─── Per-header-field helpers ─────────────────────────────
def _find_header_field(spec: dict, name: str) -> dict:
    for f in spec["flit"]["header_fields"]:
        if f["name"] == name:
            return f
    raise FieldNotFoundError(f"header field '{name}' not found")


def header_field_width(spec: dict, name: str) -> int:
    """Resolve width by evaluating width_param against field_widths."""
    f = _find_header_field(spec, name)
    return packet_eval_expr(spec, f["width_param"])


def header_field_position(spec: dict, name: str) -> tuple[int, int] | None:
    """(lsb, msb) computed cumulatively in declaration order.
    Returns None for width-0 placeholders."""
    cumulative = 0
    for f in spec["flit"]["header_fields"]:
        w = header_field_width(spec, f["name"])
        if f["name"] == name:
            return None if w == 0 else (cumulative, cumulative + w - 1)
        cumulative += w
    raise FieldNotFoundError(f"header field '{name}' not found")


def header_field_enabled(spec: dict, name: str) -> bool:
    f = _find_header_field(spec, name)
    return bool(f.get("enabled", True))


def header_width(spec: dict) -> int:
    """Sum of all header field widths (regardless of enabled)."""
    return sum(header_field_width(spec, f["name"])
               for f in spec["flit"]["header_fields"])
```

- [ ] **Step 3: Add per-payload-channel helpers (handles "derived" literal)**

Continue appending:

```python
# ─── Per-payload-channel helpers ──────────────────────────
def _find_channel(spec: dict, channel: str) -> dict:
    for ch in spec["flit"]["payload_channels"]:
        if ch["name"] == channel:
            return ch
    raise FieldNotFoundError(f"channel '{channel}' not found")


def payload_channel_width(spec: dict, channel: str) -> int:
    """Authored channel-level metadata."""
    return int(_find_channel(spec, channel)["payload_width"])


def payload_field_width(spec: dict, channel: str, name: str) -> int:
    """Resolve width. Special case: width_param='derived' →
    payload_width(channel) - sum of all other fields' widths."""
    ch = _find_channel(spec, channel)
    target = None
    others_sum = 0
    for f in ch["fields"]:
        if f["name"] == name:
            target = f
        else:
            wp = f["width_param"]
            if wp == "derived":
                raise ExprNotAllowedError(
                    f"channel '{channel}' has multiple 'derived' fields; "
                    f"only one allowed per channel (must be last)"
                )
            others_sum += packet_eval_expr(spec, wp)
    if target is None:
        raise FieldNotFoundError(f"payload field '{name}' not in channel '{channel}'")
    if target["width_param"] == "derived":
        return payload_channel_width(spec, channel) - others_sum
    return packet_eval_expr(spec, target["width_param"])


def payload_field_position(spec: dict, channel: str, name: str) -> tuple[int, int] | None:
    """(lsb, msb) within the channel's payload, cumulative declaration order."""
    ch = _find_channel(spec, channel)
    cumulative = 0
    for f in ch["fields"]:
        w = payload_field_width(spec, channel, f["name"])
        if f["name"] == name:
            return None if w == 0 else (cumulative, cumulative + w - 1)
        cumulative += w
    raise FieldNotFoundError(f"payload field '{name}' not in channel '{channel}'")
```

- [ ] **Step 4: Add derived total helpers**

Continue appending:

```python
# ─── Derived totals (computed on demand) ──────────────────
def payload_width(spec: dict) -> int:
    """Max of all payload_channels' payload_width (channels are union-typed
    by axi_ch encoding; flit allocates max channel width)."""
    return max(payload_channel_width(spec, ch["name"])
               for ch in spec["flit"]["payload_channels"])


def flit_width(spec: dict) -> int:
    return header_width(spec) + payload_width(spec)


def link_width(spec: dict) -> int:
    """Match current derived.LINK_WIDTH semantics.
    Per packet_format.md, link_width includes ECC overhead."""
    # Current generator computes link_width as flit_width itself
    # (no extra ECC on link, ECC is within flit). Preserve that.
    return flit_width(spec)


def flit_data_width(spec: dict) -> int:
    """FLIT_DATA_WIDTH = HEADER_WIDTH - FLIT_ECC_WIDTH + PAYLOAD_WIDTH"""
    fw = spec.get("flit", {}).get("field_widths", {})
    ecc_w = int(fw.get("FLIT_ECC_WIDTH", 0))
    return header_width(spec) - ecc_w + payload_width(spec)


def header_data_width(spec: dict) -> int:
    """HEADER_DATA_WIDTH = HEADER_WIDTH - FLIT_ECC_WIDTH"""
    fw = spec.get("flit", {}).get("field_widths", {})
    return header_width(spec) - int(fw.get("FLIT_ECC_WIDTH", 0))


def wstrb_width(spec: dict) -> int:
    """WSTRB_WIDTH = NOC_DATA_WIDTH / 8"""
    fw = spec.get("flit", {}).get("field_widths", {})
    return int(fw.get("NOC_DATA_WIDTH", 0)) // 8
```

- [ ] **Step 5: Write unit tests for all helpers**

Create `spec_validate/tests/test_constants_resolver.py`:

```python
"""Unit tests for pure-parameterization elaborator helpers."""
from __future__ import annotations
import pytest
from pathlib import Path
from ni_spec.loader import load_doc
from ni_spec import constants as C
from ni_spec.exceptions import (
    ExprSyntaxError, ExprNameError, ExprNotAllowedError, FieldNotFoundError,
)

SPEC_VALIDATE = Path(__file__).resolve().parent.parent


@pytest.fixture
def packet_spec():
    return load_doc(SPEC_VALIDATE / "generated" / "ni_packet.json")


# ── packet_eval_expr ──────────────────────────────────────
def test_eval_integer_literal(packet_spec):
    assert C.packet_eval_expr(packet_spec, "0") == 0
    assert C.packet_eval_expr(packet_spec, "42") == 42


def test_eval_single_symbol(packet_spec):
    # X_WIDTH = 4 per ni_packet.json field_widths
    assert C.packet_eval_expr(packet_spec, "X_WIDTH") == 4


def test_eval_addition(packet_spec):
    # X_WIDTH + Y_WIDTH = 4 + 4 = 8
    assert C.packet_eval_expr(packet_spec, "X_WIDTH + Y_WIDTH") == 8


def test_eval_subtraction_and_mul(packet_spec):
    assert C.packet_eval_expr(packet_spec, "X_WIDTH - 1") == 3
    assert C.packet_eval_expr(packet_spec, "X_WIDTH * 2") == 8


def test_eval_unknown_symbol(packet_spec):
    with pytest.raises(ExprNameError, match="MISSING_WIDTH"):
        C.packet_eval_expr(packet_spec, "MISSING_WIDTH")


def test_eval_forbidden_function_call(packet_spec):
    with pytest.raises(ExprNotAllowedError):
        C.packet_eval_expr(packet_spec, "max(X_WIDTH, Y_WIDTH)")


def test_eval_forbidden_attribute(packet_spec):
    with pytest.raises(ExprNotAllowedError):
        C.packet_eval_expr(packet_spec, "X_WIDTH.bit_length")


def test_eval_forbidden_subscript(packet_spec):
    with pytest.raises(ExprNotAllowedError):
        C.packet_eval_expr(packet_spec, "X_WIDTH[0]")


def test_eval_derived_literal_rejected(packet_spec):
    """packet_eval_expr rejects 'derived' — must be handled by payload_field_width."""
    with pytest.raises(ExprNotAllowedError, match="derived"):
        C.packet_eval_expr(packet_spec, "derived")


def test_eval_syntax_error(packet_spec):
    with pytest.raises(ExprSyntaxError):
        C.packet_eval_expr(packet_spec, "X_WIDTH +")


# ── header_field_width / position / enabled ───────────────
def test_header_field_width_basic(packet_spec):
    # axi_ch width = AXI_CH_WIDTH = 3
    assert C.header_field_width(packet_spec, "axi_ch") == 3


def test_header_field_width_expression(packet_spec):
    # src_id width = X_WIDTH + Y_WIDTH = 8
    assert C.header_field_width(packet_spec, "src_id") == 8


def test_header_field_width_zero(packet_spec):
    # noc_qos width = NOC_QOS_WIDTH = 0 (reserved placeholder)
    assert C.header_field_width(packet_spec, "noc_qos") == 0


def test_header_field_position_cumulative(packet_spec):
    # axi_ch is first non-zero field (after noc_qos which is width=0)
    # Expected: axi_ch at (0, 2), src_id at (3, 10), dst_id next
    assert C.header_field_position(packet_spec, "axi_ch") == (0, 2)
    assert C.header_field_position(packet_spec, "src_id") == (3, 10)


def test_header_field_position_zero_width_returns_none(packet_spec):
    assert C.header_field_position(packet_spec, "noc_qos") is None


def test_header_field_position_disabled_still_positioned(packet_spec):
    # route_par is enabled=false but width=1, must still have a position
    pos = C.header_field_position(packet_spec, "route_par")
    assert pos is not None
    assert pos[1] - pos[0] == 0  # width 1


def test_header_field_enabled(packet_spec):
    assert C.header_field_enabled(packet_spec, "src_id") is True
    assert C.header_field_enabled(packet_spec, "route_par") is False  # padding


def test_header_field_not_found(packet_spec):
    with pytest.raises(FieldNotFoundError):
        C.header_field_width(packet_spec, "nonexistent_field")


# ── payload_field_width / position (incl. "derived") ──────
def test_payload_field_width_basic(packet_spec):
    # AW.awid width = AXI_ID_WIDTH = 8
    assert C.payload_field_width(packet_spec, "AW", "awid") == 8


def test_payload_field_width_derived(packet_spec):
    # AW.aw_rsvd: width_param='derived' → payload_width(AW=108) - sum of other AW fields
    w = C.payload_field_width(packet_spec, "AW", "aw_rsvd")
    # Sum of other AW fields = 108 - aw_rsvd; recompute to verify
    ch = next(c for c in packet_spec["flit"]["payload_channels"] if c["name"] == "AW")
    other_sum = sum(
        C.payload_field_width(packet_spec, "AW", f["name"])
        for f in ch["fields"] if f["name"] != "aw_rsvd"
    )
    assert w == 108 - other_sum


def test_payload_field_position(packet_spec):
    # First field in AW is awid at (0, 7)
    assert C.payload_field_position(packet_spec, "AW", "awid") == (0, 7)


# ── derived totals ────────────────────────────────────────
def test_header_width(packet_spec):
    # Match current derived.HEADER_WIDTH
    assert C.header_width(packet_spec) == packet_spec["flit"]["derived"]["HEADER_WIDTH"]


def test_payload_width(packet_spec):
    assert C.payload_width(packet_spec) == packet_spec["flit"]["derived"]["PAYLOAD_WIDTH"]


def test_flit_width(packet_spec):
    assert C.flit_width(packet_spec) == packet_spec["flit"]["derived"]["FLIT_WIDTH"]
```

- [ ] **Step 6: Run new tests — verify all pass**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest tests/test_constants_resolver.py -v 2>&1 | tail -30
```

Expected: ~24 tests PASS. Each test verifies the helper computes the same value that's currently stored in JSON (`derived.HEADER_WIDTH`, etc.) → helpers proven correct against ground truth.

- [ ] **Step 7: Full pytest + drift gate**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: pytest ~133 passed (109 + 24 new); `--check` exit 0 (no elaborator changes yet).

- [ ] **Step 8: Commit**

```bash
git add spec_validate/ni_spec/constants.py spec_validate/tests/test_constants_resolver.py
git commit -m "$(cat <<'EOF'
feat(spec_validate): packet elaborator helpers + 24 unit tests

Task 2 of pure-parameterization refactor. Adds (to ni_spec/constants.py):
 - packet_eval_expr: ast safe-walk evaluator (+ - * // % parens, allowlist)
 - packet_param_value: look up in field_widths
 - header_field_{width,position,enabled}: per-header-field resolvers
 - payload_channel_width / payload_field_{width,position}: handles
   width_param="derived" by subtracting other fields from payload_width
 - header_width, payload_width, flit_width, link_width,
   flit_data_width, header_data_width, wstrb_width: derived totals

24 unit tests cover: eval correctness (literal/symbol/+/-/*/forbidden/
unknown), header positions (incl. zero-width + disabled), payload
"derived" semantics, derived totals match current snapshot values.

Elaborators not yet wired to these (Task 3 does that). --check still exit 0.

Refs spec doc §Components.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Update `cpp_packet.py` + `sv_packet.py` to use helpers (byte-identical gate)

**Goal**: Replace direct JSON dict access in packet elaborators with helper calls. Output stays byte-identical (helpers compute same values as stored).

**Files:**
- Modify: `spec_validate/tools/elaborate/cpp_packet.py`
- Modify: `spec_validate/tools/elaborate/sv_packet.py`

- [ ] **Step 1: Replace direct access in `cpp_packet.py` header field section**

In `spec_validate/tools/elaborate/cpp_packet.py`, find the section that iterates `spec["flit"]["header_fields"]` and reads `f["width"]`, `f["lsb"]`, `f["msb"]`. Replace with helper calls.

Original (around lines 50-75):

```python
out.append("namespace header {")
for f in spec["flit"]["header_fields"]:
    n = f["name"].upper()
    enabled_val = "true" if f.get("enabled", True) else "false"
    if f.get("width", 1) == 0:
        out.append(f"constexpr int  {n}_WIDTH   = 0;  // reserved placeholder (width=0 -- not in flit)")
        out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
    else:
        out.append(f"constexpr int  {n}_LSB     = {f['lsb']};")
        out.append(f"constexpr int  {n}_MSB     = {f['msb']};")
        out.append(f"constexpr int  {n}_WIDTH   = {f['width']};")
        out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
```

Replace with:

```python
out.append("namespace header {")
for f in spec["flit"]["header_fields"]:
    n = f["name"].upper()
    width = C.header_field_width(spec, f["name"])
    enabled_val = "true" if C.header_field_enabled(spec, f["name"]) else "false"
    if width == 0:
        out.append(f"constexpr int  {n}_WIDTH   = 0;  // reserved placeholder (width=0 -- not in flit)")
        out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
    else:
        pos = C.header_field_position(spec, f["name"])
        out.append(f"constexpr int  {n}_LSB     = {pos[0]};")
        out.append(f"constexpr int  {n}_MSB     = {pos[1]};")
        out.append(f"constexpr int  {n}_WIDTH   = {width};")
        out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
```

- [ ] **Step 2: Replace direct access in `cpp_packet.py` top-level widths + payload section**

Find the section that emits top-level widths (FLIT_WIDTH, HEADER_WIDTH, etc.) — originally reads from `spec["flit"]["derived"]`. Replace with helper calls.

Original (around lines 30-50):

```python
out.append(f"constexpr int FLIT_WIDTH        = {C.flit_width(spec)};")
out.append(f"constexpr int HEADER_WIDTH      = {C.header_width(spec)};")
out.append(f"constexpr int PAYLOAD_WIDTH     = {C.payload_width(spec)};")
out.append(f"constexpr int LINK_WIDTH        = {C.link_width(spec)};")
derived = spec["flit"]["derived"]
for k in ("FLIT_DATA_WIDTH", "HEADER_DATA_WIDTH", "WSTRB_WIDTH"):
    if k in derived:
        out.append(f"constexpr int {k:<15} = {derived[k]};")
```

If `C.flit_width(spec)` was already using helper-style call (verify the actual file), Step 2 may only need replacing the last 3 lines:

```python
out.append(f"constexpr int FLIT_DATA_WIDTH   = {C.flit_data_width(spec)};")
out.append(f"constexpr int HEADER_DATA_WIDTH = {C.header_data_width(spec)};")
out.append(f"constexpr int WSTRB_WIDTH       = {C.wstrb_width(spec)};")
```

Verify by re-reading the file first; replace whatever still directly reads `spec["flit"]["derived"]`.

- [ ] **Step 3: Replace direct access in `cpp_packet.py` payload channel section**

Find the section that emits `namespace payload { constexpr int AW_WIDTH = ...; }` — originally reads `ch["payload_width"]`. Replace:

Original (around lines 75-90):

```python
out.append("namespace payload {")
for ch in spec["flit"]["payload_channels"]:
    cn = ch["name"].upper()
    out.append(f"constexpr int {cn}_WIDTH = {ch['payload_width']};")
```

Replace with:

```python
out.append("namespace payload {")
for ch in spec["flit"]["payload_channels"]:
    cn = ch["name"].upper()
    out.append(f"constexpr int {cn}_WIDTH = {C.payload_channel_width(spec, ch['name'])};")
```

- [ ] **Step 4: Apply same treatment to `sv_packet.py`**

In `spec_validate/tools/elaborate/sv_packet.py`, repeat the same pattern: find every `f["width"]` / `f["lsb"]` / `f["msb"]` / `ch["payload_width"]` / `spec["flit"]["derived"]` direct access and replace with corresponding `C.*` helper.

The SV file's output structure differs from C++ (uses `localparam` instead of `constexpr int`) but the data flow is identical. Be surgical — don't change the emit string templates, only the source of the numbers.

- [ ] **Step 5: Run drift gate — must still be exit 0**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: exit 0 (helpers compute same values → output byte-identical).

If exit 1, diff shows where helpers diverge from stored — that's a helper bug, NOT a refactor decision. Fix the helper.

- [ ] **Step 6: Run golden + order tests**

```bash
py -3 -m pytest tests/test_byte_identical_golden.py tests/test_order_invariance.py -v 2>&1 | tail -10
```

Expected: 8 PASS.

- [ ] **Step 7: Full pytest**

```bash
py -3 -m pytest -q 2>&1 | tail -3
```

Expected: ~133 PASS (no regression from Task 2).

- [ ] **Step 8: Commit**

```bash
git add spec_validate/tools/elaborate/cpp_packet.py spec_validate/tools/elaborate/sv_packet.py
git commit -m "$(cat <<'EOF'
refactor(spec_validate): packet elaborators use helpers (byte-identical)

Task 3 of pure-parameterization refactor. cpp_packet.py and sv_packet.py
now go through ni_spec.constants helper API instead of direct dict access
to spec["flit"]["header_fields"][i]["width"/"lsb"/"msb"] etc.

Output stays byte-identical (helpers compute the same values currently
stored in JSON). --check exit 0, golden tests pass.

This is the first step toward dropping the stored snapshot from JSON
(Task 6 does that — by then helpers no longer have stored fallback).

Refs spec doc Invariant 2 (firewall must be real).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Rewrite packet tests that hardcode `width`/`lsb`/`msb`

**Goal**: Find every test that asserts against stored JSON dict values directly. Rewrite to assert via helpers. Tests still pass against current JSON (stored values match helper output).

**Files:**
- Modify: `spec_validate/tests/test_codegen.py` (lines reading `spec[...]["width"]` etc.)
- Modify: `spec_validate/tests/test_codegen_sv.py` (same)
- Modify: any other test file reading `f["width"]` / `f["lsb"]` / `f["msb"]` directly

- [ ] **Step 1: Find all tests that directly access stored width/lsb/msb fields**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
grep -rn '\["width"\]\|\["lsb"\]\|\["msb"\]\|\["derived"\]\|\["payload_width"\]\|\["default"\]' tests/ 2>&1
```

For each match, decide:
- Is it asserting a SPECIFIC value (e.g. `assert f["width"] == 8`)? → Rewrite via helper.
- Is it reading the value to use in further computation? → Rewrite via helper.
- Is it asserting the FIELD EXISTS? → Will need adjustment after Task 6 (when field is dropped). For now keep but note.

- [ ] **Step 2: Rewrite each hit**

Pattern for rewrites:

```python
# BEFORE
assert spec["flit"]["header_fields"][2]["width"] == 8

# AFTER
assert C.header_field_width(spec, "src_id") == 8
```

```python
# BEFORE
for f in spec["flit"]["header_fields"]:
    if f["width"] > 0:
        assert f["lsb"] < f["msb"] or f["lsb"] == f["msb"]

# AFTER
for f in spec["flit"]["header_fields"]:
    if C.header_field_width(spec, f["name"]) > 0:
        pos = C.header_field_position(spec, f["name"])
        assert pos[0] <= pos[1]
```

Apply this pattern to every grep hit from Step 1. Add `from ni_spec import constants as C` import to top of any test file that didn't already have it.

- [ ] **Step 3: Run modified test files individually**

For each test file you modified, run it standalone:

```bash
py -3 -m pytest tests/test_codegen.py -v 2>&1 | tail -10
py -3 -m pytest tests/test_codegen_sv.py -v 2>&1 | tail -10
# ... any other touched files
```

Expected: all pass (helper values match stored values currently).

- [ ] **Step 4: Full pytest + drift gate**

```bash
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: ~133 PASS; `--check` exit 0.

- [ ] **Step 5: Commit**

```bash
git add spec_validate/tests/
git commit -m "$(cat <<'EOF'
refactor(spec_validate): packet tests via helper API, not dict access

Task 4 of pure-parameterization refactor. All assertions that previously
read spec dict directly (f["width"], f["lsb"], etc.) now go through
ni_spec.constants helpers. Currently pass against current JSON (stored
values match helper output); after Task 6 drops the stored fields,
these tests still pass because helpers compute on-the-fly.

Refs spec doc §Testing Strategy L2.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Update `ni_packet.schema.json` to allow missing resolved fields

**Goal**: Schema currently REQUIRES `width`/`lsb`/`msb`/`derived.*`. Schema validation will fail in Task 6 when we drop those. Update schema first to allow missing.

**Files:**
- Modify: `spec_validate/generated/ni_packet.schema.json`

- [ ] **Step 1: Inspect current schema requirements**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
grep -A 3 '"required"' generated/ni_packet.schema.json | head -40
```

Identify the JSON paths where `width`, `lsb`, `msb`, `payload_width`, or `derived` properties are in a `"required"` array.

- [ ] **Step 2: Remove `width`/`lsb`/`msb` from header_fields required**

Edit `spec_validate/generated/ni_packet.schema.json`:

Find the header_fields object schema (likely around line 54). It has something like:

```json
"header_fields": {
  "type": "array",
  "items": {
    "type": "object",
    "required": ["name", "width_param", "width", "lsb", "msb", "enabled"]
  }
}
```

Change `"required"` to drop the resolved fields:

```json
"required": ["name", "width_param", "enabled"]
```

Note: do NOT remove the `properties` definitions themselves (a JSON instance is still allowed to HAVE these fields, just not required to). This keeps backward compat during transition — Task 6 will regen JSON without them.

- [ ] **Step 3: Same for payload_channels[].fields[]**

Find the payload_channels schema. Drop `width/lsb/msb` from the inner `fields[].required`. Keep `payload_width` REQUIRED at channel level (authored metadata, not derived).

- [ ] **Step 4: Drop `derived` from top-level flit required**

Find the `flit` object schema. If `derived` is in its `required` array, remove it. (Properties can still be present.)

- [ ] **Step 5: Verify schema is still valid JSON Schema syntax**

```bash
py -3 -c "
import json, jsonschema
schema = json.load(open('generated/ni_packet.schema.json'))
jsonschema.Draft202012Validator.check_schema(schema)
print('schema valid')"
```

Expected: prints "schema valid". No exception.

- [ ] **Step 6: Run schema validation on current ni_packet.json**

The current JSON HAS the now-optional fields → must still validate clean.

```bash
py -3 -c "
import json, jsonschema
schema = json.load(open('generated/ni_packet.schema.json'))
data   = json.load(open('generated/ni_packet.json'))
jsonschema.validate(instance=data, schema=schema)
print('current JSON validates against relaxed schema')"
```

Expected: prints success.

- [ ] **Step 7: Full pytest + drift gate**

```bash
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: pass; exit 0.

- [ ] **Step 8: Commit**

```bash
git add spec_validate/generated/ni_packet.schema.json
git commit -m "$(cat <<'EOF'
chore(spec_validate): relax ni_packet.schema.json (resolved fields optional)

Task 5 of pure-parameterization refactor. Drops width/lsb/msb from
header_fields[].required and payload_channels[].fields[].required;
drops derived from flit.required. Properties remain in schema definition
(forward compat with consumers that may still read them).

Task 6 will regenerate the JSON without these fields. Schema relaxed
first to avoid validation failure during transition.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Drop resolved fields from `ni_packet.json` + simplify packet generator

**Goal**: Refactor generator to STOP computing/writing `width`/`lsb`/`msb`/`derived.*`. Regen JSON. Helpers continue working (they computed on-the-fly anyway). Byte-identical gate verifies.

**Files:**
- Modify: `spec_validate/ni_spec/generator.py` (drop derivation paths for packet)
- Regenerate: `spec_validate/generated/ni_packet.json`

- [ ] **Step 1: Inspect current generator derivation paths**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
grep -n "payload_width\|derived\|cumulative\|lsb\|msb" ni_spec/generator.py | head -30
```

Note the line numbers of code that computes/sets `width`, `lsb`, `msb`, `payload_width` (per channel), or builds the `derived` dict.

- [ ] **Step 2: Modify generator to skip width/lsb/msb in packet header_fields output**

In `spec_validate/ni_spec/generator.py`, find the function that emits header_fields entries (likely a list comprehension or loop producing dicts like `{"name", "width_param", "width", "lsb", "msb", "enabled"}`).

Drop the `width`, `lsb`, `msb` keys from the produced dict. Keep `name`, `width_param`, `enabled`.

Example pattern:

```python
# BEFORE
entry = {
    "name": name,
    "width_param": width_param,
    "width": resolved_width,         # ← drop
    "lsb": cumulative_lsb,            # ← drop
    "msb": cumulative_lsb + resolved_width - 1,   # ← drop
    "enabled": enabled,
}

# AFTER
entry = {
    "name": name,
    "width_param": width_param,
    "enabled": enabled,
}
```

Also drop the `cumulative_lsb` tracking variable since it's no longer used to populate output (but the GENERATOR may still need it for validation during parse — leave that part if so).

- [ ] **Step 3: Modify generator to skip width/lsb/msb in payload_channels[].fields[]**

Same pattern: drop `width`, `lsb`, `msb` from per-field entries within each channel. KEEP per-channel `payload_width` (authored metadata).

- [ ] **Step 4: Modify generator to NOT emit the `derived` dict**

Find the code that builds the `derived: {FLIT_WIDTH: ..., HEADER_WIDTH: ..., ...}` dict (likely around `generator.py:329` per cross-review citations). Remove it. The packet spec's top-level should now have `flit.field_widths`, `flit.header_fields`, `flit.payload_channels` only — no `flit.derived`.

- [ ] **Step 5: Regenerate ni_packet.json**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
# Find the regen command in the generator's module docstring or README; typically:
py -3 -m ni_spec ../spec/ni/doc
```

After regen, inspect:

```bash
grep -c '"width":' generated/ni_packet.json     # should be 0 in header_fields/fields area
grep -c '"lsb":' generated/ni_packet.json       # should be 0
grep -c '"msb":' generated/ni_packet.json       # should be 0
grep -c '"derived":' generated/ni_packet.json   # should be 0
grep -c '"width_param":' generated/ni_packet.json   # should match # of fields (unchanged)
grep -c '"payload_width":' generated/ni_packet.json # one per channel (5), unchanged
```

- [ ] **Step 6: CRITICAL — Run drift gate**

```bash
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: **exit 0**. This is THE critical moment. Helpers now have no stored fallback in JSON, must compute everything. If helpers were correct, elaborated `.h`/`.sv` are byte-identical to golden.

If exit 1, the diff shows which output diverged from golden. Most likely culprits:
- A helper that wasn't migrated yet (still reading `f["width"]` or `spec["flit"]["derived"][...]`)
- An expression that evaluates differently (e.g. order-of-operations)

Investigate diff before continuing.

- [ ] **Step 7: Run golden + order + helper tests**

```bash
py -3 -m pytest tests/test_byte_identical_golden.py tests/test_order_invariance.py tests/test_constants_resolver.py -v 2>&1 | tail -15
```

Expected: 32+ tests pass.

- [ ] **Step 8: Full pytest**

```bash
py -3 -m pytest -q 2>&1 | tail -3
```

Expected: ~133 PASS (no regression — Task 4 rewrote tests to not depend on dropped fields).

- [ ] **Step 9: Verify Invariant 8 — order preservation**

```bash
py -3 -m pytest tests/test_order_invariance.py -v 2>&1 | tail -5
```

Expected: PASS. Confirms K-1 tripwire didn't trigger.

- [ ] **Step 10: Commit**

```bash
git add spec_validate/ni_spec/generator.py spec_validate/generated/ni_packet.json
git commit -m "$(cat <<'EOF'
refactor(spec_validate): drop resolved fields from ni_packet.json

Task 6 of pure-parameterization refactor. Generator no longer writes
width/lsb/msb to header_fields[] or payload_channels[].fields[]; no
longer writes the flit.derived dict. JSON is now purely symbolic
(field_widths + width_param + enabled + authored payload_width).

Helpers in ni_spec.constants compute on-the-fly. --check still exit 0
(byte-identical output preserved). All 24 helper unit tests pass.

This is the architectural commit of the refactor — JSON itself is now
the true parameterized source-of-truth.

Refs spec doc Acceptance Criteria A + E.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Signals helpers in `constants.py` + cross-domain support + unit tests

**Goal**: Add signals-domain helpers analogous to packet helpers, but with cross-domain awareness (signals can reference `FLIT_WIDTH` etc. from packet).

**Files:**
- Modify: `spec_validate/ni_spec/constants.py`
- Modify: `spec_validate/tests/test_constants_resolver.py` (append signals tests)

- [ ] **Step 1: Add signals helpers**

Append to `spec_validate/ni_spec/constants.py`:

```python
# ─── Signals namespace + cross-domain resolution ──────────
def _find_interface(signals_spec: dict, interface: str) -> dict:
    for iface in signals_spec.get("interfaces", []):
        if iface["name"] == interface:
            return iface
    raise FieldNotFoundError(f"interface '{interface}' not found")


def signal_param_value(signals_spec: dict, packet_spec: dict, interface: str, name: str) -> int:
    """Resolve a symbol name against signals' cross-domain namespace.
    Lookup order:
      1. interface-local port_parameters[]
      2. packet field_widths{}
      3. packet derived totals (FLIT_WIDTH, HEADER_WIDTH, PAYLOAD_WIDTH, ...)
    """
    iface = _find_interface(signals_spec, interface)
    # 1. interface-local
    for p in iface.get("port_parameters", []):
        if p["name"] == name:
            return int(p["default"])
    # 2. packet field_widths
    pkt_fw = packet_spec.get("flit", {}).get("field_widths", {})
    if name in pkt_fw:
        return int(pkt_fw[name])
    # 3. packet derived totals
    derived_funcs = {
        "FLIT_WIDTH": flit_width,
        "HEADER_WIDTH": header_width,
        "PAYLOAD_WIDTH": payload_width,
        "LINK_WIDTH": link_width,
        "FLIT_DATA_WIDTH": flit_data_width,
        "HEADER_DATA_WIDTH": header_data_width,
        "WSTRB_WIDTH": wstrb_width,
    }
    if name in derived_funcs:
        return derived_funcs[name](packet_spec)
    raise ExprNameError(
        f"symbol '{name}' not found in interface '{interface}' "
        f"port_parameters, packet field_widths, or derived totals"
    )


def signal_eval_expr(signals_spec: dict, packet_spec: dict, interface: str, expr) -> int:
    """ast safe-walk eval against signals' cross-domain namespace."""
    if isinstance(expr, int):
        return expr
    if not isinstance(expr, str):
        raise ExprSyntaxError(f"width_param must be str or int, got {type(expr).__name__}")
    try:
        tree = _ast.parse(expr, mode="eval")
    except SyntaxError as e:
        raise ExprSyntaxError(f"cannot parse '{expr}': {e}") from e
    # Build flat namespace dict combining interface + packet
    iface = _find_interface(signals_spec, interface)
    namespace = {}
    for p in iface.get("port_parameters", []):
        namespace[p["name"]] = int(p["default"])
    namespace.update(packet_spec.get("flit", {}).get("field_widths", {}))
    # Add derived totals lazily — only compute if expression references them
    class _LazyDerived(dict):
        def __getitem__(self, k):
            try:
                return super().__getitem__(k)
            except KeyError:
                derived_funcs = {
                    "FLIT_WIDTH": flit_width, "HEADER_WIDTH": header_width,
                    "PAYLOAD_WIDTH": payload_width, "LINK_WIDTH": link_width,
                    "FLIT_DATA_WIDTH": flit_data_width,
                    "HEADER_DATA_WIDTH": header_data_width,
                    "WSTRB_WIDTH": wstrb_width,
                }
                if k in derived_funcs:
                    v = derived_funcs[k](packet_spec)
                    self[k] = v
                    return v
                raise
        def __contains__(self, k):
            try:
                self[k]
                return True
            except KeyError:
                return False
    return _eval_ast(tree.body, _LazyDerived(namespace))


def signal_width(signals_spec: dict, packet_spec: dict, interface: str, pin_name: str) -> int:
    """Resolve width of a specific pin in an interface."""
    iface = _find_interface(signals_spec, interface)
    for ch in iface.get("channels", []):
        for s in ch.get("signals", []):
            if s.get("pin_name") == pin_name:
                wp = s.get("width_param")
                if wp is None:
                    # No width_param means width=1 (single-bit signal)
                    return 1
                return signal_eval_expr(signals_spec, packet_spec, interface, wp)
    # Signal may also live at iface level (not in channels)
    for s in iface.get("signals", []):
        if s.get("pin_name") == pin_name:
            wp = s.get("width_param")
            if wp is None:
                return 1
            return signal_eval_expr(signals_spec, packet_spec, interface, wp)
    raise FieldNotFoundError(f"pin '{pin_name}' not found in interface '{interface}'")
```

- [ ] **Step 2: Append signals unit tests**

Append to `spec_validate/tests/test_constants_resolver.py`:

```python
# ── signals helpers ──────────────────────────────────────
@pytest.fixture
def signals_spec():
    return load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")


def test_signal_param_interface_local(signals_spec, packet_spec):
    """AXI_ID_WIDTH in AXI_SLAVE_PORT port_parameters."""
    # First confirm port_parameters has AXI_ID_WIDTH for AXI_SLAVE_PORT
    v = C.signal_param_value(signals_spec, packet_spec, "AXI_SLAVE_PORT", "AXI_ID_WIDTH")
    assert v == 8


def test_signal_param_packet_fallback(signals_spec, packet_spec):
    """A param not in interface but in packet field_widths."""
    # X_WIDTH is in packet.field_widths, not signals' port_parameters
    v = C.signal_param_value(signals_spec, packet_spec, "AXI_SLAVE_PORT", "X_WIDTH")
    assert v == 4


def test_signal_param_cross_domain_flit_width(signals_spec, packet_spec):
    """FLIT_WIDTH is a packet-derived total; signal can reference it."""
    v = C.signal_param_value(signals_spec, packet_spec, "NOC_REQ_OUT", "FLIT_WIDTH")
    assert v == C.flit_width(packet_spec)


def test_signal_param_unknown(signals_spec, packet_spec):
    with pytest.raises(ExprNameError):
        C.signal_param_value(signals_spec, packet_spec, "AXI_SLAVE_PORT", "TOTALLY_BOGUS")


def test_signal_eval_expr(signals_spec, packet_spec):
    """Expression eval in signals namespace works."""
    v = C.signal_eval_expr(signals_spec, packet_spec, "AXI_SLAVE_PORT", "AXI_ID_WIDTH + 1")
    assert v == 9


def test_signal_width_basic(signals_spec, packet_spec):
    w = C.signal_width(signals_spec, packet_spec, "AXI_SLAVE_PORT", "axi_awid_i")
    assert w == 8  # AXI_ID_WIDTH


def test_signal_width_flit_width_pin(signals_spec, packet_spec):
    """noc_req_flit_o has width_param = FLIT_WIDTH (cross-domain)."""
    w = C.signal_width(signals_spec, packet_spec, "NOC_REQ_OUT", "noc_req_flit_o")
    assert w == C.flit_width(packet_spec)
```

- [ ] **Step 3: Run new signals tests**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest tests/test_constants_resolver.py::test_signal_param_interface_local \
                 tests/test_constants_resolver.py::test_signal_param_packet_fallback \
                 tests/test_constants_resolver.py::test_signal_param_cross_domain_flit_width \
                 tests/test_constants_resolver.py::test_signal_param_unknown \
                 tests/test_constants_resolver.py::test_signal_eval_expr \
                 tests/test_constants_resolver.py::test_signal_width_basic \
                 tests/test_constants_resolver.py::test_signal_width_flit_width_pin -v 2>&1 | tail -15
```

Expected: 7 PASS.

- [ ] **Step 4: Full pytest + drift gate**

```bash
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: ~140 PASS; exit 0.

- [ ] **Step 5: Commit**

```bash
git add spec_validate/ni_spec/constants.py spec_validate/tests/test_constants_resolver.py
git commit -m "$(cat <<'EOF'
feat(spec_validate): signals helpers + cross-domain resolution + 7 tests

Task 7 of pure-parameterization refactor. Adds:
 - signal_param_value, signal_eval_expr, signal_width helpers
 - Cross-domain namespace: interface port_parameters > packet field_widths >
   packet derived totals (FLIT_WIDTH, HEADER_WIDTH, ...)
 - 7 unit tests covering interface-local, packet-fallback, cross-domain
   FLIT_WIDTH, unknown symbol, expression eval, basic width, FLIT_WIDTH pin

Elaborators not yet wired (Task 8 does that).

Refs spec doc §Components (signals cross-domain rule).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Update `cpp_signals.py` + `sv_signals.py` to use helpers (drop `uint64_t` fallback)

**Goal**: Replace direct `default`/`width_expr` access with signal helper calls. Drop the `cpp_signals.py:29` `uint64_t` fallback (resolve symbolic widths properly).

**Files:**
- Modify: `spec_validate/tools/elaborate/cpp_signals.py`
- Modify: `spec_validate/tools/elaborate/sv_signals.py`

- [ ] **Step 1: Inspect current cpp_signals.py width-resolution logic**

```bash
sed -n '20,40p' spec_validate/tools/elaborate/cpp_signals.py
```

Find the function/section that determines C++ type from width. Likely `_cpp_type_for_width(width_expr)` around line 24-43 (per scope-correction commit history).

- [ ] **Step 2: Modify cpp_signals.py to resolve width via signal helper**

Wherever the elaborator currently reads `s["default"]` or `s["width_expr"]` to determine width, replace with `C.signal_width(signals_spec, packet_spec, interface_name, s["pin_name"])`.

The elaborator needs both `signals_spec` and `packet_spec` — it must load packet spec at start of `emit()`:

```python
# At top of emit(signals_json, spec_version):
from ni_spec.loader import load_doc
packet_spec = load_doc(signals_json.parent / "ni_packet.json")
signals_spec = load_doc(signals_json)
```

Then per signal:

```python
# BEFORE
width_expr = s.get("width_expr") or s.get("default") or "1"
ctype = _cpp_type_for_width(width_expr)

# AFTER
try:
    w = C.signal_width(signals_spec, packet_spec, iface_name, s["pin_name"])
except Exception:
    raise  # don't swallow; spec is wrong if we can't resolve
ctype = _cpp_type_for_resolved_width(w)
```

Where `_cpp_type_for_resolved_width(w: int)` maps an int to `uint8_t`/`uint16_t`/`uint32_t`/`uint64_t`/`std::array<uint8_t, (w+7)//8>` — this is the existing logic in `_cpp_type_for_width` AFTER the width has been resolved to an int.

The `uint64_t` fallback for symbolic widths goes away — symbolic widths now always resolve to an int.

- [ ] **Step 3: Apply same treatment to sv_signals.py**

SV side: the elaborator emits `logic [W-1:0] signal_name;`. Currently reads `default`/`width_expr` as string for the `[W-1:0]` text. Switch to `signal_width(...)` to get an int, then format `[W-1:0]` from the int.

- [ ] **Step 4: Drift gate — output must stay byte-identical**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: exit 0.

If exit 1, diff shows a signal whose old `_cpp_type_for_width(width_expr)` differed from `_cpp_type_for_resolved_width(int)`. Most likely: the old code used `width_expr` literal string and produced different C++ type than the resolved int would. Adjust `_cpp_type_for_resolved_width` to match the OLD behavior exactly — this is byte-identical territory, the goal is no change.

- [ ] **Step 5: Golden + signal tests**

```bash
py -3 -m pytest tests/test_byte_identical_golden.py tests/test_constants_resolver.py -v 2>&1 | tail -15
```

Expected: all PASS.

- [ ] **Step 6: Full pytest**

```bash
py -3 -m pytest -q 2>&1 | tail -3
```

Expected: ~140 PASS.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/tools/elaborate/cpp_signals.py spec_validate/tools/elaborate/sv_signals.py
git commit -m "$(cat <<'EOF'
refactor(spec_validate): signals elaborators use helpers (drop uint64_t fallback)

Task 8 of pure-parameterization refactor. cpp_signals.py and sv_signals.py
now go through ni_spec.constants signal_width helper instead of
reading s["default"] or s["width_expr"] directly.

cpp_signals.py:29 punt-to-uint64_t fallback (Codex X-1 finding) removed.
Symbolic widths now resolve to int via signal_width's cross-domain
namespace before C++ type mapping.

Output byte-identical (--check exit 0).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Rewrite signals tests + relax signals schema + drop `default` from ni_signals.json

**Goal**: Symmetric to Task 4+5+6 for signals. Combined into one task because signals JSON is simpler (no cumulative bit position, just per-signal `default`).

**Files:**
- Modify: `spec_validate/tests/test_signals_schema.py`
- Modify: `spec_validate/generated/ni_signals.schema.json`
- Modify: `spec_validate/ni_spec/generator.py` (drop `default` from signals output)
- Regenerate: `spec_validate/generated/ni_signals.json`

- [ ] **Step 1: Rewrite signals tests to use helpers**

```bash
grep -rn '\["default"\]\|\["width_expr"\]' spec_validate/tests/ 2>&1
```

For each match in signals-related tests, rewrite via `C.signal_width(signals_spec, packet_spec, iface, pin_name)`.

- [ ] **Step 2: Verify signals tests still pass**

```bash
py -3 -m pytest tests/test_signals_schema.py -v 2>&1 | tail -10
```

Expected: all PASS.

- [ ] **Step 3: Relax ni_signals.schema.json — drop `default` from required**

Edit `spec_validate/generated/ni_signals.schema.json`. Find the schema for `interfaces[].channels[].signals[]` and `interfaces[].signals[]` (probably both). Remove `default` from their `required` array if present. KEEP `width_param` and `pin_name` REQUIRED.

Verify schema syntax:

```bash
py -3 -c "
import json, jsonschema
schema = json.load(open('generated/ni_signals.schema.json'))
jsonschema.Draft202012Validator.check_schema(schema)
print('schema valid')"
```

- [ ] **Step 4: Modify generator.py to NOT emit `default` in signals output**

In `spec_validate/ni_spec/generator.py`, find the signals-generation function. Drop the `default` key from per-signal dicts.

Also drop any per-signal `width_expr: null` placeholders (the `width_expr` field was only used as a placeholder — `width_param` is the real symbolic field).

- [ ] **Step 5: Regen ni_signals.json**

```bash
py -3 -m ni_spec ../spec/ni/doc
```

Verify:

```bash
grep -c '"default":' generated/ni_signals.json   # should be 0 in signal entries (may remain on block_enables which is different)
grep -c '"width_expr":' generated/ni_signals.json # should be 0
grep -c '"width_param":' generated/ni_signals.json # unchanged count
```

Note: `block_enables` has `"default": "true"` for bool params — that's a different schema concept (block-level enable flag, not signal width). Distinguish and don't drop those.

- [ ] **Step 6: Drift gate + golden tests**

```bash
py -3 tools/codegen.py --check; echo "check_exit=$?"
py -3 -m pytest tests/test_byte_identical_golden.py -v 2>&1 | tail -10
```

Expected: exit 0; all golden tests pass.

- [ ] **Step 7: Full pytest**

```bash
py -3 -m pytest -q 2>&1 | tail -3
```

Expected: ~140 PASS.

- [ ] **Step 8: Commit**

```bash
git add spec_validate/tests/test_signals_schema.py \
        spec_validate/generated/ni_signals.schema.json \
        spec_validate/ni_spec/generator.py \
        spec_validate/generated/ni_signals.json
git commit -m "$(cat <<'EOF'
refactor(spec_validate): drop default from ni_signals.json (pure symbolic)

Task 9 of pure-parameterization refactor. Signals JSON no longer stores
per-signal `default` (resolved snapshot); also drops `width_expr: null`
placeholder. Schema relaxed; tests rewritten via signal_width helper.

Generator simplified to not compute defaults.

block_enables[].default kept (different concept — block-level bool flag,
not signal width snapshot).

--check exit 0 (byte-identical).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Simplify `invariants.py` — drop stored-vs-computed checks, add expression-eval checks

**Goal**: Validator currently has "width vs eval-of-width_param" cross-check. After Task 6 the stored width is gone — that check becomes vacuous. Replace with declarative checks that helpers eval cleanly.

**Files:**
- Modify: `spec_validate/ni_spec/invariants.py`

- [ ] **Step 1: Find existing flit arithmetic check**

```bash
sed -n '105,150p' spec_validate/ni_spec/invariants.py
```

Identify the section that compares `f["width"]` to `_resolve_width_param(spec, f["width_param"])` (per cross-review).

- [ ] **Step 2: Drop "stored width vs eval" cross-check; replace with "expr can eval"**

In `check_flit_arithmetic` (or equivalent), replace blocks like:

```python
# BEFORE
for f in hdr:
    rv = _resolve_width_param(packet_spec, f.get("width_param"))
    if rv is not None and rv != f["width"]:
        issues.append(_err(C, f"header '{f['name']}': width={f['width']} ..."))
```

With:

```python
# AFTER
from . import constants as _C
for f in hdr:
    try:
        _ = _C.header_field_width(packet_spec, f["name"])
    except _C.SpecResolveError as e:
        issues.append(_err(C, f"header '{f['name']}': cannot resolve width_param: {e}"))
```

Repeat for payload fields, replacing `_resolve_width_param` and stored-comparison with helper-call-with-catch.

- [ ] **Step 3: Drop `derived` checks that referenced stored snapshot**

If there's a block like `if d[name] != computed: issues.append(...)` that compared `flit.derived.X` to a computed value — that block becomes vacuous (`derived` is gone from JSON). Remove it entirely.

- [ ] **Step 4: Add new "tiling consistent" check (via helper)**

The tiling check (header fields don't overlap) used to read stored `lsb`/`msb`. Replace with helper-based version:

```python
def check_flit_tiling(packet_spec):
    issues = []
    cumulative = 0
    for f in packet_spec["flit"]["header_fields"]:
        try:
            w = _C.header_field_width(packet_spec, f["name"])
        except _C.SpecResolveError as e:
            issues.append(_err("L2-FLIT", f"header '{f['name']}': {e}"))
            continue
        # Cumulative position computed; no overlap possible by construction
        cumulative += w
    declared_hw = packet_spec["flit"].get("derived", {}).get("HEADER_WIDTH")
    # `derived` is gone — skip this check entirely or compare to header_width helper
    if cumulative != _C.header_width(packet_spec):
        issues.append(_err("L2-FLIT", f"cumulative header sum {cumulative} != header_width helper {_C.header_width(packet_spec)}"))
    return issues
```

Note: cumulative tracking through declaration order IS the tiling check (since we don't have user-provided lsb/msb to disagree with). Just verify `sum == header_width_helper` for internal consistency.

- [ ] **Step 5: Drop `_resolve_width_param` helper if unused**

If after edits `_resolve_width_param` is no longer called anywhere in `invariants.py`, delete it. Helper logic now lives in `constants.py::packet_eval_expr`.

- [ ] **Step 6: Run full pytest**

```bash
py -3 -m pytest -q 2>&1 | tail -3
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: ~140 PASS (some validator tests may need to update their expected error messages — fix as needed); exit 0.

- [ ] **Step 7: Commit**

```bash
git add spec_validate/ni_spec/invariants.py
git commit -m "$(cat <<'EOF'
refactor(spec_validate): simplify invariants — eval-based checks only

Task 10 of pure-parameterization refactor. Drop "stored width vs eval"
cross-checks (vacuous after Task 6 dropped stored values). Replace with:
 - "expression can eval" check (catches malformed width_param)
 - Tiling via helper (cumulative sum == header_width)

_resolve_width_param helper removed from invariants.py (logic moved to
constants.py::packet_eval_expr earlier).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Fix `NOC_QOS_WIDTH` duplicate localparam (K-2 pre-existing bug)

**Goal**: `ni_flit_pkg.sv` declares `localparam NOC_QOS_WIDTH` at line 23 AND line 80 — duplicate, illegal per SV LRM §6.20. Refactor touches `sv_packet.py`; fix while there.

**Files:**
- Modify: `spec_validate/tools/elaborate/sv_packet.py`
- Regenerate: `spec_validate/rtl_pkg/ni_flit_pkg.sv`
- Update: `spec_validate/tests/golden/ni_flit_pkg.sv.golden` (because elaborated output legitimately changes by 1 line)

- [ ] **Step 1: Find both emit sites of NOC_QOS_WIDTH in sv_packet.py**

```bash
grep -n "NOC_QOS_WIDTH\|noc_qos" spec_validate/tools/elaborate/sv_packet.py
```

There are likely 2 different emission paths producing the same `localparam NOC_QOS_WIDTH` line — one in field_widths emission, one in header_fields _WIDTH emission for `noc_qos` (the width=0 field).

- [ ] **Step 2: Determine which one to keep**

The field_widths section emits ALL param widths including NOC_QOS_WIDTH=0 (line 23 in current output). The header_fields section emits per-field _WIDTH/_LSB/_MSB constants; for noc_qos (width=0) it emits only _WIDTH=0 + _ENABLED (line 80).

Both should remain semantically, but the localparam declaration is duplicated. Decision: **keep the header_fields emission, suppress the field_widths emission for NOC_QOS_WIDTH specifically** — OR — **make the header field emit `// NOC_QOS_WIDTH already declared at line N` comment** — OR — **use SV `// already declared` pragma**.

Cleanest: emit field_widths section as-is, but rename the per-field _WIDTH for noc_qos. But noc_qos has the convention that its `_WIDTH` IS `NOC_QOS_WIDTH` — they're the same name. So one must yield.

**Decision**: In header_fields emission for noc_qos (width=0), skip the `_WIDTH = 0` localparam (already declared as field_widths NOC_QOS_WIDTH). Keep the `_ENABLED = true` localparam.

- [ ] **Step 3: Implement the fix**

In `sv_packet.py`, find the loop emitting header field `_WIDTH` constants for width=0 fields. Add a guard:

```python
# In the header_fields loop, when emitting width=0 field's _WIDTH:
# BEFORE:
out.append(f"  localparam int unsigned {n}_WIDTH = 0;  // reserved placeholder")
# AFTER:
# (skip — NOC_QOS_WIDTH=0 already declared in field_widths section above)
# But still emit _ENABLED:
out.append(f"  localparam bit {n}_ENABLED = 1'b{1 if enabled else 0};")
```

Actually the simpler approach: keep both emissions but check for duplicate name at emit time:

```python
emitted_names = set()
for emit_pass in ["field_widths", "header_fields", ...]:
    ...
    for line in ...:
        # extract `localparam ... NAME` from the line
        name = ...
        if name in emitted_names:
            continue
        emitted_names.add(name)
        out.append(line)
```

Pick whichever is cleaner in the actual code structure. Goal: only ONE `localparam ... NOC_QOS_WIDTH` line in the final output.

- [ ] **Step 4: Regen ni_flit_pkg.sv**

```bash
py -3 tools/codegen.py --target sv --domain packet --out rtl_pkg/
grep -c "localparam.*NOC_QOS_WIDTH" rtl_pkg/ni_flit_pkg.sv
```

Expected: 1 (was 2).

- [ ] **Step 5: Verify --check finds drift (expected)**

```bash
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: **exit 1** — the SV pkg now differs by 1 line from committed version (good — that's the fix). We must update committed version + golden fixture.

- [ ] **Step 6: Update golden fixture**

```bash
cp rtl_pkg/ni_flit_pkg.sv tests/golden/ni_flit_pkg.sv.golden
```

- [ ] **Step 7: Verify --check now exit 0**

```bash
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: exit 0 (committed version matches regenerated; golden matches regenerated).

- [ ] **Step 8: Run golden tests**

```bash
py -3 -m pytest tests/test_byte_identical_golden.py -v 2>&1 | tail -10
```

Expected: all pass (including SV packet).

- [ ] **Step 9: Verify SV file is now valid SV (optional verilator check)**

```bash
which verilator >/dev/null && verilator --lint-only rtl_pkg/ni_flit_pkg.sv || echo "verilator not in PATH, skip"
```

If verilator available: expect 0 errors about duplicate localparams.

- [ ] **Step 10: Commit**

```bash
git add spec_validate/tools/elaborate/sv_packet.py \
        spec_validate/rtl_pkg/ni_flit_pkg.sv \
        spec_validate/tests/golden/ni_flit_pkg.sv.golden
git commit -m "$(cat <<'EOF'
fix(spec_validate): NOC_QOS_WIDTH no longer duplicately declared

K-2 from pure-param cross-review (Task 11). ni_flit_pkg.sv previously
declared `localparam int unsigned NOC_QOS_WIDTH = 0;` twice (line 23
in field_widths section, line 80 in header_fields width=0 fallback).
Illegal per SV LRM §6.20.

Suppress the header_fields emission for width=0 fields where the
_WIDTH localparam is already declared in field_widths. Keep the
_ENABLED bit emission.

Golden fixture updated (legitimate output change).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: Final verification + acceptance criteria check

**Goal**: Audit acceptance criteria one by one. Any miss is fixed in this task.

**Files:** (verification only; fixes go to whichever file needs them)

- [ ] **Step 1: Acceptance A — `--check` exit 0**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 tools/codegen.py --check; echo "check_exit=$?"
```

Expected: exit 0. **Must pass.**

- [ ] **Step 2: Acceptance B — c_model ctest**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/c_model
mkdir -p build && cd build && cmake .. -G "Ninja" >/dev/null 2>&1 && cmake --build . >/dev/null 2>&1
ctest --output-on-failure 2>&1 | tail -5
```

Expected: all c_model tests pass (no consumer impact). Same count as pre-refactor (27).

- [ ] **Step 3: Acceptance C + D — Helper unit tests + all existing tests pass**

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor/spec_validate
py -3 -m pytest -q 2>&1 | tail -3
```

Expected: ~140+ passed (was ~101 baseline + ~24 new helper tests + ~8 golden/order tests + ~7 signals tests = 140).

- [ ] **Step 4: Acceptance E — JSON contains no resolved fields**

```bash
grep -E '"width":|"lsb":|"msb":|"derived":|"default":' generated/ni_packet.json && echo "FAIL packet" || echo "OK packet"
grep -E '"default":' generated/ni_signals.json | grep -v "block_enables" && echo "FAIL signals" || echo "OK signals"
grep -E '"width_expr":' generated/ni_signals.json && echo "FAIL signals width_expr" || echo "OK signals"
```

Expected: all "OK" lines printed. Registers JSON intentionally still has `width_expr` (registers domain skipped).

- [ ] **Step 5: Acceptance F — single NOC_QOS_WIDTH declaration**

```bash
grep -c "localparam.*NOC_QOS_WIDTH" rtl_pkg/ni_flit_pkg.sv
```

Expected: 1.

- [ ] **Step 6: Acceptance G — schema files no longer require resolved fields**

```bash
grep -A 2 '"required"' generated/ni_packet.schema.json | grep -E '"width"|"lsb"|"msb"' && echo "FAIL: schema still requires" || echo "OK"
grep -A 2 '"required"' generated/ni_signals.schema.json | grep '"default"' && echo "FAIL: schema still requires" || echo "OK"
```

Expected: OK for both.

- [ ] **Step 7: Final summary commit**

If everything passes, no code commit needed — Task 12 is verification. If something failed in steps 1-6, fix it and commit, then re-verify.

If a fix was needed:

```bash
git add <files>
git commit -m "fix(spec_validate): final-verification fixes for pure-param refactor"
```

Otherwise, end the plan with a one-line tag commit (optional):

```bash
git tag -a pure-param-complete -m "Pure parameterization refactor complete — 12 tasks landed"
```

---

## End-of-Plan Verification

Run from worktree root:

```bash
cd /e/05_NoC/noc-sim/.worktrees/pure-param-refactor

# pytest
cd spec_validate && py -3 -m pytest -q 2>&1 | tail -3                   # expect 140+ passed

# Drift gate
py -3 tools/codegen.py --check; echo "check_exit=$?"                    # expect 0

# Inventory drift gate
py -3 tools/gen_inventory.py --check; echo "inventory_exit=$?"          # expect 0

# c_model ctest
cd ../c_model/build && ctest --output-on-failure 2>&1 | tail -5         # expect 27/27 passed

# Final purity grep
cd ..
grep -E '"width":|"lsb":|"msb":|"derived":' spec_validate/generated/ni_packet.json || echo "packet pure"
grep -E '"default":|"width_expr":' spec_validate/generated/ni_signals.json | grep -v "block_enables" || echo "signals pure"

# Order invariance still holds
cd spec_validate && py -3 -m pytest tests/test_order_invariance.py -v 2>&1 | tail -5
```

All gates green = refactor complete. JSON is purely symbolic, helpers compute everything, consumers see no contract change.

---

## Self-Review Notes

- **Spec coverage**: Every section of `2026-05-28-pure-parameterization-design.md` is covered:
  - Invariants 1-8 (especially #3 byte-identical and #8 order preservation) → Tasks 1, 3, 8, 11
  - Special field semantics (`derived` literal, payload_width metadata, cross-domain `FLIT_WIDTH`) → Tasks 2, 7
  - Modified files table → matches Tasks 2-11
  - Pre-existing fix (NOC_QOS_WIDTH dup) → Task 11
  - Testing strategy (L0 drift + L1 unit + L1' golden + L2 rewrite) → Tasks 1, 2, 4, 9
  - Acceptance criteria A-G → Task 12 audits each one

- **TDD pattern**: Tasks 2, 7 add tests first (helpers don't exist), confirm fail (or pass against intended behavior), then implement. Tasks 3, 8, 11 use drift gate as the "test" (byte-identical).

- **No placeholders scan**: every step has actual code or actual command, no "implement X" or "add appropriate error handling" stubs.

- **Type consistency**: helper API surface (`header_field_width`, `header_field_position`, `signal_width`, `signal_eval_expr`, etc.) used consistently across Tasks 2-10.

- **Critical sequencing**: Task 5 (relax schema) must precede Task 6 (drop JSON fields) or schema validation fails. Task 6's drift gate is the architectural moment — verify before continuing to Task 7.

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-28-pure-parameterization.md`.

Two execution options:

**1. Subagent-Driven (recommended)** — dispatch a fresh subagent per task, two-stage review between tasks, fast iteration. Requires new worktree from `feat/spec-as-code` (via `using-git-worktrees` skill).

**2. Inline Execution** — `executing-plans` skill, batch execution with checkpoints. Same worktree throughout.

Which approach?
