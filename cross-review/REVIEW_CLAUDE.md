# Independent Trade-off Review — D Session (codegen extension for F-001~F-006)

Reviewer context: fresh, no prior discussion. Read the code, not the descriptions.

## TL;DR up front

The `SUFFICIENCY_FINDINGS.md` mischaracterises 2 of the 6 findings. The codegen
**already emits** the data those findings claim is missing — the c_model
simply doesn't consume it. Treating these as "codegen gaps requiring extension"
inflates D-session scope and obscures the real trade-off. The real D-now cost
is much smaller than 3–4 days, and the real risk of D-defer is concentrated in
2 specific findings (F-002, F-006), not all 6.

---

## 0. Findings audit (evidence the brief is wrong)

Before discussing options, the brief's framing has to be corrected. I read the
emitted headers in `spec_validate/include/`:

| Finding | Brief claims codegen gap | Actual state in emitted header | Real work |
|---|---|---|---|
| F-001 HeaderField enum | yes | not emitted | Real codegen work (~30 LOC in `cpp_packet.py`) |
| F-002 padding-field list | yes | **already emitted** as `<FIELD>_ENABLED = false` (`ni_flit_constants.h:45,61,65,69`) | **Pure c_model work** — just consume `_ENABLED` |
| F-003 per-channel payload positions | yes | only `<CH>_WIDTH` emitted (`ni_flit_constants.h:74-78`); JSON has full `lsb/msb/width` per field | Real codegen work (~40 LOC) |
| F-004 per-register reset | yes | not emitted, but JSON has `reset_expr` on every register (`ni_registers.json`) | Trivial codegen work (~5 LOC) |
| F-005 ALL_OFFSETS array | yes | each `<NAME>_OFFSET` already emitted (`ni_regs.h:15-45`); no aggregate | Trivial codegen work (~5 LOC) |
| F-006 per-register access mode | yes | **already emitted** as `enum class <NAME>Access { RW1C }` etc. (`ni_regs.h:62-92`) | **Pure c_model work** — just consume |

Two findings (F-002, F-006) are not codegen gaps at all. The c_model
authored a stub and recorded a "codegen TODO" instead of consuming a symbol
that was already there. This is a process bug worth surfacing on its own
— the sufficiency-findings doc is being used as a deferred-work parking
lot, not as a codegen-gap inventory.

This re-classification changes every section below.

---

## 1. Option enumeration

The brief presents D-now vs D-defer. Both are crude. The real options:

| Option | Description |
|---|---|
| **A. D-now-all** | Extend codegen + consume in c_model for all 6 findings. ~3–4 days as briefed. |
| **B. D-now-consume-only** | Touch only c_model. Consume already-emitted `_ENABLED` (F-002) and `<NAME>Access` (F-006). No codegen change. ~0.5 day. |
| **C. D-now-subset (recommended)** | Option B + the two trivial codegen one-liners (F-004 `_RESET`, F-005 `ALL_OFFSETS[]`). Defer F-001/F-003. ~1 day. |
| **D. D-defer** | Leave all 6 as-is; revisit when Stage 2 needs them. As briefed. |
| **E. Re-classify and close** | Audit findings, mark F-002/F-006 as "c_model bug, not codegen gap" and fix in c_model now; mark F-001/F-003 as "deferred until Stage 2"; mark F-004/F-005 as "low-cost codegen, do now". Same physical work as C, but with honest bookkeeping. |
| **F. Document as permanent design** | Close findings as "first-round scope decision" with no follow-up. Only viable if Stage 2 is genuinely cancelled. |

Brief presented A vs D. The user has not seen B, C, E.

---

## 2. Criteria

The axes that actually decide this:

1. **Invariant 2 fidelity** — c_model 不 hardcode 規格值 (design doc §Invariants:12). Stubs that return `false`/`true`/`{}` regardless of input *are* hardcoded values (false-as-spec).
2. **Drift risk** — when spec adds a field/register, does the c_model silently drift or compile-fail?
3. **Dead-code accumulation** — `is_wo_`/`is_rw1c_` are unused predicates (`register_file.cpp:65-70` — declared, never called by `read32`/`write32`); these are tombstones, not workarounds.
4. **Test trust** — `PaddingFieldStaysZero` (`test_flit.cpp:42-46`) passes because the function returns `true` unconditionally. Test "passes" but tests nothing. Same for any future RW1C test.
5. **Marginal cost to fix later vs now** — codegen-side change vs c_model-side change have different cost asymmetry.
6. **Stage 2 uncertainty** — explicitly stated as "deferred indefinitely". Don't pay for capability that may never be exercised.
7. **Reader / next-implementer mental load** — 6 open TODOs anchored in code create non-trivial review friction.

I deliberately omit "code aesthetic" and "completeness for its own sake".

---

## 3. Weighting (for THIS situation)

| Criterion | Severity | Justification (file:line evidence) |
|---|---|---|
| Invariant 2 fidelity | **HIGH** for F-006/F-002, **MEDIUM** for F-004, **LOW** for F-001/F-003/F-005 | F-006 stubs (`register_file.cpp:65-70`) return `false` — that is a hardcoded spec claim ("no register is RW1C"), and `ERR_STATUSAccess { RW1C }` proves otherwise (`ni_regs.h:80`). F-002 same shape (`flit.hpp:88-93`). F-004 hardcodes all-zero reset (`register_file.cpp:53-60`); current spec happens to be all-zero so the lie is harmless today, but is silent next time. F-001 has a 6-field enum in code (`flit.hpp:39-48`) that mirrors spec — drift risk exists but compile-fails fast on missing symbol. |
| Drift risk | **HIGH** for F-001/F-005, **LOW** for the rest | F-001: add a field to spec → `header_field_pos` silently returns `{-1,-1}` → `set_header_field` becomes a no-op (`flit.hpp:75`). Silent. F-005: add a register to spec → `known_offsets()` (`register_file.cpp:11-46`) doesn't see it → `is_mapped_` returns false → read returns DecErr. Silent. Both are *worst-class* drift: tests still pass; behavior diverges. |
| Dead-code accumulation | **MEDIUM** | `is_wo_`/`is_rw1c_` are declared (`register_file.hpp:35-36`), defined (`register_file.cpp:65-70`), never called. Pure tombstone. Compiler may warn-as-error in some configs. Every reader has to ask "why is this here?". |
| Test trust | **HIGH** for F-002 only | `PaddingFieldStaysZero` (`test_flit.cpp:42-46`) is currently a tautology. This is misleading to anyone reading the test suite to assess coverage. RW1C is *not* falsely tested — it's absent from the test file (`test_register_file.cpp:1-86`), which is honest. |
| Marginal cost asymmetry | **MEDIUM** | F-002/F-006 cost = c_model edit only (codegen already emits). F-004/F-005 cost = ~5 LOC each in codegen + corresponding c_model consume. F-001 cost = ~30 LOC enum emission + table refactor. F-003 cost = highest (~40 LOC emitter + payload struct in c_model). Asymmetry is real and unevenly distributed. |
| Stage 2 uncertainty | **MEDIUM** | "Stage 2 deferred indefinitely" — F-003 payload positions exist *only* to support Stage 2 feature-unit packing. Doing F-003 now is paying for something with uncertain demand. F-001/F-002/F-005/F-006 are useful for *current* Flit/RegisterFile semantics regardless of Stage 2. |
| Reader / next-implementer load | **HIGH** | 6 inline TODO comments + a separate findings doc + an ambiguous "is this a codegen gap or c_model TODO" classification create real friction. Mixed-up findings (F-002, F-006) suggest the current state has *already* misled at least one reader (the c_model author). |

---

## 4. Blind spots

Things the brief assumes that may not hold.

1. **"D session = extending codegen for all 6"**. False premise. F-002 and F-006 are c_model-only fixes (codegen already emits). The "9 tasks, 3–4 days" estimate is bloated by this miscount. Real codegen extension is needed for F-001, F-003, F-004, F-005 only — and F-004/F-005 are one-liners.

2. **"Workarounds are harmless if Stage 2 never happens"**. False for F-006: `is_rw1c_`/`is_wo_` are dead code *today*, in *current* tests. Their existence implies a behavior contract the code doesn't honor. If a future test writes to a WO register and reads it back, it'll see the value rather than 0 (`register_file.cpp:117` unconditionally stores). That's a *current-round* spec-vs-implementation drift, not a Stage 2 concern.

3. **"Invariant 2 is binary"**. The design doc says "違反視同 drift bug" (`design.md:12`). But the current code is *already* violating it: returning hardcoded `false` from `is_wo_` is a hardcoded spec claim. Calling it "workaround" doesn't change that. Either the invariant is enforced or it isn't; pretending the workarounds satisfy it is the worst of both worlds.

4. **"Test pass = behavior validated"**. `PaddingFieldStaysZero` (`test_flit.cpp:42-46`) is a vacuous test. The implementation literally cannot fail it (`flit.hpp:88-93` returns `true`). When a future engineer regresses padding behavior, this test will still pass. This is *worse* than no test, because it gives false confidence.

5. **"Findings are independent"**. F-005 (offset list) and F-006 (access mode) likely share the same emitter loop in `cpp_registers.py`; doing them together costs barely more than either alone. F-004 (reset_expr) similarly. The "9 tasks" framing creates artificial fragmentation.

6. **"D-defer means do nothing now"**. Even under D-defer, the existing dead `is_wo_`/`is_rw1c_` methods (`register_file.hpp:35-36`) and the tautological `PaddingFieldStaysZero` test (`test_flit.cpp:42`) should be either fixed or explicitly deleted. Leaving them in place isn't "deferral"; it's accruing technical debt with no marker visible from inside the source.

7. **Codegen output stability**. Once `<NAME>Access { RW1C }` is emitted as an enum (`ni_regs.h:80`), c_model can take a hard dependency on the namespace shape. Defining a more useful form *later* (e.g. a single `AccessMode` enum + per-register `constexpr AccessMode <NAME>_ACCESS`) is a breaking change to every consumer. The current emission is awkward (one enum-class per register, single value) — locking it in by consuming it as-is may be worse than throwing the F-006 codegen output away and re-doing it.

---

## 5. Recommendation

**Take Option C / E (re-classify + targeted fix), ~1 work-day, not 3–4.**

Concrete plan:

1. **Fix c_model F-002 and F-006 now (no codegen change).** Codegen already emits the symbols. `flit.hpp::check_padding_is_zero` (`flit.hpp:88-93`) should iterate header fields and check `*_ENABLED == false` regions stay zero. `register_file.cpp::is_wo_`/`is_rw1c_` (lines 65-70) should consult the per-register `<NAME>Access` enum. Either then *use* them in `write32`/`read32`, or delete them. Currently they are dead. Picking "use them" enforces Invariant 2; picking "delete them" reduces dead surface — both are honest. Both are < 1 hour.

2. **Reject the current F-006 emission shape before consuming.** `enum class <NAME>Access { RW1C };` (`ni_regs.h:80`) is a per-register single-value enum — strange to consume. Before committing the c_model to this API, change codegen to emit a single `enum class AccessMode { RW, RO, RW1C, WO };` plus `constexpr AccessMode <NAME>_ACCESS = AccessMode::RW1C;`. ~10 LOC change in `cpp_registers.py`, ~10 LOC change in c_model. Do this before step 1 so step 1 consumes the right shape.

3. **Do F-004 and F-005 codegen (~10 LOC total).** Trivial additions in `cpp_registers.py`. Wire `reset()` (`register_file.cpp:53-60`) to per-register reset; replace hand-maintained `known_offsets()` (`register_file.cpp:11-46`) with `ALL_OFFSETS[]`. ~2 hours total.

4. **Defer F-001 and F-003.** Both are real codegen work. F-001's drift risk is real but the failure mode is "silently no-op" (`flit.hpp:75,84`), which is well-localized to set/get header field — easy to spot if exercised. F-003 entirely supports Stage 2 packing, which is "deferred indefinitely". Pay the cost when Stage 2 demand materialises, not now.

5. **Document the rejected findings F-001/F-003 clearly.** Change `SUFFICIENCY_FINDINGS.md` to say "DEFERRED until Stage 2 (no current consumer)" with a list of *what triggers re-opening* (e.g. "first attempt to pack AW payload in c_model"). This is the difference between "TODO" rot and a real deferral with a tripwire.

Why not D-defer entirely? Because F-006 is producing dead code (`register_file.cpp:65-70`) and a misleading test (`test_flit.cpp:42`) *today*. Those aren't deferrals — they're bugs. Leaving them in is paying interest on debt that doesn't need to exist.

Why not D-now-all? Because F-003 specifically pays for Stage 2 capability with uncertain demand. The brief itself says Stage 2 may never happen for some findings. A 40-LOC emitter for "set/get_payload_channel" with no current consumer fails the Karpathy lens — if I deleted it tomorrow, would any reader notice? No.

---

## 6. Verdict

**Conditional on which findings.** D is necessary for F-002, F-004, F-005, F-006 (low cost, removes current drift / dead-code). D is **not** necessary for F-001 and F-003 (defer until Stage 2 demand or first observed drift).

The "all-or-nothing D session" framing should be rejected. Treat the findings list as 6 separate decisions, not one bundle.

**Confidence: HIGH** on the re-classification (codegen output and JSON source are public evidence I read directly). **MEDIUM** on the "Stage 2 deferred indefinitely" judgement — if Stage 2 starts within 1 month, do F-003 now; if not, defer.

**Top concern:** the `SUFFICIENCY_FINDINGS.md` doc itself is currently misleading — 2 of the 6 entries describe a non-existent codegen gap. Acting on this doc without re-reading the emitted headers wastes 1+ days of codegen work and locks in an awkward `<NAME>Access` enum shape that should be re-designed first.
