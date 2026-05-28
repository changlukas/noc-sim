# Pure Parameterization Refactor

> 2026-05-28 · supersedes "denormalized JSON + validator cross-check" pattern.
>
> **Trigger**: user-observed architectural concern — "JSON 內存 symbolic `width_param` + resolved `width/lsb/msb` 雙份，對自動化不及格"。Cross-review confirmed validator catches drift today, but design is process-automation, not data-model-automation. Refactor closes the gap before Layer B lands.
>
> **Industry validation** (Codex survey 2026-05-28): pattern matches **PeakRDL `Node.get_property()`** API style, **IP-XACT** explicit resolve discipline, **Protocol Buffers** regen-diff CI, **Verilator** byte-identical gate. Design direction externally confirmed against 7 reference tools.

---

## Purpose

Make spec JSON **purely symbolic**: drop pre-resolved values (`width`, `lsb`, `msb`, `derived.*`, `default`). Computation moves to `ni_spec.constants` elaborator helper API. Elaborated `.h`/`.sv` stays **byte-identical**.

**Three goals**:
1. **True data-model parameterization** — JSON is source of structure; values flow from `field_widths` / `port_parameters` namespaces through expression eval
2. **Zero consumer impact** — `c_model/` + `rtl/` see same resolved constants in elaborated headers
3. **Layer B readiness** — when Layer B starts referencing spec values, underlying layer is parameterized

---

## Invariants

1. **JSON 完全 symbolic**: 每 field 只存 `{name, width_param, enabled}` + 必要 metadata；不存 `width / lsb / msb / derived.*`
2. **Elaborator helper 集中在 `ni_spec.constants`**: 所有 expression evaluation + cumulative bit position 邏輯一處。Elaborator 不直接 access JSON dict for values
3. **Elaborated output byte-identical**: `tools/codegen.py --check` 在 refactor 前後皆 exit 0
4. **Expression evaluator 用 ast safe-walk**: 不用 `eval()`；允許 `+ - * // % parens`，禁止 function call / attribute access / subscript / comprehension / lambda
5. **`enabled` flag 不影響 bit position arithmetic**: disabled fields 仍占 bits
6. Describe codegen action with **elaborate** terminology (per project Invariant 3)
7. **OSS-first** for any new dep (stdlib `ast` only)
8. **Order preservation**: Elaborator helper iterates header_fields / channels / params in source declaration order. **No `sorted()` or reordering** — byte-identical depends on this. Tests assert order invariance

---

## Scope

| Domain | Action |
|---|---|
| **Packet** (`ni_packet.json` + schema) | Full refactor — drop `width`/`lsb`/`msb`/`derived.*`/per-channel `payload_width` IF derivable, keep `field_widths` + `width_param` + `enabled` |
| **Signals** (`ni_signals.json` + schema) | Full refactor — drop `default` per signal, drop `cpp_signals.py:29` `uint64_t` fallback (resolve symbolic width properly), keep `width_param` + `port_parameters[]` per interface |
| **Registers** | **SKIP** — literal widths, no parameterization |

---

## Field Semantics — Special Cases (Resolved per Cross-Review)

### `width_param: "derived"` (channel-internal padding fillers)

Some payload fields use literal string `"derived"` as `width_param` (e.g. `aw_rsvd`, `ar_rsvd`, `b_rsvd`, `w_rsvd`, `r_rsvd`). Currently their width is implied by `msb - lsb + 1`. After refactor (where msb/lsb are gone), rule:

> **A field with `width_param: "derived"` has width = `payload_width(channel) - sum(other fields' resolved widths)`**.
>
> The resolver detects literal `"derived"` and short-circuits the eval. Order matters: derived field must be the LAST field in its channel (other fields evaluated first; remainder is derived width).

This formalization preserves current semantic exactly while keeping JSON purely symbolic.

### Per-channel `payload_width` source

The `payload_width` per channel (e.g. `AW: 108`, `W: 352`, `R: 352`) is **authored structural metadata**, not derived from field arithmetic. It is the declared size of that channel's payload portion.

**Decision**: `payload_width` **stays as channel-level metadata in JSON**. It is NOT a derived snapshot; it is part of the source-of-truth declaration of "how wide is each AXI channel's payload". The "derived" rule above uses this value.

```json
"payload_channels": [
  {
    "name": "AW", "payload_width": 108,      ← authored, not derived
    "fields": [ ... { "width_param": "derived" } ]    ← width = 108 - sum(others)
  }
]
```

### Cross-domain `FLIT_WIDTH` reference

`ni_signals.json` has signals with `width_param: "FLIT_WIDTH"` (e.g. `noc_req_flit_o`). FLIT_WIDTH lives in packet domain.

**Decision**: signals elaborator helper accepts a **second namespace** parameter — the loaded packet spec:

```python
signal_eval_expr(signals_spec, packet_spec, interface, expr) -> int
```

The eval namespace is: `port_parameters[interface]` (interface-local) ∪ packet `field_widths{}` ∪ `{FLIT_WIDTH: packet_flit_width(packet_spec), HEADER_WIDTH: packet_header_width(...), PAYLOAD_WIDTH: ...}`. Symbol resolution looks up in this order.

This makes the cross-domain coupling explicit: signals elaborator must load packet spec before resolving.

---

## Architecture

### Pipeline contrast

```
              BEFORE                                  AFTER

  spec/ni/doc/*.md                  ← unchanged →     spec/ni/doc/*.md

  generator.py                                       generator.py
    parse + RESOLVE                                    parse only
    (eval expr, cum lsb, derived)                     (no derivation)

  generated/*.json                                   generated/*.json
    width_param + width                                width_param only
    + lsb + msb + derived.*                            + field_widths preserved
    + payload_width per channel                        + payload_width per channel (kept;
                                                         authored, not derived)

  constants.py                                       constants.py
    thin getter                                        ELABORATOR HELPER
    (read stored val)                                  (eval + cum lsb + derived rule)

  elaborate/*_packet.py                              elaborate/*_packet.py
    read f["width"] direct                             via constants.* API only

  include/*.h, rtl_pkg/*.sv         ← byte-identical (acceptance) →

  c_model + rtl consumer            ← zero touch →
```

### Modified files (spec_validate internal)

| File | Change |
|---|---|
| `ni_spec/generator.py` | Drop derivation paths around lines 206 (payload_width compute), 287 (derived parse), 329 (derived write). Generator only parses MD → raw structure |
| `ni_spec/constants.py` | Lines 23 + 39 etc. — replace stored-value getters with elaborator helper functions (see Components). Add cross-domain support for signals |
| `ni_spec/exceptions.py` (new) | `SpecResolveError` + 4 subclass exceptions |
| `generated/ni_packet.json` | Regen — drop `width`/`lsb`/`msb`/`derived.*`; **keep** per-channel `payload_width` (authored metadata) and `field_widths` |
| `generated/ni_signals.json` | Regen — drop per-signal `default`; keep `width_param` + `port_parameters[]` |
| `generated/ni_packet.schema.json` | Lines 54, 83 — drop `width/lsb/msb` required fields; keep `payload_width` requirement |
| `generated/ni_signals.schema.json` | Drop `default` required field; tighten `width_param` requirement |
| `tools/elaborate/cpp_packet.py` (lines 54, 70, 81) | Replace direct dict access with `constants.*` API calls |
| `tools/elaborate/sv_packet.py` (lines 34, 49, 57) | Same |
| `tools/elaborate/cpp_signals.py` (line 29 — drop `uint64_t` fallback) | Replace with proper symbolic width resolution via elaborator helper |
| `tools/elaborate/sv_signals.py` | Same treatment |
| `ni_spec/invariants.py` | Drop "stored vs computed" cross-check; add "expr can eval" + "tiling consistent via resolver" checks |
| `tests/test_constants_resolver.py` (new) | ~15-20 elaborator-helper unit tests, including edge cases C-1/C-2/C-3 |
| `tests/test_byte_identical_golden.py` (new) | Capture pre-refactor `.h`/`.sv` to fixtures; pytest diffs post-refactor output (excluding timestamp) |
| Other `tests/*.py` | Asserts via elaborator helper API where they directly accessed dict |

### Unchanged

- `generated/ni_registers.json` + schema, `cpp_registers.py`, `sv_registers.py`
- `include/*.h`, `rtl_pkg/*.sv` (byte-identical post-refactor)
- `c_model/`, `rtl/`
- `tools/codegen.py` dispatcher, `tools/gen_inventory.py`

---

## Components: `ni_spec.constants` elaborator helper API

Public functions for elaborator + validator + tests:

```python
# Packet namespace access
packet_param_value(spec, name) -> int
packet_eval_expr(spec, expr) -> int          # ast safe-walk

# Per-header-field
header_field_width(spec, name) -> int        # eval width_param against field_widths
header_field_position(spec, name) -> tuple[int, int] | None
header_field_enabled(spec, name) -> bool

# Per-payload-channel
payload_channel_width(spec, channel) -> int  # returns authored payload_width
payload_field_width(spec, channel, name) -> int
                                             # handles "derived" → channel_width - sum(others)
payload_field_position(spec, channel, name) -> tuple[int, int] | None

# Derived totals (computed on demand)
header_width(spec) -> int                    # sum of header_field widths, ignores `enabled`
payload_width(spec) -> int                   # max of payload_channel widths
flit_width(spec) -> int
link_width(spec) -> int

# Signals (cross-domain aware)
signal_param_value(signals_spec, packet_spec, interface, name) -> int
signal_eval_expr(signals_spec, packet_spec, interface, expr) -> int
signal_width(signals_spec, packet_spec, interface, pin_name) -> int
```

**Cross-domain resolution rule** (signals): the resolver tries (in order) interface-local `port_parameters` → packet `field_widths` → derived totals (`FLIT_WIDTH`, `HEADER_WIDTH`, `PAYLOAD_WIDTH`).

Naming exactly matches the layer being modeled — no abbreviated `get_*` style; functions describe what they return (e.g. `header_field_width` not `get_field_w`). Caching strategy: `functools.lru_cache` keyed on `id(spec)` for `_cumulative_positions` (immutable spec dict assumption).

---

## Data Flow

`constants.header_field_width(spec, "src_id")` return value **unchanged** before/after (both 8). Call site / return type / value preserved → elaborator output preserved → `.h`/`.sv` byte-identical.

Only the **internal computation path** changes: previously `return spec["..."]["width"]`, now `return packet_eval_expr(spec, f["width_param"])`.

---

## Error Handling

### Exception hierarchy

```
SpecResolveError
├── ExprSyntaxError        # ast.parse failure
├── ExprNameError          # symbol not in any namespace (incl. cross-domain)
├── ExprNotAllowedError    # forbidden ast node (function call, attribute, ...)
└── FieldNotFoundError     # unknown field name
```

### Per-layer handling

| Layer | Failure mode | Behavior |
|---|---|---|
| Elaborator helper | width_param eval fail | Raise specific exception with field name + expression text |
| Validator | helper raises | Catch, convert to `Issue("ERROR", "L2-FLIT-EXPR", msg)` |
| Elaborator (codegen) | helper raises | Bubble up, codegen.py prints traceback + exit 1 |
| Drift gate (`--check`) | output differs from committed | Exit 1 with diff |

### Expression evaluator safety

**Allowed**: numeric literal, `+`, `-`, `*`, `//`, `%`, unary `+`/`-`, name reference, parens, **literal string `"derived"` short-circuit** (special case).

**Forbidden**: function call, attribute access, subscript, comprehension, lambda, conditional expr, float division.

Current spec has only `+` expressions + the `"derived"` literal. Whitelist covers 100%.

---

## Pre-Existing Bugs to Fix Opportunistically

| # | Bug | File:line | Fix |
|---|---|---|---|
| B-1 | Duplicate `localparam NOC_QOS_WIDTH` declaration (SV LRM §6.20 violation) | `rtl_pkg/ni_flit_pkg.sv:23` AND `:80` | After refactor, only one declaration should remain. Trace to elaborator (`sv_packet.py`) and fix root cause |

Not strictly in refactor scope, but the refactor touches `sv_packet.py`'s emit logic; fixing here avoids carrying the bug through byte-identical gate (which would otherwise preserve the duplicate).

---

## Testing Strategy

### 3-layer test discipline

| Layer | What | Where |
|---|---|---|
| **L0 Drift gate** | `tools/codegen.py --check` exit 0 (byte-identical) | `tools/codegen.py` (existing) |
| **L1 Helper unit tests** | Expression eval correctness + error cases + cumulative position + edge cases (`"derived"`, width=0, disabled fields, cross-domain `FLIT_WIDTH`) | `tests/test_constants_resolver.py` (new, ~15-20 cases) |
| **L1' Byte-identical golden test** | Fixtures = pre-refactor `.h`/`.sv` snapshots. Pytest: regen + diff (excluding timestamp) → must be empty | `tests/test_byte_identical_golden.py` (new, 6 fixtures: 3 .h + 3 .sv for non-blocks domains) |
| **L2 Existing test rewrite** | Asserts via helper API instead of dict access | `tests/test_codegen.py`, `test_codegen_sv.py`, `test_signals_schema.py` |

### Required edge case unit tests (L1)

1. **`width_param: "derived"`** — verify `payload_field_width(spec, "AW", "aw_rsvd")` returns `108 - sum(other AW fields)`
2. **Zero-width field** — `header_field_position(spec, "noc_qos")` returns `None` (NOC_QOS_WIDTH=0)
3. **Disabled padding field** — `header_field_position(spec, "route_par")` returns valid tuple (occupies bits despite enabled=false)
4. **Cross-domain** — `signal_eval_expr(signals_spec, packet_spec, "NOC_REQ_OUT", "FLIT_WIDTH")` resolves to packet's `flit_width(packet_spec)`
5. **Expression with `+`** — `packet_eval_expr(spec, "X_WIDTH + Y_WIDTH")` = 8
6. **Forbidden syntax** — `packet_eval_expr(spec, "max(X_WIDTH, Y_WIDTH)")` raises `ExprNotAllowedError`
7. **Unknown symbol** — `packet_eval_expr(spec, "MISSING_WIDTH")` raises `ExprNameError`

### Per-task gate during refactor

Every task ends green on:
1. `cd spec_validate && py -3 -m pytest -q` → 0 failed
2. `cd spec_validate && py -3 tools/codegen.py --check` → exit 0 (byte-identical)
3. `cd c_model/build && ctest` → 0 failed (consumer untouched)

### Pytest count expectation

- Before: ~101 passed
- After: ~120-125 passed (delete ~3 "stored vs computed" tests, add ~15-20 helper unit tests + 6 golden tests)

---

## Acceptance Criteria

A. **`tools/codegen.py --check` exit 0** → byte-identical
B. **`c_model/build && ctest`** all pass → consumer contract intact
C. **Helper unit tests pass** → behavior verified independently
D. **All existing tests pass** (rewritten where needed)
E. **JSON `grep -E "\"width\":|\"lsb\":|\"msb\":|\"derived\""` returns no hits** in packet/signals (registers unaffected)
F. **`sv_packet.py` emits `NOC_QOS_WIDTH` exactly once** in `ni_flit_pkg.sv` (B-1 fixed)
G. **Schema files no longer require resolved fields**

---

## Out of Scope

- **Registers domain** — literal widths, skip
- **Elaborated output structure** — `.h`/`.sv` keep resolved-constant shape
- **C++/SV consumer changes** — `c_model/` and `rtl/` zero touch
- **`tools/codegen.py` dispatcher, `tools/gen_inventory.py`** — unaffected
- **Power operator / function calls in expressions** — defer until needed

---

## Process Discipline

- Cross-review on this design doc completed (Codex GPT-5.5 + independent Claude subagent); 11 edits applied (this version)
- Each implementation task ends green on 3 gates before next starts
- Bug log NOT in this design doc — none expected since refactor preserves contract
- Order preservation Invariant #8 is the hidden tripwire; **first implementation task must add an order-invariance test before refactoring anything**

---

## Next Steps

1. ~~Spec self-review~~ ✓
2. ~~Cross-review (Codex + Claude subagent)~~ ✓ — see `cross-review/pure-param-REVIEW_AGGREGATE.md`
3. User review of revised spec
4. Invoke `writing-plans` skill
5. `subagent-driven-development` to execute (new worktree from `feat/spec-as-code`)
