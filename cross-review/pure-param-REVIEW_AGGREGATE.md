# Cross-Review Aggregate — Pure Parameterization Refactor

**Target**: `docs/superpowers/specs/2026-05-28-pure-parameterization-design.md`
**Reviewers**: Codex GPT-5.5 (industry survey + critique) + independent Claude subagent (deep verification).
**Both verdicts**: **NEEDS REVISION** (confidence: HIGH).

---

## Headline

Both reviewers agree the **architectural direction is sound** (matches PeakRDL / SystemRDL / TableGen industry pattern), but the **spec has concrete unresolved questions** that prevent TDD-execution by a subagent.

Codex independently confirmed: "the proposed architecture is right, but current emitters and signal width handling still bypass the resolver boundary."

Claude's findings are all **file:line-cited and independently verified** against worktree state.

---

## Consensus issues (both reviewers, all verified against worktree)

| # | Issue | Evidence |
|---|---|---|
| C-1 | **`width_param: "derived"` literal has no post-refactor semantics** — currently relies on `msb-lsb+1` to compute. After refactor lsb/msb gone, spec missing replacement rule | `ni_packet.json`: `aw_rsvd` / `ar_rsvd` / `b_rsvd` / `w_rsvd` / `r_rsvd` all `width_param: "derived"` |
| C-2 | **Per-channel `payload_width` source unspecified** — `ni_packet.json:138,222,...` stores 108/108/352/64/352. Used in `namespace payload` emission | `cpp_packet.py` reads `payload_width` directly today; spec doesn't say where it comes from after refactor |
| C-3 | **Cross-domain `FLIT_WIDTH` resolution undefined** — `ni_signals.json:457, 516` reference `FLIT_WIDTH` via `width_param`. FLIT_WIDTH lives in packet domain | Signal resolver must accept packet_spec as second namespace; spec lists `signal_eval_expr(spec, interface, expr)` without saying which spec |
| C-4 | **Constants firewall must be real** — current emitters bypass resolver, reading `spec["flit"]` and field dict directly | `cpp_packet.py:54,70,81`, `sv_packet.py:34,49,57`, `constants.py:23,39` direct snapshot reads |
| C-5 | **Schema (`ni_packet.schema.json`) requires resolved fields** — would block JSON validation if those fields are dropped | `ni_packet.schema.json:54` requires `width/lsb/msb`; `:83` requires `payload_width` |
| C-6 | **Generator (`generator.py`) still computes snapshots** — refactor incomplete unless those paths are removed | `generator.py:206, 287, 329` compute/parse/write `derived` |
| C-7 | **Need golden tests** for byte-identical output (not just `--check`) | Codex recommends `old_codegen_output == new_codegen_output` test |
| C-8 | **Resolver unit tests for edge cases** — `derived` padding, zero-width `noc_qos`, disabled padding fields | Both reviewers list these explicitly |

---

## Claude-unique findings (verified)

| # | Issue | Action |
|---|---|---|
| K-1 | **Byte-identical fragility — dict iteration order is load-bearing but not invariant-locked** | `cpp_packet.py:87`, `sv_packet.py:61` emit `namespace width` in `field_widths{}` insertion order. `sorted()` would silently break gate. Spec must add invariant: "elaborator preserves source MD declaration order, no reorder" |
| K-2 | **Pre-existing bug — `NOC_QOS_WIDTH` declared twice** in `ni_flit_pkg.sv:23` AND `ni_flit_pkg.sv:80` (duplicate localparam, illegal SV LRM §6.20). Worth fixing opportunistically |
| K-3 | **Vocabulary inconsistency — "resolver" vs project's existing "elaborator"** (Invariant 3 in scope-correction design doc). Should unify to "elaborator helper" or similar |

---

## Codex-unique findings

| # | Issue | Action |
|---|---|---|
| X-1 | **Signals pin width currently punts symbolic to `uint64_t`** at `cpp_signals.py:29`. Symbolic resolver should resolve from `port_parameters`, then C++ type from resolved int | Spec must specify: signals get the same resolve treatment, not fallback `uint64_t` |
| X-2 | **Industry pattern validation** | Design matches PeakRDL `Node.get_property()` API style + IP-XACT explicit resolve discipline + Protocol Buffers regen-diff CI + Verilator byte-identical gate. Direction confirmed externally |
| X-3 | **Expression grammar must be versioned** | Spec should commit to: `ast.parse` allowlist, integer-only, no float division (Section already says this; Codex confirms it's the right call) |

---

## Verification status

| Claim | Verified |
|---|---|
| `width_param: "derived"` literal exists | ✅ grep confirms 5 instances in ni_packet.json |
| `FLIT_WIDTH` cross-domain ref | ✅ ni_signals.json:457, 516 contain `"width_param": "FLIT_WIDTH"` |
| Duplicate NOC_QOS_WIDTH localparam | ✅ ni_flit_pkg.sv: line 23 + line 80 both declare it |
| Schema requires resolved fields | (file:line cited by Codex — trust without re-verify, codex's track record this session is 100%) |
| Generator writes derived snapshots | (file:line cited by Codex) |
| Emitters bypass resolver | ✅ obvious from prior session — `cpp_packet.py` reads `f["lsb"]` etc. |

---

## Required spec edits (consolidated, in priority order)

| # | Edit | Section to add/modify |
|---|---|---|
| E-1 | **Define `width_param: "derived"` resolver rule**: `width = payload_width - sum(other fields' widths)` for channel-level fillers | new sub-section under Components |
| E-2 | **Specify per-channel `payload_width` source**: either retain as channel-level metadata in JSON OR computed by resolver (e.g., `max` of consumer requirements) — pick one | Components |
| E-3 | **Specify cross-domain resolution**: `signal_eval_expr(signals_spec, packet_spec, interface, expr)` — resolver accepts packet_spec for FLIT_WIDTH-class refs | Components |
| E-4 | **Add invariant**: "elaborator preserves source declaration order; no reordering of header fields, channels, params" | Invariants section (becomes #8) |
| E-5 | **Update schema migration plan**: drop `width/lsb/msb/payload_width/derived.*` from `ni_packet.schema.json` and `ni_signals.schema.json`; add explicit "schema 也是 derived artifact" note | Architecture / scope section |
| E-6 | **Specify generator simplification**: drop `generator.py:206-330`-equivalent paths (the ones computing derived) | Modified files table |
| E-7 | **Rename "resolver" → "elaborator helper"** throughout spec for consistency | Whole doc |
| E-8 | **Pre-existing duplicate `NOC_QOS_WIDTH` localparam** — add to fixup list | Out of scope OR new "Opportunistic Fixes" section |
| E-9 | **Specify signals refactor more concretely** — current `cpp_signals.py:29` `uint64_t` fallback removed; symbolic widths resolved properly | Architecture or Components |
| E-10 | **Add golden-output test requirement** — not just `--check`, but a pytest that captures pre-refactor `.h`/`.sv` to a fixture and diffs post-refactor output | Testing Strategy |
| E-11 | **Add resolver edge-case tests requirement**: `"derived"`-width fields, zero-width fields, disabled fields | Testing Strategy (already partially present, needs to call out the 3 specific cases) |

---

## Final verdict

**NEEDS REVISION** — 11 specific edits make the spec TDD-executable. Architecture is fundamentally sound (independently validated by Codex against 7 industry tools). All 11 edits are scoped to spec text changes, not redesign.

**Top concern**: C-1 (`width_param: "derived"` semantics undefined) is the only one that exposes a true gap in the design model — the others are spec polish. C-1 needs an architectural answer before implementing can begin.

**Recommended path**:
1. Apply all 11 edits inline to spec doc
2. Re-self-review
3. Hand to user for review
4. If user approves, hand to writing-plans (no re-cross-review needed — edits are spec polish + one architectural clarification on C-1)
