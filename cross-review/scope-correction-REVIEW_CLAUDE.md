# Cross-Review — Scope Correction Spec (independent Claude reviewer)

Target: `docs/superpowers/specs/2026-05-28-scope-correction-design.md`
Reviewer context: fresh — read only the target spec, cited background files, and worktree code/JSON.

---

## 1. Ambiguity

### Sentences a fresh implementer cannot resolve

| # | Spec sentence | What's missing |
|---|---|---|
| A1 | Phase X.1: "`spec_validate/ni_spec/invariants.py` `check_mode_enum_name_unique` | delete" | `invariants.py:355-380` defines this function with return type `List[str]` (not `List[Issue]`). Deleting it is straightforward, but the spec does not say whether the function's *call site* in `__main__.py:153-154` (which wraps the returned strings into Issues) is what gets removed, or both. The spec lists only "the `check_mode_enum_name_unique` call" — implementer must infer the helper-loop too. |
| A2 | Phase X.1: "drop the 2 mode-enum-unique tests; keep schema + cross-ref tests" | `test_function_blocks.py` has 15 tests. The "2" being dropped are `test_mode_enum_names_unique_after_block_prefix` (line 114) and `test_l2_mode_enum_unique_check_fires_on_collision` (line 134). But other tests reference modes too — `test_mode_identifiers_valid_for_cpp_sv` (line 53), `test_constants_blocks_modes_of` (line 100). Spec does not say these stay; implementer must guess. State explicitly: keep both. |
| A3 | Phase X.1: "`spec_validate/tools/codegen.py --check` | remove blocks from drift check" | The actual drift-check is data-driven from `DOMAIN_TO_EMITTER` (codegen.py:165). Removing the dict entries (line above in the same table) is sufficient — no separate `--check` change needed. Spec implies two edits; really one. Clarify or merge the rows. |
| A4 | Phase X.2: "SV side same" (re: `<REG>_RESET`) | `sv_registers.py` currently does *not* emit access enums at all (it skips that section). Spec says "SV side same" for reset but says nothing about whether SV registers emitter should *also* gain access-mode emission in Phase X.3. The asymmetry is not flagged. See cross-file consistency §3 below. |
| A5 | Phase X.3: "`ACCESS_RO, ACCESS_RW, ...`" enum encoding `logic [2:0]` | 5 values fit in 3 bits, fine. But spec does not say whether the enum is declared in `ni_regs_pkg` or a new package, nor whether `ACCESS_WC` is emitted in SV given the X-Out-of-Scope note ("AccessMode WC ... do not implement"). Two consistent reads: (a) emit all 5 values in the enum but do not exercise WC, (b) emit only RO/RW/RW1C/WO/WC at type level and skip behavior. Implementer needs both decisions. |
| A6 | Phase X.4: "Hand-list of these 4 names is acceptable for now; full table-driven iteration blocked on F-001" | Where does the hand-list live — inside `flit.hpp` (header-only) or as a new `constexpr std::array` in codegen output? Spec does not pick. If it lives in `flit.hpp`, that contradicts Invariant 2 ("c_model 不 hardcode 規格值"). The 4 names `ROUTE_PAR`, `RSVD_COMMTYPE`, `MULTICAST`, `FLIT_ECC` are spec-derived, so hand-listing them in c_model *is* hardcoding. |
| A7 | Phase X.4: "Remove from public API. ... Re-introduce as part of F-003" | "Remove" how? Delete the methods, or move to `private:`, or rename `_unsupported`? Codex review (per `REVIEW_AGGREGATE.md:51`) suggested rename. This spec did not pick. |
| A8 | "End state: 0 PENDING findings" (X.5) | F-001 and F-003 are marked DEFERRED — but a DEFERRED finding *is* a PENDING finding with a re-open trigger. Spec conflates "deferred" with "closed". Implementer reading `SUFFICIENCY_FINDINGS.md` after this round will find 2 PENDING (F-001/F-003), unless the spec wants them recategorized to a new "DEFERRED" status column. Decide. |

### Invariant interactions not explained

- **Invariant 2 vs Phase X.4 hand-list**: see A6.
- **Invariant 5** ("`ni_function_blocks.json` 保留 as feature inventory") leaves dangling responsibility: who consumes this inventory now? Cross-ref `check_blocks_xref_packet` / `check_blocks_xref_registers` survive (table row), but the spec doesn't state the *purpose* the validator serves once nothing reads the JSON downstream. Looks like a slow drift fuse — but say so.

---

## 2. Completeness

### Missing contracts

| Gap | Why it matters |
|---|---|
| **Phase ordering enforcement** | Spec implies X.1 → X.2 → X.3 → X.4 sequence but does not state it. X.1 deletes `ni_blocks.h`, which `c_model/include/ni_spec.hpp:8` includes; if X.2 work starts before that `#include` is removed, the build breaks. Spec must say: X.1 must complete (incl. removing `#include "ni_blocks.h"`) before X.2 starts. |
| **Per-phase regression discipline** | "every commit must pass all existing tests" (CLAUDE.md) — but phases delete tests (X.1: 2 mode-enum tests). What happens if X.2 work breaks an X.1-surviving test? Spec does not require running the full suite after each phase. |
| **Codegen artifact recommit** | After X.2/X.3 codegen changes, `spec_validate/include/ni_regs.h` + `spec_validate/rtl_pkg/ni_regs_pkg.sv` *must* be regenerated and recommitted (drift gate). Spec does not say so. |
| **What `_RESET` should hold for reserved entries** | `ni_registers.json:228-233` has a `kind: "reserved"` row (offset 0x110). It has no `reset_expr`. Codegen iteration in `cpp_registers.py:97` skips `kind != "register"`. Confirmed safe — but spec should explicitly say reserved offsets get no `_RESET` constant. |
| **`is_wo_` / `is_rw1c_` signature change** | X.3 changes their bodies to consult `access_mode_of(offset)`. But `access_mode_of` is a new function — not defined in the spec. Where does it live (codegen-side switch? c_model-side switch over offset→AccessMode constexpr map?)? Without this, X.3 is not buildable. |
| **`write32` semantics for RW1C** | Spec says "write 1 clears the bit" but does not say what happens to a write of `0` to an RW1C reg (per AMBA: ignored / preserve old value). Implementer might write the value directly. |
| **`write32` semantics for WO + read** | Spec says "read returns 0". That's already `csr_policy::WO_READ_IS_ZERO`. But `register_file.cpp:92` returns `storage_[offset]` for any mapped read — meaning currently WO reads return whatever was last written, not 0. The fix is not just `is_wo_`; `read32` body needs an early return. Spec does not state this. |
| **GTest cases for F-004 / F-005** | X.2 changes c_model behavior (reset value, offset list) but the spec does not enumerate the GTests required to lock in the behavior. The TXN_MIN_LATENCY = 0xFFFF reset (`ni_registers.json:138`) is *the* witness — there must be a test asserting `read32(TXN_MIN_LATENCY_OFFSET).data == 0xFFFF` after `reset()`. Existing test `ResetValuesAreZeroForNow` (`test_register_file.cpp:14`) actively *asserts the bug*. Spec must say to update or delete it. |
| **Padding test concretization** | Phase X.4 mentions "drive a non-zero value into a padding bit range" — but `Flit` has no API to write to padding fields (they're not in the dispatch table). Implementer must either (a) use raw byte mutation via `raw_` (but `raw_` is private and `raw()` returns const) or (b) add a `Flit` test helper. Spec does not pick. |
| **SUFFICIENCY_FINDINGS.md update** | X.5 says "final disposition" but does not state which file gets the table written (assume `c_model/SUFFICIENCY_FINDINGS.md` itself), nor whether the disposition table replaces the existing per-finding sections or supplements them. |
| **`ni_spec.hpp` regression** | Includes `ni_blocks.h` (line 8). X.1 removes the file but only mentions "drop `#include "ni_blocks.h"`" — does the umbrella header stay, or does it shrink? Spec says "drop", implying keep umbrella, just shrink. Confirm. |

### Tests not defined for each deliverable

| Phase | Stated deliverable | Test that verifies it |
|---|---|---|
| X.1 | "blocks domain removed" | (missing) — should be: `pytest -q` passes with the 2 deleted tests gone, `py -3 tools/codegen.py --check` passes, c_model still compiles |
| X.2 F-004 | "use codegen `<REG>_RESET`" | (missing concrete) — should be: `TXN_MIN_LATENCY_OFFSET` reads `0xFFFF` after construction |
| X.2 F-005 | "drop hand-maintained 31-entry list" | (missing) — should be: count of `ALL_OFFSETS[]` matches `len(spec.registers where kind=='register')` at codegen-time + a c_model test using `sizeof(ALL_OFFSETS)/sizeof(...)` |
| X.3 F-006 | "RW1C / WO semantics" | Spec *does* mention "New GTest cases cover RW1C clear + WO read-as-zero against ERR_STATUS / EXCLUSIVE_MONITOR_CTRL" — this one is OK |
| X.4 F-002 | "padding check" | Spec mentions tightened `PaddingFieldStaysZero` — OK in concept, blocked by A6/test-helper gap |

---

## 3. Cross-file consistency

### Verified against actual worktree

| Spec claim | Worktree truth | Match? |
|---|---|---|
| `spec_validate/include/ni_flit_constants.h:25-69` has `_ENABLED` flags incl. 4 false ones | `ni_flit_constants.h:45,61,65,69` → `ROUTE_PAR_ENABLED=false`, `RSVD_COMMTYPE_ENABLED=false`, `MULTICAST_ENABLED=false`, `FLIT_ECC_ENABLED=false` | YES |
| Per-reg access enum at `ni_regs.h:62-91` | `ni_regs.h:61-92`. 30 enum classes (not 31; reserved at 0x110 skipped). Modes: RW, RO, RW1C, WO | YES (but **off by one** — spec says "31" in Phase X.3 prose; actually 30 emitted). Confirm: `ni_registers.json` has 31 entries but one is `kind: "reserved"` (no access). |
| `<REG>_RESET` constant is *missing* from codegen | `ni_regs.h` has offsets + field masks + access enums + csr_policy. No `_RESET`. | YES (it's missing — X.2 plan is sound) |
| `ALL_OFFSETS[]` missing | Confirmed missing in `ni_regs.h`. | YES |
| `register_file.cpp:53-57` reset-to-0 universally | `register_file.cpp:53-60` — `for (auto off : known_offsets()) storage_[off] = 0;`. | YES (live bug; TXN_MIN_LATENCY reset value 0xFFFF in `ni_registers.json:138` is lost) |
| `register_file.cpp:12-43` 31-entry hand list | `register_file.cpp:12-44` — actually **31 entries** including all real registers. Spec count "31" correct here. | YES |
| AccessMode set `{ RO, RW, RW1C, WO, WC }` covers all access types in `ni_registers.json` | Searched `ni_registers.json` for `"access":` — values used are `RW`, `RO`, `RW1C`, `WO`. **`WC` does not appear.** | NO — spec emits a value that has no consumer. Out-of-Scope note acknowledges this ("present in the enum but no current register uses it"), but Invariant 2 spirit asks: why pollute the enum with a value the spec does not need? Better: emit only the 4 actual values today; add `WC` when a register declares it. |
| F-002 padding field list = `ROUTE_PAR`, `RSVD_COMMTYPE`, `MULTICAST`, `FLIT_ECC` | Verified — `ni_packet.json:78-132` has these 4 with `enabled: false`. **But `noc_qos` has `width: 0, enabled: true, lsb/msb: null`** (line 38-44). It's a zero-width placeholder, not padding. `check_padding_is_zero` should not touch it. Spec is correct to exclude it from the 4 names, but does not explain *why* — fresh reader may wonder if a width=0 enabled field qualifies. Add one sentence. |
| `ni_function_blocks.json`'s validator references survive deletion | Confirmed: `invariants.py:311-352` (`check_blocks_xref_packet`, `check_blocks_xref_registers`, `check_blocks_param_uniqueness`, `check_blocks_related_features_symmetric`) all consume `ni_function_blocks.json` directly via `loader.load_doc`. None depend on `cpp_blocks` / `sv_blocks` emitters. Deletion of the emitters does **not** break the validators. | YES |
| `c_model/include/ni_spec.hpp:8` `#include "ni_blocks.h"` is the only c_model consumer | grep'd: c_model files include `ni_spec.hpp` (umbrella) only; nothing reaches into `ni_blocks.h` directly. Safe to drop the include. | YES |
| `tools/emit/` → `tools/elaborate/` rename (first-round Phase 0 step 3) | Already done — `spec_validate/tools/elaborate/` exists. Spec X.1 does not need to re-do this. | OK (spec doesn't claim to redo it; just noting prior round already cleared) |

### Inconsistencies that *will bite* the implementer

1. **SV registers emitter never had access enums** (`sv_registers.py:48-70` jumps from masks to `endpackage`). Phase X.3's "SV side similarly: `typedef enum logic [2:0] {...}`" is a *net add* to SV, not a redesign — but the spec's "Current codegen emits 31 single-value enum class" prose only applies to C++. **State explicitly**: X.3 is "C++ redesign + SV first-time emission".
2. **Spec count "31"** for the access enums is wrong (actually 30; reserved-row skipped). Minor but the kind of detail that erodes trust.
3. **`enum class <REG>Access` in `ni_regs.h` lines 62-92** is currently a real C++ symbol that other code might consume. grep'd: nothing in `c_model/` consumes them today (`register_file.cpp` does not reference `*Access`). Safe to remove. Confirmed.
4. **Estimate inconsistency with prior aggregate**: `REVIEW_AGGREGATE.md:84` total was 1.5 days; this spec is 2 days. Reasonable padding for X.1 (not in aggregate), but the 0.5 day for X.1 looks light given the number of file-and-test edits (15+ files touched in the table). Bump to 0.75.

---

## 4. Testability

Per-phase concrete success criteria evaluation.

### Phase X.1 — Blocks domain removal

| Sub-goal | Concrete test |
|---|---|
| Files deleted | `ls spec_validate/include/ni_blocks.h` returns no such file (or `! test -f`) |
| Codegen --check passes | `py -3 tools/codegen.py --check` exit 0 |
| `pytest spec_validate/tests/test_function_blocks.py -v` | 13 tests pass (15 - 2 dropped) |
| c_model still builds | `cmake --build c_model/build` exit 0 |
| Validator still cross-refs blocks JSON | `pytest spec_validate/tests/test_function_blocks.py::test_xref_packet_fields_exist` passes |

Spec does not list these. **Add an "Exit criteria" line to X.1**.

### Phase X.2 — F-004 + F-005

| Sub-goal | Concrete test |
|---|---|
| `<REG>_RESET` emitted | Grep `ni_regs.h` for `TXN_MIN_LATENCY_RESET = 0xFFFF` |
| `ALL_OFFSETS[]` emitted | Grep `ni_regs.h` for `ALL_OFFSETS\[` with 30 entries |
| `RegisterFile::reset` uses codegen | Update `RegisterFile, ResetValuesAreZeroForNow` test (it *currently asserts the bug*); add new test `TXN_MIN_LATENCY_ResetsTo_0xFFFF` |
| `known_offsets_` uses codegen | Compile-time check via codegen drift — sizeof check |

Spec is **mostly concrete** but does not flag that `test_register_file.cpp:14-19` *must change* (it actively expects 0).

### Phase X.3 — F-006 redesign

| Sub-goal | Concrete test |
|---|---|
| `enum class AccessMode { RO, RW, RW1C, WO, WC }` exists | Grep `ni_regs.h` |
| Per-reg constexpr exists | Grep `ERR_STATUS_ACCESS = AccessMode::RW1C` |
| RW1C semantics | New GTest: write `0x7` to `ERR_STATUS`, read returns `0`; write `0x1`, read returns `0x6` after another `0x1` write (verify clear-bit) |
| WO read returns 0 | New GTest: write `0x1` to `EXCLUSIVE_MONITOR_CTRL`, read returns 0 |
| Old per-reg `<NAME>Access` enums removed | Grep ensure no `ERR_STATUSAccess` (the old shape) |

**Spec is concrete here.** The only gap: `access_mode_of()` location (see §2).

### Phase X.4 — F-002 + API quarantine

| Sub-goal | Concrete test |
|---|---|
| `check_padding_is_zero()` returns false on dirty padding | Implementer needs a write hook (see A6 / completeness gap) |
| `set_payload_channel` / `get_payload_channel` removed | Compile-fail test on caller, or grep flit.hpp for absence |

**Spec is weak here** due to A6/A7. Needs concrete picks before implementation.

---

## 5. Audience fit

A subagent with no prior context and TDD discipline will need:

| Need | Spec satisfies? |
|---|---|
| What files to edit (paths) | YES (X.1 table is exhaustive; X.2-X.4 mostly clear) |
| What functions to add / remove (signatures) | PARTIAL — `access_mode_of` not defined; `is_wo_` body change implied but not spelled out |
| Order of operations | NO (see §2) |
| How to verify each phase | PARTIAL — X.3 best, X.1 worst |
| What to do on test failure | NO — falls back to CLAUDE.md "stop after 3 attempts" |
| RTL analogy where needed | N/A (C++ side; no RTL concepts new in this round) |
| Branch / commit discipline | Mentioned indirectly via "subagent-driven-development" but not in spec body |
| F-001 / F-003 deferral mechanics | Re-open trigger stated, but where it's recorded is not (annotate `SUFFICIENCY_FINDINGS.md`? add a new YAML?) |

**The subagent will succeed at X.1 (mechanical) and likely X.3 (most detail). X.2 needs one explicit test mandate. X.4 will block on the `raw_` mutation question.**

---

## 6. Verdict

**NEEDS REVISION**

Specific required changes before TDD handoff:

1. Pin Phase X.4 hand-list location: codegen-side `constexpr std::array<std::string_view, 4> PADDING_FIELD_NAMES{...}` in `ni_flit_constants.h` (preserves Invariant 2), not hand-coded in `flit.hpp`. Phrase: "X.4 requires a 5-LOC codegen addition; not pure c_model fix."
2. Define `access_mode_of(offset)` precisely: either (a) a generated `constexpr AccessMode access_mode_of(uint32_t off)` switch in `ni_regs.h`, or (b) a c_model `unordered_map<uint32_t, AccessMode>` initialized from the per-reg constants. Pick (a).
3. Drop `AccessMode::WC` from the X.3 enum until a register needs it (Invariant 2 spirit). If kept, explicitly say it's a deliberate forward-compat hook.
4. Spell out: `test_register_file.cpp:14-19` (`ResetValuesAreZeroForNow`) is a bug-witness test and must be replaced in X.2 with `TXN_MIN_LATENCY_ResetsTo_0xFFFF`.
5. Spell out: `read32` body must early-return 0 for WO offsets when `csr_policy::WO_READ_IS_ZERO == 1`; touching only `is_wo_` is insufficient.
6. Add a `Flit` test-only helper (e.g., friend or `set_raw_byte_for_test`) so X.4 can drive non-zero padding bits without making `raw_` public.
7. Add explicit phase ordering: X.1 must finish first (it removes `#include "ni_blocks.h"`). After each phase: `tools/codegen.py --check` + `pytest` + `cmake --build` all green before next phase opens.
8. Fix "31" → "30" in X.3 prose.
9. Make Phase X.3 say "C++ redesign + SV first-time emission of access enums" (sv_registers.py currently has none).
10. Clarify F-001 / F-003 disposition status: introduce "DEFERRED" as a status distinct from PENDING/RESOLVED, and document where the re-open trigger lives.
11. Decide and state the X.4 API removal verb: delete vs `_unsupported` rename. Recommend delete (cheaper to re-add; rename leaves dead naming).

**Confidence: HIGH** — every claim above is verified against the actual worktree files cited.

**Top concern (one line):** Phase X.4 unilaterally hand-lists 4 padding field names without saying where; spec must pin codegen-side `PADDING_FIELD_NAMES[]` or it silently violates Invariant 2.

---

## Appendix — file:line citations used

- `docs/superpowers/specs/2026-05-28-scope-correction-design.md` (target)
- `c_model/SUFFICIENCY_FINDINGS.md` (F-001..F-007 definitions)
- `cross-review/REVIEW_AGGREGATE.md:51, 84` (prior estimates / quarantine option)
- `spec_validate/include/ni_blocks.h:1-74` (file to delete)
- `spec_validate/include/ni_regs.h:14-45` (offsets), `:48-59` (field masks), `:62-92` (per-reg Access enums — 30 entries, not 31), `:103-112` (csr_policy)
- `spec_validate/include/ni_flit_constants.h:45,61,65,69` (4 ENABLED=false flags — verified)
- `spec_validate/include/ni_flit_constants.h:24-25` (`NOC_QOS_WIDTH=0, ENABLED=true` — explains why noc_qos is not in padding set)
- `spec_validate/generated/ni_packet.json:78-132` (4 enabled=false header fields)
- `spec_validate/generated/ni_registers.json:138` (TXN_MIN_LATENCY reset 0xFFFF)
- `spec_validate/generated/ni_registers.json:228-233` (reserved row, no access)
- `spec_validate/tools/elaborate/cpp_registers.py:95-108` (where per-reg access emission lives)
- `spec_validate/tools/elaborate/sv_registers.py:48-70` (SV registers: no access enum emitted today)
- `spec_validate/tools/codegen.py:40-49` (DOMAIN_TO_EMITTER), `:165-184` (drift check loop)
- `spec_validate/ni_spec/invariants.py:311-352` (validator functions surviving), `:355-380` (check_mode_enum_name_unique — to delete)
- `spec_validate/ni_spec/__main__.py:153-154` (call site to delete)
- `spec_validate/tests/test_function_blocks.py:53-60, 100-111, 114-131, 134-144` (existing tests — only the last two are mode-enum-unique)
- `spec_validate/tests/test_codegen.py:87-108, 119, 131` (blocks-related drift tests to remove)
- `spec_validate/tests/test_codegen_sv.py:192-237, 248, 252, 316` (blocks-related SV tests to remove)
- `c_model/include/flit.hpp:88-93` (F-002 stub), `:95-102` (F-003 stubs to quarantine), `:39-48` (header_field_pos hand dispatch)
- `c_model/include/ni_spec.hpp:8` (`#include "ni_blocks.h"` to remove)
- `c_model/include/register_file.hpp:34-36` (`is_mapped_/is_wo_/is_rw1c_` signatures)
- `c_model/src/register_file.cpp:11-46` (31-entry hand list), `:53-60` (reset to 0), `:65-70` (F-006 stubs), `:84-92` (read mapped path — needs WO early return)
- `c_model/tests/test_flit.cpp:42-46` (tautological PaddingFieldStaysZero)
- `c_model/tests/test_register_file.cpp:14-19` (bug-witness ResetValuesAreZeroForNow)
- `spec_validate/tools/README.md:48, 64, 76, 80, 115, 119, 240, 263, 267, 278` (docs to update for blocks removal)
- `spec_validate/docs/guide/architecture.md:7, 23, 25` (docs to update)
