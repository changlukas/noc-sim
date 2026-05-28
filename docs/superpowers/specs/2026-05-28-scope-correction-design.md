# Scope Correction — Codegen Range Narrowed + c_model Bug Fixes

> 2026-05-28 · supersedes the "D session" framing implied by `c_model/SUFFICIENCY_FINDINGS.md`.
>
> **Cross-review evidence**: `cross-review/REVIEW_AGGREGATE.md` (2026-05-28). Codex + independent Claude reviewer agreed: full D is not necessary; 2 of 6 findings (F-002, F-006) are c_model bugs, not codegen gaps.

---

## Purpose

Two corrections on top of the c_model bootstrap first round:

1. **Narrow codegen scope** to standard interface + CSR only. Internal unit modes / compile-time params are implementer decisions; pre-elaborating them was premature.
2. **Resolve 4 of the 7 sufficiency findings** that the cross-review showed are real bugs we can close now without further design discussion. Defer 2 to Layer B trigger. (F-007 is already RESOLVED.)

---

## Invariants

1. **Codegen 規範**（**revised** — narrower than first round）:
   - Top-level pin interface (C++ port struct + SV `interface`) — from `ni_signals.json`
   - Over-wire packet format (header bit positions, payload widths, axi_ch encoding) — from `ni_packet.json`
   - CSR registers (offset, field mask, reset value, access mode) — from `ni_registers.json`

   **Codegen 不規範**: internal unit class shape / method signature / mode enum / `compile_time_params`. These are implementer decisions and shall be co-located with the unit's own implementation when that unit is designed.

2. **c_model 不 hardcode 規格值** — 一律 reference codegen elaborated symbol. 違反視同 drift bug.
3. **elaborate**, not "emit", when describing codegen action.
4. **OSS-first** for source / test code.
5. **`ni_function_blocks.json` 保留** as feature inventory (cross-domain consistency check via validator), but no longer drives codegen.

---

## Phase X.1 — Blocks domain removal

Cleanest path: delete codegen-side artifacts; keep JSON + validator.

| Item | Action |
|---|---|
| `spec_validate/tools/elaborate/cpp_blocks.py` | delete |
| `spec_validate/tools/elaborate/sv_blocks.py` | delete |
| `spec_validate/include/ni_blocks.h` | delete |
| `spec_validate/rtl_pkg/ni_blocks_pkg.sv` | delete |
| `spec_validate/tools/codegen.py` `DOMAIN_TO_EMITTER` | remove `blocks` entries (cpp + sv) |
| `spec_validate/tools/codegen.py --check` | remove blocks from drift check |
| `c_model/include/ni_spec.hpp` | drop `#include "ni_blocks.h"` |
| `spec_validate/ni_spec/invariants.py` `check_mode_enum_name_unique` | delete (no more mode enums to clash) |
| `spec_validate/ni_spec/__main__.py` dispatcher | drop the `check_mode_enum_name_unique` call |
| `spec_validate/tests/test_function_blocks.py` | drop the 2 mode-enum-unique tests; keep schema + cross-ref tests |
| `spec_validate/ni_function_blocks.json` | **keep** (feature inventory) |
| `spec_validate/ni_function_blocks.schema.json` | **keep** |
| Validator `check_blocks_*` in `invariants.py` | **keep** (schema + cross-ref) |
| `spec_validate/tools/README.md` | update dataflow diagram (4 domains → 3) |

---

## Phase X.2 — F-004 + F-005 codegen (register domain)

| Finding | Action |
|---|---|
| F-004 per-reg reset value | `cpp_registers.py` elaborate `constexpr uint32_t <REG>_RESET = N;` per register; SV side same |
| F-005 ALL_OFFSETS array | `cpp_registers.py` elaborate `constexpr uint32_t ALL_OFFSETS[] = { ... };`; SV side `localparam int unsigned ALL_OFFSETS[N] = '{ ... };` |
| c_model `RegisterFile::reset` | use codegen `<REG>_RESET` per offset; drop the all-zero workaround |
| c_model `RegisterFile::known_offsets_` | use codegen `ALL_OFFSETS[]`; drop the hand-maintained 31-entry list |

---

## Phase X.3 — F-006 access-mode redesign

Current codegen emits 31 single-value `enum class <REG>Access { RW1C };` shapes — unusable from a switch / dispatch.

Replace with:

```cpp
namespace ni::regs {
enum class AccessMode { RO, RW, RW1C, WO, WC };
constexpr AccessMode ERR_STATUS_ACCESS    = AccessMode::RW1C;
constexpr AccessMode PKT_PROBE_EN_ACCESS  = AccessMode::RW;
constexpr AccessMode EXCLUSIVE_MONITOR_CTRL_ACCESS = AccessMode::WO;
// ... per register
}
```

SV side similarly: `typedef enum logic [2:0] { ACCESS_RO, ACCESS_RW, ACCESS_RW1C, ACCESS_WO, ACCESS_WC } access_mode_e;` + `localparam access_mode_e <REG>_ACCESS = ACCESS_<MODE>;`.

c_model `RegisterFile`:
- `is_wo_(offset)` → returns `access_mode_of(offset) == AccessMode::WO`
- `is_rw1c_(offset)` → returns `access_mode_of(offset) == AccessMode::RW1C`
- `write32` honors RW1C semantics (write 1 clears the bit) and WO semantics (write succeeds, but read returns 0)
- New GTest cases cover RW1C clear + WO read-as-zero against ERR_STATUS / EXCLUSIVE_MONITOR_CTRL

---

## Phase X.4 — F-002 c_model fix + API quarantine

| Item | Action |
|---|---|
| F-002 `Flit::check_padding_is_zero` | Replace stub with real check. Codegen `<FIELD>_ENABLED` already exists at `spec_validate/include/ni_flit_constants.h:25-69`. The 4 fields with `_ENABLED = false` (`ROUTE_PAR`, `RSVD_COMMTYPE`, `MULTICAST`, `FLIT_ECC`) are the padding set; for each, verify the field's bit range in `raw_` is zero. Hand-list of these 4 names is acceptable for now; full table-driven iteration blocked on F-001 (deferred). |
| `Flit::set_payload_channel` / `get_payload_channel` | Remove from public API. They are no-op stubs that promise behavior they cannot deliver. Re-introduce as part of F-003 closure when Layer B / Stage 2 needs them. |
| `c_model/tests/test_flit.cpp` | Tighten `PaddingFieldStaysZero` to drive a non-zero value into a padding bit range and assert `check_padding_is_zero()` returns false. Without this the test is tautological. |

---

## Phase X.5 — `SUFFICIENCY_FINDINGS.md` final disposition

| # | Finding | Final state after this round |
|---|---|---|
| F-001 | HeaderField enum | **DEFERRED** — re-open trigger: Layer B unit starts (any unit that consumes Flit header field by name) |
| F-002 | Padding field check | **RESOLVED** in Phase X.4 (c_model consumes existing `_ENABLED`; full enumeration blocked on F-001) |
| F-003 | Per-channel payload positions | **DEFERRED** — re-open trigger: Layer B / Stage 2 begins payload pack/unpack work |
| F-004 | Per-reg reset value | **RESOLVED** in Phase X.2 |
| F-005 | ALL_OFFSETS array | **RESOLVED** in Phase X.2 |
| F-006 | Per-reg access mode | **RESOLVED** in Phase X.3 (codegen redesigned + c_model consumes) |
| F-007 | csr_policy dispatch | **RESOLVED** already (Task 12 fix `f6e0222`) |

End state: 0 PENDING findings.

---

## Process lesson recorded

The first-round Phase 1 sufficiency findings policy did not require the implementer to `grep` codegen output before classifying a gap as "codegen does not elaborate X". F-002 and F-006 were misclassified — the symbols were emitted but the c_model subagent did not look. Future c_model rounds must include a "consume audit": for each apparent missing symbol, grep the existing elaborated headers first.

This belongs as an addition to a sufficiency findings template, not in this design doc body.

---

## Out of Scope

- **Layer B / Stage 2** — cycle-accurate behavior, NMU/NSU feature units. Re-evaluation when first Layer B unit is designed.
- **F-001 HeaderField enum + F-003 payload positions** — see X.5 disposition.
- **AccessMode `WC` semantics** — present in the enum but no current register uses it; do not implement write-clear behavior in `RegisterFile::write32` until a register needs it.

---

## Estimated work

| Phase | Estimate |
|---|---|
| X.1 Blocks removal | 0.5 day |
| X.2 F-004 + F-005 codegen + c_model consume | 0.5 day |
| X.3 F-006 redesign + c_model consume + tests | 0.5 day |
| X.4 F-002 fix + API quarantine + test tighten | 0.5 day |
| **Total** | **~2 工程日** |

---

## Next

1. Spec self-review
2. User review of this spec
3. Invoke `writing-plans` skill → implementation plan
4. `subagent-driven-development` to execute (same worktree, branch `feat/c-model-bootstrap`)
