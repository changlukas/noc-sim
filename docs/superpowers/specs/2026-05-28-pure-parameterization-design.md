# Pure Parameterization Refactor

> 2026-05-28 · supersedes the "denormalized JSON + validator cross-check" pattern for spec parameterization.
>
> **Trigger**: user-observed architectural concern — "現在 JSON 內存 symbolic `width_param` + resolved `width/lsb/msb` 雙份，這對自動化來說不及格"。Cross-review verified validator catches drift today, but the design is process-automation, not data-model-automation. Refactor closes the gap before Layer B (cycle-accurate behavior) lands, when retrofit cost will be much higher.

---

## Purpose

Make spec JSON **purely symbolic**: drop all pre-resolved values (`width`, `lsb`, `msb`, `derived.*`, `default`). Computation moves to `ni_spec.constants` resolver API. Elaborator + downstream consumers see no contract change — elaborated `.h`/`.sv` output stays **byte-identical**.

**Three goals**:
1. **True data-model parameterization** — JSON itself is the source of structure; values flow from `field_widths` / `port_parameters` namespaces through expression evaluation
2. **Zero consumer impact** — `c_model/` + `rtl/` see same resolved constants in elaborated headers
3. **Layer B readiness** — when Layer B (NMU/NSU feature units) starts referencing spec values, the underlying spec layer is properly parameterized

---

## Invariants

1. **JSON 完全 symbolic**: 每個 field 只存 `{name, width_param, enabled}` + 必要 metadata；不存 `width / lsb / msb / derived.*` 等可從 `field_widths` 推得的值
2. **Resolver 集中在 `ni_spec.constants`**: 所有 expression evaluation + cumulative bit position 計算邏輯只在一處。Elaborator 不直接 access JSON dict for values
3. **Elaborated output byte-identical**: `tools/codegen.py --check` 在 refactor 前後皆 exit 0。Acceptance criterion 主軸
4. **Expression evaluator 用 ast safe-walk**: 不用 `eval()`；允許 `+ - * // % parens`，禁止 function call / attribute access / subscript / comprehension / lambda
5. **`enabled` flag 不影響 bit position arithmetic**: disabled fields 仍占 bits，後續 field 的 cumulative lsb 不受影響
6. Describe codegen action with **elaborate** terminology
7. **OSS-first** for any new dep (expression parser likely uses stdlib `ast`, no external lib needed)

---

## Scope

| Domain | Action |
|---|---|
| **Packet** (`ni_packet.json`) | Full refactor — drop `width`/`lsb`/`msb`/`derived.*`, keep `field_widths` + `width_param` + `enabled` |
| **Signals** (`ni_signals.json`) | Full refactor — drop `default` per signal, keep `width_param` + `port_parameters[]` per interface |
| **Registers** (`ni_registers.json`) | **SKIP** — `width_expr: "32"` are literals, no real parameterization to extract |

---

## Architecture

### Pipeline contrast

```
              BEFORE                                  AFTER

  ┌─────────────────────────┐         ┌─────────────────────────┐
  │ spec/ni/doc/*.md        │         │ spec/ni/doc/*.md        │  (no change)
  └────────────┬────────────┘         └────────────┬────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ generator.py            │         │ generator.py            │
  │  + RESOLVE              │  ──→    │  parse only             │
  │  (eval expr, cum lsb)   │         │  (no resolve)           │
  └────────────┬────────────┘         └────────────┬────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ generated/*.json        │         │ generated/*.json        │
  │  width_param            │         │  width_param            │
  │  + width + lsb + msb    │  ──→    │  (resolved fields gone) │
  │  + derived.*            │         │  + field_widths kept    │
  └────────────┬────────────┘         └────────────┬────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ constants.py            │         │ constants.py            │
  │  thin getter            │  ──→    │  RESOLVER               │
  │  (read stored val)      │         │  (eval + cum lsb)       │
  └────────────┬────────────┘         └────────────┬────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ elaborate/*_packet.py   │         │ elaborate/*_packet.py   │
  │  read f["width"]        │  ──→    │  C.header_field_width(  │
  │  emit string            │         │     spec, name)         │
  └────────────┬────────────┘         └────────────┬────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ include/*.h, rtl_pkg/   │  ───    │ include/*.h, rtl_pkg/   │
  │  resolved constants     │ ══════  │  resolved constants     │  ← byte-identical
  └─────────────────────────┘         └─────────────────────────┘
               │                                   │
  ┌────────────▼────────────┐         ┌────────────▼────────────┐
  │ c_model + rtl consumer  │         │ c_model + rtl consumer  │  ← zero touch
  └─────────────────────────┘         └─────────────────────────┘
```

### Modified files (spec_validate internal)

| File | Change |
|---|---|
| `ni_spec/generator.py` | Drop pre-resolve helpers; only parse MD → raw structure |
| `ni_spec/constants.py` | Add resolver API (see Components) |
| `ni_spec/exceptions.py` (new) | `SpecResolveError` + 4 subclass exceptions |
| `generated/ni_packet.json` | Regen — symbolic only |
| `generated/ni_signals.json` | Regen — symbolic only |
| `generated/ni_packet.schema.json` | Drop resolved field requirements |
| `generated/ni_signals.schema.json` | Same |
| `tools/elaborate/cpp_packet.py` + `sv_packet.py` | Access via `constants.*` API |
| `tools/elaborate/cpp_signals.py` + `sv_signals.py` | Same |
| `ni_spec/invariants.py` | Simplify — drop "stored vs computed" checks; add "expr can eval" + "tiling consistent" |
| `tests/test_constants_resolver.py` (new) | ~15-20 unit tests for resolver |
| Other `tests/*.py` | Asserts via resolver API where they accessed dict directly |

### Unchanged

- `generated/ni_registers.json`, `cpp_registers.py`, `sv_registers.py` — registers domain skipped
- `include/*.h`, `rtl_pkg/*.sv` — byte-identical post-refactor (acceptance criterion)
- `c_model/` + `rtl/` — all consumer code
- `tools/codegen.py` dispatcher, `tools/gen_inventory.py`
- `tools/codegen.py --check` mode (used as drift gate)

---

## Components: `ni_spec.constants` resolver API

Public functions exposed for elaborator + validator + tests:

```python
# Param namespace access
packet_param_value(spec, name) -> int
packet_eval_expr(spec, expr) -> int     # ast safe-walk evaluator

signal_param_value(spec, interface, name) -> int
signal_eval_expr(spec, interface, expr) -> int

# Per-field resolvers
header_field_width(spec, name) -> int
header_field_position(spec, name) -> tuple[int, int] | None   # None if width=0
header_field_enabled(spec, name) -> bool

payload_field_width(spec, channel, name) -> int
payload_field_position(spec, channel, name) -> tuple[int, int] | None

# Derived totals
header_width(spec) -> int       # sum of all header fields, ignores `enabled`
payload_width(spec) -> int      # max of all payload channel widths
flit_width(spec) -> int
link_width(spec) -> int

# Signals
signal_width(spec, interface, pin_name) -> int
signals_all_pins(spec) -> list[dict]
```

Naming conventions, full signatures, caching strategy, expression parser implementation — left to the implementer per Section 2 brainstorming agreement.

---

## Data Flow

**Refactor 前**: generator 算好所有 derived → JSON 存全部 → elaborator 直接讀

**Refactor 後**: generator 只 parse → JSON 只存 symbolic → constants resolver eval-on-read → elaborator 透過 resolver 取值

`constants.header_field_width(spec, "src_id")` 的 return value 在 refactor 前後 **完全一致**（都是 8）。Call site 不變 → elaborator 邏輯不變 → emitted string 不變 → `.h`/`.sv` byte-identical.

---

## Error Handling

### Exception hierarchy

```
SpecResolveError
├── ExprSyntaxError        # ast.parse failure
├── ExprNameError          # symbol not in namespace
├── ExprNotAllowedError    # forbidden syntax (function call, attribute, ...)
└── FieldNotFoundError     # unknown field name
```

### Per-layer handling

| Layer | Failure mode | Behavior |
|---|---|---|
| Resolver | width_param eval fail | Raise specific exception with field name + expression text |
| Validator | resolver raises | Catch, convert to `Issue("ERROR", "L2-FLIT-EXPR", msg)` |
| Elaborator | resolver raises | Bubble up, codegen.py prints traceback + exit 1 |
| Drift gate (`--check`) | elaborated output differs from committed | Exit 1 with diff |

### Expression evaluator safety

Allowed: numeric literal, `+`, `-`, `*`, `//` (整除), `%`, unary `+`/`-`, name reference, parens.

Forbidden: function call, attribute access, subscript, comprehension, lambda, conditional expr, float division.

Current spec MD has only `+` expressions in `width_param` (grep verified) — whitelist covers 100%.

---

## Testing Strategy

### 3-layer test discipline

| Layer | What | Where |
|---|---|---|
| **L0 Drift gate** | `tools/codegen.py --check` exit 0 (byte-identical elaborated output) | `tools/codegen.py` (existing) |
| **L1 Resolver unit tests** | Expression eval correctness + error cases + cumulative position + edge cases (width=0, disabled fields) | `tests/test_constants_resolver.py` (new, ~15-20 cases) |
| **L2 Existing test rewrite** | Asserts via resolver API instead of dict access | `tests/test_codegen.py`, `test_codegen_sv.py`, `test_signals_schema.py` |

### Per-task gate during refactor

Every implementation task ends green on:
1. `cd spec_validate && py -3 -m pytest -q` → 0 failed
2. `cd spec_validate && py -3 tools/codegen.py --check` → exit 0 (byte-identical)
3. `cd c_model/build && ctest` → 0 failed (consumer untouched)

### Pytest count expectation

- Before: ~101 passed
- After: ~115-120 passed (delete ~3 "stored vs computed" tests, add ~15-20 resolver unit tests)

---

## Acceptance Criteria

A. **`tools/codegen.py --check` exit 0** after refactor → elaborated `.h`/`.sv` byte-identical
B. **`c_model/build && ctest`** all pass → consumer contract intact
C. **Resolver unit tests pass** → resolver behavior verified independently
D. **All existing tests pass** (rewritten where needed) → no regression
E. **Generated JSON contains no `width`/`lsb`/`msb`/`derived.*`** → grep clean

---

## Out of Scope

- **Registers domain** — `width_expr: "32"` literals, no parameterization to extract
- **Elaborated output structure** — `.h`/`.sv` keep current resolved constant shape (no parameterized C++/SV)
- **C++/SV consumer changes** — `c_model/` and `rtl/` zero touch
- **Function blocks JSON** — `uses_packet_fields[]` reference by name only, unaffected
- **Power operator / function calls in expressions** — defer until a real `width_param` needs them

---

## Process Discipline

Per session's accumulated learnings:
- Cross-review on this design doc before writing-plans (Codex GPT-5.5 + independent Claude subagent)
- Each implementation task ends green on 3 gates before next starts
- Bug log NOT in this design doc — implementing-time findings go to `c_model/SUFFICIENCY_FINDINGS.md` (none expected here since refactor preserves contract)

---

## Next Steps

1. Spec self-review
2. Codex industry-pattern survey + design review (running; will append findings as appendix)
3. User review of final spec
4. Invoke `writing-plans` skill
5. `subagent-driven-development` to execute (new worktree from `feat/spec-as-code`)
