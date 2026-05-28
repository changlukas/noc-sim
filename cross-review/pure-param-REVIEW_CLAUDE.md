# Pure Parameterization Refactor — Independent Spec Review (Claude, fresh context)

Target: `docs/superpowers/specs/2026-05-28-pure-parameterization-design.md`
Reviewed: 2026-05-28
Lens: senior software architect, no hedging.

---

## 1. Ambiguity

A subagent cannot TDD-execute this spec as written. Concrete gaps:

| # | Gap | Why it blocks execution |
|---|---|---|
| A1 | Resolver namespace seam between packet and signals is undefined | `port_parameters` of the NoC interfaces references `FLIT_WIDTH` (`generated/ni_signals.json:489-493, 547-552, 1016-1020, 1521-1525`) but `FLIT_WIDTH` lives in `flit.derived` today, which the refactor drops (`Scope` table row 1). The spec lists `signal_eval_expr(spec, interface, expr)` but does not say whether `spec` here is `signals_spec`, `packet_spec`, or a bundle. Without that, the implementer cannot decide which expressions are legal in `width_param`. |
| A2 | `width_param: "derived"` semantics are unspecified after refactor | `aw_rsvd / ar_rsvd / w_rsvd / b_rsvd / r_rsvd` carry the literal string `"derived"` (`generated/ni_packet.json:212, 296, 338, 380, 429`). Today the generator computes their width from `msb - lsb + 1`. Post-refactor there is no `msb`. The spec is silent on whether `"derived"` becomes (a) `payload_width - sum(other field widths)`, (b) an explicit symbolic width like `AW_RSVD_WIDTH` added to `field_widths{}`, or (c) something else. Section 2 brainstorming-deferral does not cover this — it is a data-model question, not a naming question. |
| A3 | Per-channel `payload_width` source is unspecified | Currently `ni_packet.json:138, 222, 306, 348, 390` store `payload_width: 108/108/352/64/352`. These are needed to resolve `"derived"` widths AND drive `namespace payload { constexpr int AW_WIDTH = 108; ... }` in `ni_flit_constants.h:84-90`. Spec Invariant 1 says drop "`width / lsb / msb / derived.*` 等可從 `field_widths` 推得的值" — but `payload_width` cannot be derived from `field_widths{}` alone (you need `max()` across channel field-width sums, and one of those sums depends on `"derived"`, see A2). Either `payload_width` survives in the JSON or the resolver needs a fixed-point algorithm. Spec picks neither. |
| A4 | Schema migration scope is hand-waved | Section 3 lists `generated/ni_packet.schema.json` and `ni_signals.schema.json` as needing "Drop resolved field requirements" — but the current packet schema (`ni_packet.schema.json:54`) has `"required": ["name", "width_param", "width", "lsb", "msb"]`. Drop set is incomplete: removing `width / lsb / msb` from `required` is necessary but not sufficient — `additionalProperties: true` currently lets stale fields through silently. Spec should require `additionalProperties: false` to enforce symbolic-only, or it weakens Invariant 1. |
| A5 | "Resolver caching strategy left to implementer" but call-site count not bounded | A naive resolver re-evaluates every `header_field_position(spec, name)` call. The elaborator iterates 12 header fields + 5 channels × ~10 fields = ~65 position lookups for one elaboration; per-test fixtures can run codegen 5-10× per pytest session. Without an O(1)-after-first-resolve contract, byte-identical re-elaboration in `--check` mode may slow down detectably. Implementer needs a perf budget or an explicit "memoize per `spec` dict" instruction. |
| A6 | Iteration order for `field_widths` namespace is not pinned | Current generator at `generator.py:258-276` builds `field_widths{}` by traversing MD groups 1, 2, 3, 5 in order. Python ≥ 3.7 dict iteration is insertion-ordered, and `cpp_packet.py:87` / `sv_packet.py:61` iterate the dict directly to emit `namespace width` constants (line 94-119 of `ni_flit_constants.h`). After refactor the resolver may build the namespace differently (e.g. lazy keys); spec does not state "iteration order MUST match generator's original insertion order" — but it is load-bearing for byte-identical. |
| A7 | `enabled` field bit-position accounting is verbally specified, not algorithmically | Invariant 5 says "disabled fields 仍占 bits, 後續 field 的 cumulative lsb 不受影響". Current generator does NOT compute cumulative lsb at all — it parses literal bit ranges from MD (`generator.py:124-165`). Spec implies new resolver does cumulative computation. But MD §2.1 today happens to have cumulative ranges. Spec must say (a) authoritative source is the MD bit ranges, OR (b) MD bit ranges are about to be removed and resolver becomes the source. Today both are alive; tomorrow ambiguous. |
| A8 | "ast safe-walk" allowed-node list lacks `ast.Num` vs `ast.Constant` clarity | Python 3.8 deprecated `ast.Num`; Python 3.12 made `ast.Constant` the only numeric node. Spec says "numeric literal" without naming the node class. Implementer needs to know to whitelist `ast.Constant` (with `isinstance(node.value, int)`) and reject `ast.Constant` with `str/float/bool`. Not blocking but a TDD subagent will guess. |

**Verdict for "TDD-executable?"**: not yet. A1, A2, A3 are spec-level holes the user must resolve before plan-writing. A4-A8 are guidance gaps an implementer can paper over but the byte-identical gate may catch them late.

---

## 2. Completeness

Missing sections / contracts:

| # | Missing | Notes |
|---|---|---|
| C1 | **Migration order** | Not specified. Generator and elaborator must change atomically with regen-of-JSON, otherwise intermediate commits break `--check`. Implementer needs ordering: e.g. (1) build resolver API alongside old constants, (2) flip elaborator to resolver API while JSON still has both, (3) shrink JSON. Spec says nothing about which domain to migrate first. Packet-first is correct (signals depends on `FLIT_WIDTH`) but spec doesn't say it. |
| C2 | **Rollback strategy** | None. Acceptance criterion A is binary (`--check` exits 0 or not). No instruction for what to do if a post-refactor regen produces a 1-line diff in `ni_flit_constants.h` — revert? bisect? attempt to make new output match old? Given the spec calls this the "main acceptance axis", the absence of a "what if it fails" branch is glaring. |
| C3 | **Test-rewrite triage rules** | Section "Other `tests/*.py`" says "asserts via resolver API where they accessed dict directly". No grep pattern given. A subagent will miss things. Real list of in-tree dict-access sites that need rewriting (from my walk): `tests/test_foundation.py:65-69` (asserts `"enabled" in f`, `isinstance(f["enabled"], bool)`), `tests/test_signals_schema.py:36-40` (asserts `"pin_name" in sig`). Spec doesn't enumerate. |
| C4 | **JSON schema validation** | Mentioned briefly ("Drop resolved field requirements") but not specified. No bullet on adding `"required": ["width_param"]` symmetric to removing `"width"`. No bullet on tightening `additionalProperties` to enforce purity. Schema is half the contract. |
| C5 | **ast safe-walk visitor algorithm** | The spec gives an allow-list of node types and a deny-list, but does not specify the visitor pattern, error context (where in expression did parse fail), or the `ns: dict[str, int]` lookup semantics (KeyError vs `ExprNameError`). Section "Components" punts on this ("expression parser implementation left to implementer"); given safety is mentioned in Invariant 4, this should be a one-screen reference algorithm. The cost of half-baking it: `ast.Constant.value` of type `bool` is also `isinstance(int)` in Python — implementer must explicitly reject `True/False` numeric literals or risk allowing `True + 1`. Not mentioned. |
| C6 | **Operator precedence** | Allow-list says `+ - * // % parens unary +/-`. ast.parse handles precedence natively, so a tree walk is fine. But: `X_WIDTH + Y_WIDTH` (today's only multi-token case, `ni_packet.json:55, 63`) is `BinOp(Add)`. The spec asserts "Current spec MD has only `+` expressions in `width_param` (grep verified)". I grep-confirmed this against `generated/ni_packet.json` (only `X_WIDTH + Y_WIDTH`). But the resolver must also handle future expressions in signal `width_param` — and `port_parameters[].constraint_text` carries non-trivial expressions like `1 ≤ x ≤ 8` (`ni_signals.json:486`). Spec is silent on whether `constraint_text` is in scope for evaluation. (My read: it isn't — it's prose. But say so.) |
| C7 | **Per-channel payload-width algorithm** | See A3. Worth listing twice because it is both an ambiguity AND a missing contract. |
| C8 | **`generator.py` simplification details** | "Drop pre-resolve helpers; only parse MD → raw structure" is one line. Concretely the spec means: `_parse_bit_range` stops being called, `_parse_payload_section` stops computing `lsb/msb`, `parse_derived` stops being called. But `parse_field_widths` MUST stay. Generator currently uses MD bit ranges to detect tile errors at parse time — that error checking moves to validator. Spec should say so explicitly. |

---

## 3. Cross-file consistency

### Claims verified against worktree state

| Spec claim | Reality | Status |
|---|---|---|
| `field_widths{}` exists in JSON top-level | `ni_packet.json:8-35` has `flit.field_widths` with 25 entries | OK (note: under `flit.`, not top-level — spec is loose but obvious) |
| `port_parameters[]` per-interface in signals | Confirmed at `ni_signals.json:426, 481, 540, 955, 1008, 1459, 1514` | OK |
| `derived.*` fields all eliminable | **NOT TRUE** — see below | FAIL |
| Elaborator reads `f["width"]` etc. | Confirmed at `cpp_packet.py:54-58, 65-72, 87, 104-110`; `sv_packet.py:34-37, 43-52, 61` | OK |
| Validator's `_resolve_width_param` does the eval | Confirmed at `invariants.py:81-89` (uses `eval()` not ast safe-walk) | OK; note current code uses bare `eval()` which is the unsafe path spec rightly replaces |

### `derived.*` elimination — verified against consumers

| `derived.*` field | Consumer reads | Eliminable post-refactor? |
|---|---|---|
| `FLIT_WIDTH` | `cpp_packet.py:50` via `C.flit_width()`; `sv_packet.py:30`; `signals.NOC_REQ_OUT.port_parameters[FLIT_WIDTH]` cross-ref (`ni_signals.json:489-493`); `c_model/include/flit.hpp:12` `ni::FLIT_WIDTH = ni::FLIT_WIDTH` symbol | YES if resolver recomputes from header sum + max payload |
| `HEADER_WIDTH` | `cpp_packet.py:51`; `sv_packet.py:31` | YES — sum of header field widths |
| `PAYLOAD_WIDTH` | `cpp_packet.py:52`; `sv_packet.py:32` | YES — `max(payload_channel widths)`, but requires resolving `"derived"` (A2) |
| `LINK_WIDTH` | `cpp_packet.py:53`; `sv_packet.py:33` | YES — `FLIT_WIDTH + 1` |
| `FLIT_DATA_WIDTH` | `cpp_packet.py:54-58, 104-110` (static_assert), `sv_packet.py:35` | YES if `HEADER_DATA_WIDTH + PAYLOAD_WIDTH` formula encoded |
| `HEADER_DATA_WIDTH` | `cpp_packet.py:54-58`, `sv_packet.py:35` | YES — `HEADER_WIDTH - FLIT_ECC_WIDTH` |
| `WSTRB_WIDTH` | `cpp_packet.py:55-58`; `sv_packet.py:35`; also lives in `field_widths{}` indirectly via `width_param "WSTRB_WIDTH"` on the wstrb field | **Suspicious overlap** — `WSTRB_WIDTH` appears both in `derived{}` (`ni_packet.json:438`) AND is the `width_param` of `wstrb` (`ni_packet.json:331`). The new resolver must put it in the `field_widths{}` namespace, OR `wstrb` width fails to resolve. Spec implies the latter but does not say which. |

**Verdict**: `derived.*` are all algorithmically eliminable, **but only after A2/A3 (the `"derived"` width_param case) is resolved**. Spec's confident "drop derived.*" elides the circular dependency.

### Schema requirements vs spec intent

`ni_packet.schema.json:54` lists `"width", "lsb", "msb"` as required. After refactor these must move OUT of required AND `width_param` must move IN. Spec sketches this but does not state the symmetric add. Schema is data-contract; "tighten" without specifying = footgun.

### Other inconsistency I found while reading code (not directly spec's concern but worth flagging)

`ni_flit_pkg.sv:23` and `ni_flit_pkg.sv:80` both declare `localparam ... NOC_QOS_WIDTH` in the same SV package — once under "header field" iteration (because noc_qos width=0 falls through to the WIDTH-only branch in `sv_packet.py:44-47`), once under "all field widths". This is a duplicate localparam in the same package — illegal SV (LRM §6.20). It compiles today only because most simulators tolerate duplicate-same-value redefinitions or no one has tried `vcs -kdb` on it. Not the refactor's bug, but the refactor regen will reproduce it. Worth fixing in the same PR (one-line: skip width=0 entries in the header_fields loop OR rename one to `header_NOC_QOS_WIDTH`).

---

## 4. Risk analysis

Ordered by under-emphasis × severity:

### R1 (HIGHEST) — Byte-identical claim is fragile on `field_widths` ordering

The spec calls byte-identical the "acceptance criterion 主軸" (Invariant 3). Current header (`ni_flit_constants.h:94-119`) emits `namespace width { ... }` in insertion order of `flit.field_widths{}` dict. After refactor, a "cleaner" resolver might `sorted()` the keys for determinism, instantly breaking byte-identity. Spec mentions byte-identical four times but never says "iteration order is part of the contract".

Fix: add Invariant — "all dict iteration that drives elaborator output MUST preserve `generated/*.json` insertion order; resolver MUST NOT `sorted()` its output keys".

### R2 — `"derived"` width_param round-trip

Already covered in A2. If the resolver doesn't reproduce the exact same `aw_rsvd` / `b_rsvd` widths (3, 3, 55, 44, 77), the `PaddingFieldPos PADDING_FIELDS[]` array at `cpp_packet.py:16-34` could change, breaking byte-identical even though packet header bits don't shift. Actually wait — PADDING_FIELDS only includes `enabled=false`, but the `*_rsvd` payload fields are not in header_fields at all. False alarm; mentioning because the relationship between `derived` width_param, `enabled` flag, and PADDING_FIELDS deserves to be explicit. Spec gives no list of which fields fall into which bucket.

### R3 — ast safe-walk semantic divergence from current `eval()`

`invariants.py:25-28` uses `eval(str(expr), {"__builtins__": {}}, dict(ns))`. This handles `X_WIDTH + Y_WIDTH` correctly. An ast safe-walk that whitelists `+` will also handle it. But subtle: Python `eval("3 + 4")` returns `int`. ast walk for `BinOp(Add, Constant(3), Constant(4))` should also return `int`. **However** the current `eval()` also handles things like `8 - 0` if a future MD adds it. The spec's allow-list includes `-` so that survives. Spec correctly closes the gap.

Real risk: order of operations on negative numbers. `eval("-5 + 3")` = `-2` (UnaryOp). ast safe-walk needs to evaluate `UnaryOp(USub, Constant(5))` then `BinOp(Add, -5, 3)`. If implementer forgets `UnaryOp` (even though spec mentions "unary +/-"), `eval()`-vs-safe-walk diverge on a future MD entry. Low probability today (no negatives in MD); spec covers it; flagging because regression is silent.

### R4 — Multi-pass interaction ordering

Generator → JSON → validator → resolver → elaborator. If validator runs BEFORE elaborator (it does, per `tools/codegen.py --check`) AND validator's `check_flit_arithmetic` uses the new resolver to recompute things, then validator catches "expr can eval" failures before elaborator crashes. Good. But: validator also asserts "derived.HEADER_WIDTH == sum-of-widths" today (`invariants.py:140-156`). After `derived.*` is gone, those assertions need replacement, not just deletion. Spec line 101 ("simplify — drop 'stored vs computed' checks; add 'expr can eval' + 'tiling consistent'") acknowledges this but does not enumerate the 5 replacement checks. Risk: validator coverage shrinks silently.

### R5 — Cumulative position determinism with `enabled=false` width=0

`noc_qos` has width=0 AND enabled=true (`ni_packet.json:39-44`). It is a placeholder. Current resolver at `constants.py:39-48` returns `(None, None)` for it. If the new cumulative position algorithm "skips" zero-width fields (natural), it does no harm because position += 0. But if it "skips" enabled=false fields (incorrect per Invariant 5), bit positions shift. Spec correctly stating Invariant 5 mitigates this. Risk: implementer reads Invariant 5 once, writes `if not f.get("enabled"): continue`, ships, byte-identical breaks at `flit_ecc` position. TDD needed: add unit test "disable a wide field, assert subsequent field position unchanged".

### R6 (LOW but worth flagging) — sv_packet duplicate localparam

Already discussed above. Pre-existing bug; refactor will faithfully reproduce; spec should explicitly call this out as "known issue, NOT in scope" or "fix opportunistically".

---

## 5. Industry comparison

I have seen this pattern in production. Anchor points:

| Tool | Pattern alignment | Difference |
|---|---|---|
| **SystemRDL / PeakRDL** (Accellera 1685-2020) | Strong match. RDL source files are symbolic (`field { sw=rw; hw=r; } status[3:0];`); the `peakrdl regblock` compiler resolves to concrete bit positions per instantiation. Compiled output is deterministic and re-running produces byte-identical SV/UVM. | RDL has a real grammar and `Compiler` class with explicit "elaborate" phase that materialises instances. This spec's `constants.resolver` is the rough analog. The byte-identical contract is implicit in PeakRDL (driven by generator templates); they don't make it an explicit invariant the way this spec does. |
| **IP-XACT 1685-2014** | Match in intent (XML registers + symbolic ports); diverges in tooling (IP-XACT is verbose, vendor-specific code-gen). | IP-XACT keeps resolved + symbolic together (`field.bitOffset` + `field.bitWidth` AND parameter references). Closer to current "denormalized" state than to the refactor's goal. Worth referencing as "what NOT to do" rather than as a model. |
| **Protocol Buffers `.proto`** | Loose match — `.proto` is symbolic, `protoc` elaborates to language-specific code. The byte-identical-output-after-refactor pattern is the gold standard there (Google CI rejects PRs where re-elaborating diverges from committed). | Protobuf has no equivalent of cumulative bit positions; the analogy is shallow. Cite for "elaborated-output-as-build-artifact" discipline only. |
| **TableGen (LLVM)** | Closest cousin. `.td` files are symbolic; `llvm-tblgen` resolves and emits C++ headers. CI gates ensure regenerated headers match committed. | TableGen has more expressive language (records, multiclasses) than this design needs. Cite for "DSL-driven elaboration is a viable architecture if scope grows". |
| **OpenAPI** | Doesn't fit — OpenAPI is interface description, not bit-level. | Skip. |
| **Linker scripts** | Wrong analogy. Linker scripts express layout intent, but layout is resolved by the linker, not pre-baked. | Skip. |

### Naming-convention conflicts I noticed

- `resolver` is generic and overloaded in compiler tooling (Python's `importlib.resources`, SQLAlchemy, GraphQL, DNS). PeakRDL calls the equivalent the **elaborator**, which the c_model spec series already standardised on (`scope-correction-design.md` Invariant 3, `c-model-bootstrap-design.md` Invariant 3). The pure-param spec calls it "resolver" in Section "Components" header. Pick one term — "elaborator" is the established choice and matches industry (PeakRDL, TableGen). Rename the API from `constants.*` → `elaborate.*` or at minimum drop "resolver" terminology in favor of "compute" / "evaluate".

- `header_field_position()` returning `tuple[int, int] | None` — Python convention is `Optional[Tuple[int, int]]` and SystemRDL convention is to return a structured object (`FieldNode.lsb`, `.msb`). A tuple is fine for two ints, but four months later when someone wants to add `enabled` to the return, it's a breaking change. Consider returning a small dataclass. Not blocking.

### Pattern absent from spec that industry uses

PeakRDL has an **elaborated AST** (`SystemRDLCompiler.elaborate()` returns a `RootNode`) — a one-shot resolved tree, separate from the symbolic source. The pure-param spec by contrast keeps the JSON symbolic and resolves lazily on each accessor call. Both work; the lazy path is simpler but requires the caching strategy A5 calls out. If perf bites, the spec should leave room for a "build resolved view once, return cached lookups" implementation (i.e. don't ban memoisation).

---

## 6. Verdict

**NEEDS REVISION**.

Confidence: **high**.

### Required edits before plan-writing (rank-ordered)

1. **Resolve A2 + A3 (the `"derived"` + `payload_width` question)** — these are data-model holes; the spec cannot be implemented without picking one of the two paths.
2. **Add Invariant — iteration order preservation** (R1). One sentence: "Resolver MUST iterate `flit.field_widths{}` and `flit.header_fields[]` in their JSON insertion order; no `sorted()` allowed on outputs that feed the elaborator."
3. **Specify the validator's replacement check set** (R4). Enumerate the ≥5 checks that survive: (i) expr can ast-parse, (ii) expr names resolve in namespace, (iii) tiling: cumulative widths leave no gap, (iv) field widths consistent with `field_widths{}` value, (v) `route_par_coverage` names exist. Today's spec says "add tiling consistent" — too sparse.
4. **Schema delta** (C4). Make explicit: `width_param` becomes required; `width / lsb / msb / default / derived` removed from required AND from allowed (set `additionalProperties: false` on field-level objects).
5. **Migration order** (C1). Packet first, then signals; resolver added before generator changes; JSON shrinks last.
6. **Cross-domain namespace** (A1). Decide: `signal_eval_expr` either takes the union namespace (packet field_widths + signals port_parameters) or takes only signals-local symbols. Today `FLIT_WIDTH` is referenced from signals — make that resolution path explicit.

### Edits worth doing but not blocking

- A5 caching guidance (one sentence)
- C6 confirm `constraint_text` is out of scope for eval
- A8 confirm `ast.Constant(value: int)` is the only allowed numeric form
- R6 sv_packet duplicate localparam — flag as "fix-in-pass" or "known issue"
- Industry vocabulary alignment: prefer "elaborator" / "elaborate" over "resolver" (matches existing project terminology)

### Top concern (one line)

**The byte-identical acceptance gate hides three open data-model questions (`"derived"` width_param, per-channel `payload_width` source, cross-domain `FLIT_WIDTH` reference) that no implementer choice can paper over without freezing them in spec first.**
