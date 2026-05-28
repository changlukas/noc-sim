### 1. Ambiguity

- “CSR registers … access mode” is underspecified: Phase X.3 defines `RO/RW/RW1C/WO/WC`, but only says how `RW1C` and `WO` behave. It never states whether `write32` to `RO` should ignore, error, or write storage; current code writes every mapped register unconditionally at `c_model/src/register_file.cpp:117`.
- “SV side same” for reset values is not enough. Current SV register elaborator only emits offsets and field masks, then ends the package at `spec_validate/tools/elaborate/sv_registers.py:40-69`; the spec must name exact reset symbol shape and tests.
- “use codegen `ALL_OFFSETS[]`” does not say whether `known_offsets()` should remain an `unordered_set`, become a linear scan, or use a generated lookup helper. Current `known_offsets()` is a hand-built set at `c_model/src/register_file.cpp:11-45`.
- “`access_mode_of(offset)`” is used but never specified: location, visibility, behavior for unmapped/reserved offsets, and whether it is generated or handwritten are all missing at `docs/superpowers/specs/2026-05-28-scope-correction-design.md:87-88`.
- X.1 deletes blocks codegen but keeps `ni_function_blocks.json`; it does not say whether block accessors in `constants.py` stay. They currently expose modes and compile-time params at `spec_validate/ni_spec/constants.py:208-231`.
- “Remove from public API” for `Flit::set_payload_channel` / `get_payload_channel` does not state whether this is an API break requiring test updates only, or also downstream compile-fix search. The methods are currently public at `c_model/include/flit.hpp:22-23`.

### 2. Completeness

| Gap | Why It Matters | Evidence |
|---|---|---|
| Blocks test cleanup is incomplete | X.1 only names `test_function_blocks.py`, but codegen tests directly require blocks generation. | `spec_validate/tests/test_codegen.py:87-109`, `spec_validate/tests/test_codegen.py:119-132`, `spec_validate/tests/test_codegen_sv.py:248-253`, `spec_validate/tests/test_codegen_sv.py:316-317` |
| CLI/domain cleanup is incomplete | Removing `DOMAIN_TO_EMITTER` blocks entries is not enough; `--domain` choices and usage text still expose blocks. | `spec_validate/tools/codegen.py:4-12`, `spec_validate/tools/codegen.py:40-48`, `spec_validate/tools/codegen.py:253` |
| Generated-source path is not explicit | Spec says `ni_registers.json`; actual codegen source is `spec_validate/generated/ni_registers.json`. A fresh implementer may edit/read the wrong path. | `spec_validate/tools/codegen.py:43`, `spec_validate/include/ni_regs.h:3` |
| Reset codegen lacks contract | No exact handling for reserved rows, parse failures, integer type, or one non-zero reset case. | reserved row has `reset_expr: null` at `spec_validate/generated/ni_registers.json:230-232`; non-zero reset exists at `spec_validate/generated/ni_registers.json:136-138` |
| Access semantics incomplete | RO and WC behavior are unresolved; WC is declared but explicitly out of scope. That leaves an enum value with no contract. | enum requested at `docs/superpowers/specs/2026-05-28-scope-correction-design.md:76`; WC deferred at `docs/superpowers/specs/2026-05-28-scope-correction-design.md:132` |
| Phase ordering not enforced | X.1 deletes artifacts that current tests and includes require; spec does not define “phase done” gates before moving to X.2. | `c_model/include/ni_spec.hpp:8`; `spec_validate/tests/test_codegen.py:87-109` |

### 3. Cross-file consistency

| Spec Claim | Worktree Reality | Position |
|---|---|---|
| F-002 padding set is 4 fields: `ROUTE_PAR`, `RSVD_COMMTYPE`, `MULTICAST`, `FLIT_ECC`. | These four are exactly `_ENABLED = false` at `spec_validate/include/ni_flit_constants.h:42-69`; other header fields shown there are enabled. | Correct. |
| F-006 current codegen emits per-register single-value access enums. | `ni_regs.h` emits one enum per register at `spec_validate/include/ni_regs.h:61-92`; generator does it at `spec_validate/tools/elaborate/cpp_registers.py:94-105`. | Correct. |
| AccessMode set covers current data. | Current generated register access values are `RW`, `RO`, `RW1C`, `WO`; no current register uses `WC` at `spec_validate/generated/ni_registers.json:17-360`. | Correct, but WC contract remains missing. |
| F-004 is a real bug. | `TXN_MIN_LATENCY` reset is `0xFFFF` at `spec_validate/generated/ni_registers.json:136-138`; `RegisterFile::reset()` writes zero to all offsets at `c_model/src/register_file.cpp:53-57`. | Correct. |
| F-005 is a real drift risk. | 31 offsets are hand-maintained at `c_model/src/register_file.cpp:12-43`; generated header already has individual offsets at `spec_validate/include/ni_regs.h:14-45` but no array. | Correct. |
| X.1 item list covers all blocks fallout. | It misses codegen tests and SV test setup still iterating `"blocks"` at `spec_validate/tests/test_codegen.py:119-132` and `spec_validate/tests/test_codegen_sv.py:248-253`. | Incorrect/incomplete. |
| “F-007 already resolved” | Current `RegisterFile` uses generated csr policy sentinels for misaligned/unmapped/subword paths at `c_model/src/register_file.cpp:77-115`; sentinels are in `spec_validate/include/ni_regs.h:103-111`. | Consistent. |

### 4. Testability

| Phase | Testability | Required Clarification |
|---|---|---|
| X.1 | Partly testable. Deletion can be verified by `--domain blocks` rejection, no `ni_blocks.h`, no `ni_blocks_pkg.sv`, and passing pytest. | Add exact tests to remove/update in `test_codegen.py`, `test_codegen_sv.py`, and CLI `choices`. |
| X.2 | Partly testable. Can test generated `<REG>_RESET`, `ALL_OFFSETS`, and `TXN_MIN_LATENCY` reset value. | Define exact generated names/types, reserved-row exclusion, and a c_model test asserting `TXN_MIN_LATENCY` resets to `0xFFFF`. |
| X.3 | Not complete enough. RW1C and WO tests are named, but RO/RW behavior is undefined. | Define `RO` write behavior, `access_mode_of()` placement/default behavior, and whether `write_field()` bypasses access policy. |
| X.4 | Mostly testable. Existing tautological padding test is at `c_model/tests/test_flit.cpp:42-45`; raw constructor allows injecting bad padding via `Flit(raw)` at `c_model/include/flit.hpp:17`. | Specify which padding bit to set in the test and require compile tests after removing payload API. |
| X.5 | Documentation-only and testable by grep/checklist. | Specify whether `SUFFICIENCY_FINDINGS.md` itself must be edited to mark final states. Current doc still lists F-004/F-005/F-006 as gaps at `c_model/SUFFICIENCY_FINDINGS.md:17-33`. |

### 5. Audience Fit

A subagent with no prior context can understand the broad direction, but cannot execute TDD cleanly from this spec. It assumes knowledge of current generated-file paths, existing test layout, access policy intent, and codegen conventions. The most serious audience-fit failure is that X.3 asks for an access-mode redesign without specifying full access semantics, while current `RegisterFile` has public behavior for all mapped writes at `c_model/src/register_file.cpp:95-120`.

### 6. Verdict

NEEDS REVISION (with specific changes).

Confidence: high.

Top concern: define the complete register access contract, especially RO writes and `access_mode_of()`, before sending this to an implementation subagent.
