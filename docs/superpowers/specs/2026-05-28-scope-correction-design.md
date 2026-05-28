# Scope Correction — Codegen Range Narrowed + c_model Bug Fixes

> 2026-05-28 · revised after cross-review (see `cross-review/scope-correction-REVIEW_AGGREGATE.md`).
>
> Supersedes the "D session" framing implied by `c_model/SUFFICIENCY_FINDINGS.md`. Cross-review evidence: full D is not necessary; 2 of 6 findings (F-002, F-006) are c_model bugs, not codegen gaps.

---

## Purpose

Two corrections on top of the c_model bootstrap first round:

1. **Narrow codegen scope** to standard interface + CSR only. Internal unit modes / compile-time params are implementer decisions; pre-elaborating them was premature.
2. **Resolve 4 of the 7 sufficiency findings** that the cross-review showed are real bugs we can close now (F-002, F-004, F-005, F-006). Defer F-001/F-003 to Layer B trigger. F-007 already RESOLVED.

---

## Invariants (revised)

1. **Codegen 規範**:
   - Top-level pin interface (C++ port struct + SV `interface`) — from `spec_validate/generated/ni_signals.json`
   - Over-wire packet format — from `spec_validate/generated/ni_packet.json`
   - CSR registers (offset, field mask, reset value, access mode) — from `spec_validate/generated/ni_registers.json`

   **Codegen 不規範**: internal unit class shape / method signature / mode enum / `compile_time_params`. These are implementer decisions and shall be co-located with the unit's own implementation when that unit is designed.

2. **c_model 不 hardcode 規格值** — 一律 reference codegen elaborated symbol. 違反視同 drift bug.
3. **elaborate**, not "emit", when describing codegen action.
4. **OSS-first** for source / test code.
5. **`ni_function_blocks.json` 保留** as feature inventory (cross-domain consistency check via validator), but no longer drives codegen.

## Phase ordering rule (enforced)

Each phase MUST end with all three gates green before the next phase starts:
- `cd spec_validate && py -3 -m pytest -q` → no regressions
- `cd spec_validate && py -3 tools/codegen.py --check` → exit 0
- `cd c_model/build && ctest --output-on-failure` → no regressions

Rationale: Phase X.1 deletes `ni_blocks.h` while `c_model/include/ni_spec.hpp:8` still `#include`s it. Running X.2/X.3/X.4 before X.1 completes breaks the build.

Phase sequence: X.1 → X.1.5 → X.2 → X.3 → X.4 → X.5.

---

## Phase X.1 — Blocks domain removal

Cleanest path: delete all codegen-side blocks artifacts; keep JSON + validator + cross-ref check.

| Item | Action |
|---|---|
| `spec_validate/tools/elaborate/cpp_blocks.py` | delete |
| `spec_validate/tools/elaborate/sv_blocks.py` | delete |
| `spec_validate/include/ni_blocks.h` | delete (was committed) |
| `spec_validate/rtl_pkg/ni_blocks_pkg.sv` | delete (was committed) |
| `spec_validate/tools/codegen.py` `DOMAIN_TO_EMITTER` map | remove both `("cpp","blocks")` and `("sv","blocks")` entries |
| `spec_validate/tools/codegen.py` `--domain` argparse choices | remove `"blocks"` from the choices list |
| `spec_validate/tools/codegen.py` module docstring (lines 4-12) | remove the two `--domain blocks` example lines |
| `spec_validate/tools/codegen.py --check` mode | drop blocks from the per-domain regen loop |
| `c_model/include/ni_spec.hpp` line 8 | drop `#include "ni_blocks.h"` |
| `spec_validate/ni_spec/invariants.py::check_mode_enum_name_unique` | delete |
| `spec_validate/ni_spec/__main__.py` dispatcher | drop the `check_mode_enum_name_unique` call site |
| `spec_validate/tests/test_function_blocks.py` | drop `test_mode_enum_names_unique_after_block_prefix` and `test_l2_mode_enum_unique_check_fires_on_collision`. Keep schema + cross-ref tests. |
| `spec_validate/tests/test_codegen.py` (lines 87-109, 119-132) | drop blocks-domain elaboration tests + remove `"blocks"` from any `for domain in [...]` loop |
| `spec_validate/tests/test_codegen_sv.py` (lines 248-253, 316-317) | same — drop blocks references |
| `spec_validate/ni_spec/constants.py` (lines 208-231) block accessors | **delete** — they served only the deleted elaborator |
| `spec_validate/ni_function_blocks.json` | **keep** (feature inventory) |
| `spec_validate/ni_function_blocks.schema.json` | **keep** |
| `spec_validate/ni_spec/invariants.py::check_blocks_*` | **keep** (schema + cross-ref validation against packet/registers) |
| `spec_validate/tools/README.md` | update dataflow (4 domains → 3); remove blocks row from per-domain table |

**Phase X.1 gate**: all 3 green AND no `ni::blocks::` reference left in c_model or examples (`grep -r "ni::blocks" c_model/ spec_validate/examples/`).

---

## Phase X.1.5 — Feature inventory generator

After deleting blocks-domain codegen, replace its role with a lighter-weight markdown inventory generator. Goals:
- **Visibility**: NMU / NSU feature lists visible as readable markdown (slide-friendly, no JSON grep needed)
- **Drift gate**: committed inventory MD must stay in sync with `ni_function_blocks.json` (pytest-enforced, mirrors `tools/codegen.py --check` pattern)
- **Convention hint**: each feature row points to its expected c_model header path (informational; not enforced until Layer B starts)

This does NOT generate any C++ / SV code. Class shape remains implementer-decided (Invariant 1).

### Tool: `spec_validate/tools/gen_inventory.py` (new, standalone)

- Reads `spec_validate/ni_function_blocks.json`.
- Writes `c_model/FEATURE_INVENTORY.md` with two tables (NMU + NSU), columns: feature id / summary / modes / expected c_model header path.
- Convention for expected header path: `FEAT-NMU-<X>` → `c_model/include/nmu/<x_lowercase>.hpp`; same for NSU.
- Idempotent: re-running produces byte-identical output (modulo timestamp banner) so drift gate works.

### Pytest gate: `spec_validate/tests/test_feature_inventory.py` (new)

- `test_inventory_md_up_to_date`: regenerate to tempfile, diff vs committed `c_model/FEATURE_INVENTORY.md` (timestamp line excluded). Exit 1 on drift.

The "implementation status per feature" check (header existence) is **NOT enforced** — most features won't have c_model headers until Layer B, and we don't want this round to flag false positives. Inventory MD shows the expected path as informational only.

**Phase X.1.5 gate**: all 3 standard gates green; new `test_inventory_md_up_to_date` passes against the committed `FEATURE_INVENTORY.md`.

---

## Phase X.2 — F-004 + F-005 codegen + c_model consume

### F-004 (per-register reset value)
- `spec_validate/tools/elaborate/cpp_registers.py`: elaborate `constexpr uint32_t <REG>_RESET = N;` per register. **Skip rows with `kind: "reserved"` or `reset_expr: null`** (e.g. the reserved placeholder at offset `0x110`).
- `spec_validate/tools/elaborate/sv_registers.py`: same, `localparam int unsigned <REG>_RESET = N;`.

### F-005 (ALL_OFFSETS array)
- `cpp_registers.py`: elaborate `constexpr uint32_t ALL_OFFSETS[] = { ... };` with N = count of non-reserved registers. Also elaborate `constexpr std::size_t ALL_OFFSETS_COUNT = N;`.
- `sv_registers.py`: elaborate `localparam int unsigned ALL_OFFSETS [N] = '{ ... };` similarly.

### c_model `RegisterFile`
- `reset()`: per offset, write `<REG>_RESET` codegen constant. Drop the `for off : known_offsets() storage_[off] = 0` loop.
- `known_offsets()`: implementation becomes `static const std::unordered_set<uint32_t> kKnown{ni::regs::ALL_OFFSETS, ni::regs::ALL_OFFSETS + ni::regs::ALL_OFFSETS_COUNT};`. Drop the 31-line hand-maintained set at `register_file.cpp:12-43`.

### Tests
- **Delete** `c_model/tests/test_register_file.cpp::ResetValuesAreZeroForNow` (lines 14-19) — it asserts the F-004 bug.
- **Add** a test that asserts `RegisterFile reset; rf.read32(TXN_MIN_LATENCY_OFFSET).data == 0xFFFF` (covers at least one non-zero reset, per `spec_validate/generated/ni_registers.json:136-138`).
- **Add** a test that asserts every codegen-emitted `ALL_OFFSETS[i]` is reported `is_mapped_` true and unknown offsets are not.
- **Add** pytest in `spec_validate/tests/test_registers_parser.py` to verify `ni_regs.h` contains `<REG>_RESET` for non-reserved registers and `ALL_OFFSETS` of correct length.

**Phase X.2 gate**: all 3 green; ctest passes the new `TXN_MIN_LATENCY` non-zero reset test.

---

## Phase X.3 — F-006 access mode redesign + c_model consume

### Codegen (replaces current 30 single-value `enum class <REG>Access { ... }` shapes at `spec_validate/include/ni_regs.h:62-91`)

C++ side (`cpp_registers.py`):

```cpp
namespace ni::regs {
enum class AccessMode { RO, RW, RW1C, WO };   // 4 values, no WC (no current register uses it)
constexpr AccessMode ERR_STATUS_ACCESS              = AccessMode::RW1C;
constexpr AccessMode PKT_PROBE_EN_ACCESS            = AccessMode::RW;
constexpr AccessMode EXCLUSIVE_MONITOR_CTRL_ACCESS  = AccessMode::WO;
// ... per non-reserved register
}
```

SV side (`sv_registers.py`) — **first-time emission** (current `sv_registers.py:48-70` has zero access-mode output, jumps from masks to `endpackage`):

```systemverilog
typedef enum logic [1:0] { ACCESS_RO, ACCESS_RW, ACCESS_RW1C, ACCESS_WO } access_mode_e;
localparam access_mode_e ERR_STATUS_ACCESS = ACCESS_RW1C;
// ... per register
```

### c_model `RegisterFile`

Add a free function in `c_model/src/register_file.cpp`:

```cpp
static ni::regs::AccessMode access_mode_of(uint32_t offset) {
  switch (offset) {
    case ni::regs::ERR_STATUS_OFFSET:             return ni::regs::ERR_STATUS_ACCESS;
    case ni::regs::PKT_PROBE_EN_OFFSET:           return ni::regs::PKT_PROBE_EN_ACCESS;
    case ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET: return ni::regs::EXCLUSIVE_MONITOR_CTRL_ACCESS;
    // ... per register
    default:                                       return ni::regs::AccessMode::RW;  // default for unknown (shouldn't reach if is_mapped_ guards)
  }
}
```

This switch is the only code that mentions per-offset / per-mode pairing — adding a register only requires adding one switch arm; mode values come from codegen.

Update existing helpers:
- `is_wo_(offset)` → `return access_mode_of(offset) == AccessMode::WO;`
- `is_rw1c_(offset)` → `return access_mode_of(offset) == AccessMode::RW1C;`

Update `read32`:
- After `is_mapped_` + alignment checks: if `access_mode_of(offset) == AccessMode::WO`, return `{Ok, 0}` (WO read-as-zero per AXI4-Lite convention).
- Otherwise return `{Ok, storage_[offset]}`.

Update `write32`:
- After `is_mapped_` + alignment + wstrb checks, dispatch on `access_mode_of(offset)`:
  - `RO`: silent ignore, return `{Ok, 0}`. (No DecErr — common AXI4-Lite practice.)
  - `RW1C`: for each bit set in `value`, clear corresponding bit in `storage_[offset]`. Set `last_rw1c_clear_ = true` if any bit was cleared.
  - `WO`: store as-is, return `{Ok, 0}`. (Write succeeds even though read returns 0.)
  - `RW`: store as-is, return `{Ok, 0}`.

### Tests
- **Add** `RegisterFile.WriteOneToRW1CClearsBit` against `ERR_STATUS_OFFSET`.
- **Add** `RegisterFile.WriteToROIsSilentlyIgnored` against any RO register (e.g. `PKT_BYTE_COUNT_OFFSET`).
- **Add** `RegisterFile.ReadFromWOReturnsZero` against `EXCLUSIVE_MONITOR_CTRL_OFFSET` (write succeeds, read returns 0).
- **Add** `RegisterFile.LastWriteTriggeredIrq` exercises the `last_rw1c_clear_` flag.
- Keep existing access tests that pass.

**Phase X.3 gate**: all 3 green; the 4 new access-mode tests pass.

---

## Phase X.4 — F-002 c_model fix + API quarantine

### Codegen (new — supports F-002 closure without F-001 enum)

`cpp_packet.py` elaborate a padding field list **as struct array** (name + lsb + msb, only for `enabled: false` AND `width > 0`):

```cpp
namespace ni::header {
struct PaddingFieldPos { const char* name; int lsb; int msb; };
constexpr PaddingFieldPos PADDING_FIELDS[] = {
  { "route_par",     ROUTE_PAR_LSB,     ROUTE_PAR_MSB     },
  { "flit_ecc",      FLIT_ECC_LSB,      FLIT_ECC_MSB      },
  // (only fields where ENABLED=false AND width > 0;
  //  reserved width=0 placeholders excluded since they have no LSB/MSB)
};
constexpr std::size_t PADDING_FIELDS_COUNT = N;
}
```

(SV side: not required — padding check is c_model side concern; if SV ever needs it, defer.)

### c_model `Flit::check_padding_is_zero`

Iterate `ni::header::PADDING_FIELDS`, for each, verify the bit range in `raw_` is zero. No hand-listed names in c_model.

### API quarantine

Remove `Flit::set_payload_channel` and `Flit::get_payload_channel` from `flit.hpp`. They are public no-op stubs. Re-introduce when F-003 closes during Layer B / Stage 2 work.

### Tests
- **Tighten** `c_model/tests/test_flit.cpp::PaddingFieldStaysZero` (currently tautological at line 42-46): use the existing `Flit(raw_bytes)` constructor (`flit.hpp:17`) to inject a bit at `ni::header::ROUTE_PAR_LSB` (or another `PADDING_FIELDS[i]`), then assert `check_padding_is_zero() == false`.
- **Add** pytest in `spec_validate/tests/test_codegen.py` asserting `PADDING_FIELDS` array exists in `ni_flit_constants.h` and has expected length (2-4 depending on which padding fields have width > 0 — verify via reading `spec_validate/generated/ni_packet.json`).
- **Remove** any c_model test referencing `set_payload_channel` / `get_payload_channel`.

**Phase X.4 gate**: all 3 green; tightened padding test fails before the fix and passes after.

---

## Phase X.5 — `SUFFICIENCY_FINDINGS.md` final disposition

Edit `c_model/SUFFICIENCY_FINDINGS.md` per the table below. End state: 0 PENDING, 4 RESOLVED-this-round, 2 DEFERRED with re-open trigger, 1 already-RESOLVED.

| # | Finding | Status | Notes |
|---|---|---|---|
| F-001 | HeaderField enum | **DEFERRED** | Re-open trigger: Layer B unit starts (any unit consuming Flit header field by name) |
| F-002 | Padding field check | **RESOLVED (Phase X.4)** | Codegen `PADDING_FIELDS[]` + c_model iterates it |
| F-003 | Per-channel payload positions | **DEFERRED** | Re-open trigger: Layer B / Stage 2 payload pack/unpack work begins. Public API `set_payload_channel` / `get_payload_channel` removed (quarantine). |
| F-004 | Per-reg reset value | **RESOLVED (Phase X.2)** | Codegen `<REG>_RESET`; reserved rows skipped |
| F-005 | ALL_OFFSETS array | **RESOLVED (Phase X.2)** | `ni::regs::ALL_OFFSETS[]` + `ALL_OFFSETS_COUNT` |
| F-006 | Per-reg access mode | **RESOLVED (Phase X.3)** | Codegen redesigned to single `enum class AccessMode { RO, RW, RW1C, WO }` + per-reg `<REG>_ACCESS` constexpr; c_model `access_mode_of` switch |
| F-007 | csr_policy dispatch | **RESOLVED (earlier, Task 12 fix `f6e0222`)** | Documented previously |

---

## Process lesson recorded

The first-round Phase 1 sufficiency findings policy did not require the implementer to `grep` codegen output before classifying a gap as "codegen does not elaborate X". F-002 and F-006 were misclassified — the symbols were emitted but the c_model subagent did not look. Future rounds must include a **consume audit**: for each apparent missing symbol, grep the existing elaborated headers first.

Belongs as an addition to the sufficiency findings template, not in this design doc body.

---

## Out of Scope

- **Layer B / Stage 2** — cycle-accurate behavior, NMU/NSU feature units. Re-evaluation when first Layer B unit is designed.
- **F-001 HeaderField enum + F-003 payload positions** — see X.5 disposition.
- **AccessMode `WC`** — dropped from the enum (no current register uses it). Re-add only when a register needs it.

---

## Estimated work

| Phase | Estimate |
|---|---|
| X.1 Blocks removal | 0.5 day |
| X.1.5 Feature inventory generator + drift gate | 0.25 day |
| X.2 F-004 + F-005 codegen + c_model consume | 0.5 day |
| X.3 F-006 redesign + c_model consume + access tests | 0.75 day |
| X.4 F-002 codegen + c_model consume + API quarantine | 0.5 day |
| X.5 SUFFICIENCY_FINDINGS.md disposition edits | 0.1 day |
| **Total** | **~2.75 工程日** |

---

## Next

1. Spec self-review
2. User review
3. Invoke `writing-plans` skill
4. `subagent-driven-development` to execute (same worktree, branch `feat/c-model-bootstrap`)
