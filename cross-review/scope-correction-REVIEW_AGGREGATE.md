# Cross-Review Aggregate — Scope Correction Spec

**Target**: `docs/superpowers/specs/2026-05-28-scope-correction-design.md`
**Reviewers**: Codex GPT-5.5 + independent Claude subagent.
**Both verdicts**: **NEEDS REVISION** (confidence: HIGH).

---

## Consensus (both reviewers, all VERIFIED against worktree)

| # | Gap | Evidence | Required spec fix |
|---|---|---|---|
| C-1 | **RO write behavior undefined** | `register_file.cpp:117` writes every mapped offset unconditionally; X.3 only spells out RW1C + WO | Add to X.3: define what `write32` to RO does (ignore? DecErr? SlvErr?) |
| C-2 | **`access_mode_of(offset)` is undefined** | Spec X.3 references it but never says where it lives, what type signature, what unmapped/reserved behavior is | Define: is it codegen-emitted helper? c_model free function? What's the dispatch table? |
| C-3 | **SV register access-mode emission is first-time, not redesign** | `sv_registers.py:48-70` jumps from masks straight to `endpackage` — there is NO existing SV access enum to redesign | Re-phrase X.3 to say SV side is **new** elaboration, plus the typedef + per-reg `localparam` shape |
| C-4 | **Phase ordering not enforced** | X.1 deletes `ni_blocks.h` but `c_model/include/ni_spec.hpp:8` still `#include`s it; running X.2/X.3/X.4 before X.1 cleans that include = broken build | Add explicit "phase gate" rule: each phase ends with green pytest + ctest + `--check`; next phase blocked until prior passes |
| C-5 | **Blocks cleanup is incomplete** | Codegen tests still iterate `"blocks"` at `test_codegen.py:119-132`, `test_codegen_sv.py:248-253`; CLI `--domain` choices and usage text expose `blocks` at `codegen.py:4-12,40-48,253` | X.1 must enumerate: codegen tests, CLI `--domain` choices list, usage docstring, `tools/README.md` dataflow |
| C-6 | **WC AccessMode has no consumer** | Generated registers use only RW/RO/RW1C/WO (`ni_registers.json:17-360`); WC is in the enum but no register has it | Drop WC from the enum, OR explicitly state "reserved for future; emit but no register uses it today" |
| C-7 | **F-001 / F-003 status conflation in X.5** | X.5 ends with "0 PENDING findings" but F-001 and F-003 are explicitly DEFERRED, which is not the same as RESOLVED | Add distinct status column or rephrase: "0 PENDING; 2 DEFERRED with documented re-open trigger" |

---

## Claude-only findings (high value, VERIFIED)

| # | Finding | Why it matters |
|---|---|---|
| K-1 | **Spec says "31 single-value enum class" — actually 30** in `ni_regs.h:62-92` (reserved row `0x110` skipped) | Numerical accuracy. Implementer counting offsets won't match. |
| K-2 | **F-002 test can't be tightened with current Flit API**: `raw_` is private, `raw()` returns `const&`, `set_header_field` won't dispatch to padding fields (not in dispatch table) | **Real implementation blocker**. X.4 says "drive a non-zero value into a padding bit range" but no API exists to do so. Either (a) add `Flit::set_raw_byte_for_test(idx, val)` test-only friend, or (b) accept the test stays tautological and document why. |
| K-3 | **`ResetValuesAreZeroForNow` test (`test_register_file.cpp:14`) actively asserts the F-004 bug**. Just adding new tests for non-zero reset isn't enough — this test must be replaced/deleted. | X.2 says "update tests" generically; must specifically mandate replacing this test. |
| K-4 | **F-002 padding field name list — WHERE does it live?** If hand-listed in `flit.hpp` it violates Invariant 2 ("c_model 不 hardcode 規格值"). | Spec must specify: codegen-emitted constexpr array of `_ENABLED=false` field names, OR test-only data, OR explicit Invariant 2 exception with rationale. |
| K-5 | **`read32` needs early return for WO** so reads return 0 (per access policy). Spec only mentions changing `is_wo_`; that helper alone doesn't reach `read32`. | X.3 implementation guidance incomplete. |

---

## Codex-only findings (worth incorporating)

| # | Finding | Required spec fix |
|---|---|---|
| X-1 | block accessors in `constants.py:208-231` not addressed | X.1 must decide: keep (function_blocks validator may use them) or remove |
| X-2 | Reserved row `0x110` has `reset_expr: null` — how to handle in F-004 elaboration? | X.2 must specify: skip reserved rows |
| X-3 | Generated-source path ambiguous: spec says `ni_registers.json` but the actual file is `spec_validate/generated/ni_registers.json` | Use full paths in X.2 / X.3 |
| X-4 | `ALL_OFFSETS[]` consumption shape undefined: linear scan vs lookup helper? | X.2 must spell out e.g. `static const std::unordered_set<uint32_t> kKnown{ALL_OFFSETS, ALL_OFFSETS + N};` |
| X-5 | `SUFFICIENCY_FINDINGS.md` itself: spec doesn't mandate editing it | X.5 must include "edit the findings file to reflect final disposition" |

---

## Verification status

Every concrete claim above I cross-checked by reading the cited file/line. No reviewer claims were rejected as false.

---

## Required spec edits (numbered for traceability)

To take this from NEEDS REVISION → APPROVE, edit `2026-05-28-scope-correction-design.md`:

| Edit | Section | What to add |
|---|---|---|
| E-1 | X.1 | Add: (a) update `--domain` choices in `codegen.py`, (b) update usage docstring `codegen.py:4-12`, (c) delete/skip codegen tests at `test_codegen.py:119-132`, `test_codegen_sv.py:248-253`, (d) decide block accessors in `constants.py:208-231` |
| E-2 | X.1 | Add phase-gate rule: each phase ends green; next phase blocked otherwise |
| E-3 | X.2 | Specify: skip reserved rows (`reset_expr: null`); generate `<REG>_RESET` as `constexpr uint32_t`; one non-zero reset (`TXN_MIN_LATENCY = 0xFFFF`) must be in a test |
| E-4 | X.2 | Specify `ALL_OFFSETS[]` consumption shape in c_model |
| E-5 | X.2 | Specify replacing `ResetValuesAreZeroForNow` test, not just adding new ones |
| E-6 | X.3 | Define RO write behavior (recommend: silent ignore + return `{Ok,0}` per typical AXI4-Lite RO) |
| E-7 | X.3 | Specify `access_mode_of(offset) -> AccessMode` placement (recommend: c_model free function inside `register_file.cpp`, uses codegen `<REG>_ACCESS` constants in switch) |
| E-8 | X.3 | Specify SV side is **new** access-mode emission (not redesign): typedef + per-reg localparam shape |
| E-9 | X.3 | Specify `read32` early-return path for WO |
| E-10 | X.3 | Drop WC from enum (no consumer), OR document explicit reservation rationale |
| E-11 | X.4 | Specify HOW to write a padding bit in test (recommend: add test-only `Flit::set_raw_byte` friend OR keep tautological + document) |
| E-12 | X.4 | Specify WHERE the 4-name padding list lives (recommend: codegen elaborate `constexpr const char* PADDING_FIELDS[]` to keep Invariant 2; otherwise test-only) |
| E-13 | X.5 | Distinguish RESOLVED vs DEFERRED status; update "0 PENDING" wording |
| E-14 | X.5 | Add: edit `c_model/SUFFICIENCY_FINDINGS.md` itself per disposition table |
| E-15 | several | Fix "31 single-value enum class" → 30 |
| E-16 | several | Use full path `spec_validate/generated/ni_registers.json` |

---

## Final verdict

**NEEDS REVISION** before sending to implementation subagent. Direction is correct; 16 specific edits make the spec TDD-executable by a fresh subagent.

**Top concern**: K-2 (F-002 test cannot be tightened with current Flit API — needs test-only API extension or accept tautological test). This is the only finding that exposes an actual *design* gap; everything else is a *spec polish* gap.
