# Handoff — noc-sim NI Spec Import

**Date generated**: 2026-05-04
**Context**: Output of `/spec-import` (hw-spec-author plugin v0.2.0, doc-mode) run against the noc-sim public design docs. The user will resume editing on a different machine; this file captures everything needed to pick up cold.

---

## 0. Orient

**This directory** is a brownfield import of the Network Interface (`ni`) spec, restructured from the noc-sim documentation into the OpenTitan-Comportability 6-file layout enforced by the `hw-spec-author` plugin.

**Source material** (original docs):
- Repo: <https://github.com/changlukas/noc-sim>
- Path in repo: `docs/design/`
- Files consulted (in priority order):
  - `04_network_interface.md` — primary
  - `02_flit.md` — flit format, ECC, RoB analysis
  - `06_qos.md` — full NI CSR memory map (this is where the registers actually live)
  - `01_overview.md` — system context, default flit/link parameters
  - `09_verification.md` — verification scope and integration test list
- Files **not** consulted (out of NI scope, may be relevant if scope expands): `00_architecture.md`, `03_router.md`, `05_physical_channel.md`, `08_simulation.md`, `10_width_converter.md`, `noc_model_guide.md`

**Plugin used**: `hw-spec-author` v0.2.0 — <https://github.com/changlukas/hw-spec-author-plugin>. Install on the new machine before resuming:

```
/plugin marketplace add changlukas/hw-spec-author-plugin
/plugin install hw-spec-author@changlukas
```

**Current stage**: pre-D0. The skeleton exists; the designer must close the items below to reach D0, then the plugin's standard workflow takes over (`/spec-status` → Phase 2 iteration → `/spec-review` → `/spec-gate D1`).

**Key existing artifacts in this directory**:
- `IMPORT_REPORT.md` — detailed audit trail; confidence levels, conflict log, TODO inventory, recommended actions
- `README.md` — produced spec summary
- `doc/{theory_of_operation, programmers_guide, interfaces, registers}.md` — produced 6-file layout
- `dv/plan.md` — produced DV plan with 26 testpoints
- `HANDOFF.md` — this file

Every produced spec line that came from the source has an inline `<!-- source: <file> §<section> -->` provenance comment. Grep for `<!-- source:` to trace any value back to its origin without re-running the import.

---

## 0.1 Background — what we are working on (full context)

This NI-spec import is one step in a larger arc. Two parallel things are happening; you may pick up either thread on the new machine.

### Track A — `hw-spec-author` plugin development (the tool)

You are building a Claude Code plugin that authors / restructures / reverse-extracts hardware design specs in OpenTitan-Comportability style. Repo:

- **Local**: `D:\04_Learning\hw-spec-author\files\hw-spec-author-plugin\hw-spec-author-plugin\` (git-tracked)
- **Remote**: <https://github.com/changlukas/hw-spec-author-plugin>

Commit history so far (newest first):

| Commit | Summary |
|---|---|
| `e5c8a7d` | Add `/spec-import`: brownfield entry point with RTL/hjson/doc extraction (**v0.2.0**) |
| `1e53176` | Add bilingual READMEs (English + Traditional Chinese) at top-level and plugin-level |
| `7f84e40` | Address review findings (round 2): wctmr correctness, stable gate anchors, lint precision |
| `4238a3f` | Initial release: hw-spec-author plugin v0.1.0 |

Plugin is currently at **v0.2.0**. Components shipped:

- 1 skill: `hw-spec-author` (workflow engine + 6 templates + 4 process docs)
- 1 subagent: `spec-reader` (isolated-context reader for the reader test)
- 7 slash commands: `/spec-init`, `/spec-import`, `/spec-status`, `/spec-review`, `/spec-lint`, `/spec-gate`, `/spec-help`
- 1 worked example: `wctmr` (64-bit timer; reader-test log shows 10/10 PASS post-fix)

Key process docs the plugin enforces:

- `references/process/stage_gates.md` — D0 → D3 checklists with stable `(id: ...)` anchors; persistent waivers in `WAIVERS.md`.
- `references/process/reader_test.md` — universal + block-specific question banks for `/spec-review`.
- `references/process/writing_principles.md` — audience separation, single source of truth, RTL identifier naming, no fabrication.
- `references/process/rtl_extraction.md` — what is and is not extractable from RTL/hjson, used by `/spec-import`.

If on the new machine you continue working on the plugin itself rather than the NI spec, the open task list is at the bottom of this section under "Plugin-side open items".

### Track B — dogfood `/spec-import` against real material (this directory)

This directory is the **first non-toy use** of `/spec-import`. The intent was twofold:

1. Bootstrap an NI spec for `noc-sim` that you actually need.
2. Stress-test `/spec-import`'s doc-mode on a real codebase (not the contrived `wctmr` example).

The dogfood validated that `/spec-import` works as designed:
- Auto-detection picked doc-mode correctly.
- Audience separation routed `06_qos.md`'s register tables into `registers.md` despite the source organizing them under "QoS and Performance Monitoring" — that is, the classifier worked semantically, not structurally.
- The conflict detector caught **4 source-internal inconsistencies** in noc-sim that designer review had missed.
- Honest accounting: 50% high-conf / 10% medium / 40% TODO. No fabrication.

Track B is currently **paused** at the import-completed state (this directory). The next step on Track B is for the designer (you) to close the 36 TODOs and 4 conflicts, run `/spec-review`, then `/spec-gate D1`. Sections §1 through §6 of this file describe that work.

### What is open on Track A (the plugin itself)

These are not blockers for Track B but are worth noting in case you switch threads:

- **No `marketplace.json` testing on a fresh machine** — the install instructions (`/plugin marketplace add changlukas/hw-spec-author-plugin`) were authored but never tested end-to-end on a clean Claude Code install. First-run on the new machine implicitly tests this.
- **`/spec-import` has not been tested against RTL** — only doc-mode (this exercise) and not yet `.sv` / `.v` mode. If you have any small RTL module handy on the new machine, running `/spec-import path/to/module.sv` would be a useful second dogfood. Particularly relevant: confirm that `rtl_extraction.md`'s heuristics (active-low reset detection from `_ni` suffix, FSM enum extraction, `always_ff` reset value extraction) actually fire as documented.
- **Plugin lint has not been run on its own example** — `/spec-lint plugins/hw-spec-author/examples/wctmr/` should pass cleanly, but it has not been verified after the most recent edits to the wctmr files.
- **Plugin `/spec-status` has not been run on its own example** — same caveat.
- **`spec-import.md` constraint about idempotency** (archive previous `IMPORT_REPORT.md` to `.import_history/<timestamp>.md`) has not been exercised; second run on the same output dir should test it.

### Where Track A and Track B meet

The most likely path forward:

1. (On new machine, Track B) Resolve §2 conflicts in this NI import. Push fixes back to the noc-sim source repo (`docs/design/04_network_interface.md`, `06_qos.md`).
2. (Track B) Author `programmers_guide.md` for `ni`. This is the longest single block of work.
3. (Track B) Run `/spec-review`. Triage gaps. Iterate.
4. (Track B) `/spec-gate D1`.
5. (Track A) The completed `noc-sim-ni/` becomes a second `examples/` entry in the plugin alongside `wctmr`, demonstrating brownfield use. This justifies a `v0.3.0` plugin release.
6. (Track A) Test `/spec-import` against an RTL module to validate rtl-mode.

You can do these in any order; the dependency graph is loose.

---

## 1. Strategic decision (do this FIRST — gates ~10 TODOs)

**Question**: Is this spec going to describe the **RTL implementation** of `ni`, or the **C++ behavior model** of `ni`?

**Why it matters**: The hw-spec-author plugin and its 6-file layout are designed for RTL specs. If `ni` will only ever exist as a C++ behavior model (per `09_verification.md` references to GoogleTest + DPI-C co-sim), then several of the produced TODOs are **not real gaps**:

| If C++ behavior model only | Action |
|---|---|
| `theory_of_operation.md` Resets section | Mark "out of scope: behavioral model has no reset" |
| `theory_of_operation.md` Clock domains | Mark "out of scope" |
| `theory_of_operation.md` Power domains | Mark "out of scope" |
| `interfaces.md` `Resets` table | Mark "out of scope" |
| `interfaces.md` clock signal | Mark "out of scope" |
| `interfaces.md` signal-level wire decomposition | Mark "interface is at typedef bundle level by design" |
| `registers.md` register access width / sub-word policy / unmapped offset policy | Mark "behavior model exposes a function-call API, not a memory-mapped CSR bus" — and consider whether `registers.md` belongs at all |

**If RTL is the eventual target**: every TODO stands as written; no shortcuts.

**Recommendation**: state the decision clearly at the top of `README.md` (e.g. "This spec describes the synthesizable RTL implementation of `ni`. The C++ behavior model in noc-sim is a separate verification artifact.") **before** doing any other work. The decision is a one-liner; the consequences are 3+ hours of work either saved or correctly scoped.

---

## 2. Four source-vs-source conflicts (do these next — 30 min total)

These are **inconsistencies inside the original noc-sim docs**, not import errors. I extracted both sides faithfully; you must adjudicate.

### 2.1 `ERR_STATUS` access mode

- **`06_qos.md §4.1`** declares `ERR_STATUS` access = **RO** (read-only).
- **`06_qos.md §4.4`** says error counters are cleared by **writing 1 to `ERR_STATUS[0]`**.
- **A read-only register cannot be written.** Self-contradictory.

**Fix options**:
- (a) Change access to **RW1C** (write-1-to-clear) — most likely intent.
- (b) Add a separate `ERR_CLEAR` write-only register at a new offset.
- (c) Remove the clear-via-write-1 sentence and add a different clearing mechanism.

**Where to update**: `registers.md` § ERR_STATUS, and the `ERR_COUNT` / `ECC_UNCORR_ERR_CNT` "clear" descriptions. After deciding, also update the source noc-sim repo (`06_qos.md`) so this doesn't regress.

### 2.2 `URGENCY_STEP` register has no offset

- **`06_qos.md §2.4.6`** lists `URGENCY_STEP` as a 2-bit Regulator-mode CSR.
- **`06_qos.md §4.1`** Register Summary table doesn't assign it an offset.

**My inference** (in the produced `registers.md`): consolidate `URGENCY_STEP` into the same register word as `BASE_QOS` at offset `0x018`, occupying bits `[5:4]`. **This is a guess** — if the original intent was a separate offset, my inference is wrong.

**Where to update**: `registers.md` § BASE_QOS field table. After deciding, also update the source noc-sim repo's `06_qos.md §4.1` to add the offset (or merge the register description).

### 2.3 NI clock and reset signals never enumerated

- **`04_network_interface.md` §4** lists `axi_*`, `noc_*`, `id_i`, `route_table_i` in the port enumeration. **No `clk_i`. No `rst_ni`.**
- **`01_overview.md`** implies a single mesh clock (uses `cycles` as the throughput unit, no mention of multiple clocks).

**Fix**: add a Clocks table and a Resets table to `interfaces.md`. Default-most-likely choice (single domain): `clk_i` + `rst_ni` (active-low, synchronous). If AXI side and Router side can run at different frequencies, this is a **CDC point** and synchronizers must be specified — a much bigger deal.

**Where to update**: `interfaces.md` § Clocks and § Resets, plus `theory_of_operation.md` § Resets and § Clock domains and CDC.

### 2.4 Compatibility claim is half-stated

- **`04_network_interface.md §1.3`** says "AMD Versal 風格" (Versal style) for NMU/NSU naming.
- The README must commit to whether `ni` claims **register-set compatibility** with AMD Versal NoC NIU, or only borrows the naming.

**Fix**: in `README.md` § Compatibility, either commit to a specific Versal NIU revision and document the deltas, or replace with "This IP defines a custom NoC interface; NMU/NSU naming follows AMD Versal convention." (the second is much cheaper and probably correct).

---

## 3. TODO inventory by file (priority order)

Total: **36 TODOs** across 6 files.

### 3.1 `doc/theory_of_operation.md` — 8 TODOs (HIGH urgency)

The biggest of these is **Resets section is empty** — D1 cannot pass `/spec-review` without it. Reader test will hit a NOT_ANSWERED on every reset-related question.

Items to fill:
1. **Resets section** — entire section empty. Need: signal name, active level, sync/async, post-reset state of every register, post-reset state of NMU/NSU sub-blocks, behavior under mid-transaction reset, behavior of InjectionBuffer / RoB / `bandwidth_counter` / `urgency_level` after reset.
2. **Clock domains and CDC** — single sentence acceptable if truly single-domain; otherwise full CDC analysis.
3. **Power domains** — single sentence acceptable if no retention / always-on regions.
4. **RoB allocator policy** when multiple `FREE` entries exist.
5. **Tie-breaking** when two RoB entries become `READY_TO_RELEASE` in the same cycle on the same `axi_id`.
6. **RoB behavior when `rob_req = 0`** in the flit header (skip allocation entirely? degenerate stall path?).
7. **Multi-beat R response with one ECC error**: does the entire burst's `rresp` go SLVERR, or only the affected beat?
8. **Explicit "no error condition latches the block"** statement (or enumerate latching cases).

### 3.2 `doc/programmers_guide.md` — 9 TODOs (HIGHEST volume; LARGEST gap overall)

The source documents have **no software-driver-author perspective at all**. ~80% of this file is TODO. This is the single longest block of work.

Items to author from designer knowledge:
1. **Verified initialization sequence** — currently a plausible draft, has not been DV-validated.
2. **All use cases with code fragments** — Single Write, Burst Write with QoS, Performance Probe reading, etc. Source does not have these.
3. **Register-accesses-during-operation** — entire section unspecified. Critical questions: which CSRs are safe to write while traffic is in flight? Are `*_BIN_*_COUNT` reads racy? Is `LAST_ERR_INFO` atomic from the AXI side? Does writing `*_PROBE_EN = 0` immediately freeze counters?
4. **Interrupt handling** — currently states "no top-level IRQ outputs". If a system-level IRQ is added (recommended for ECC uncorrectable / timeout), spec the source set and clearing.
5. **Software reset path** — currently "system reset only". Some NoC NIs offer drain-and-quiesce; decide.
6. **Error recovery procedures** — blocked on §2.1 contradiction (ERR_STATUS RO vs W1C).
7. **CSR bus exposure** — which AXI port exposes the CSR file, at what address window?
8. **Counter clearing semantics** — bin counters, packet probe counters, error counters.
9. **Probe snapshot mode** — `06_qos.md §3.2` mentions "snapshot vs continuous" but no CSR control bit exists for it.

### 3.3 `doc/interfaces.md` — 8 TODOs (medium effort, mechanical)

1. **Signal-level decomposition** of `axi_in_*`, `axi_out_*` — currently at the bundle/typedef level. AXI4 spec is the reference; this is mechanical work.
2. **Signal-level decomposition** of `noc_req_*` / `noc_rsp_*` — `valid` (1b), `ready` (1b), `flit` (`FLIT_WIDTH` bits) per direction.
3. **Resets table** — see §2.3 above.
4. **Clocks table** — see §2.3 above.
5. **Parameter constraints** — most `TODO(designer): range` markers can be filled in 5 minutes each (most are common-sense bounds: `ADDR_WIDTH ∈ {32, 64}`, etc.).
6. **NMU/NSU enable rule** — confirm "at least one of `EN_MGR_PORT` / `EN_SBR_PORT` must be true".
7. **`route_table_i` width formula** — depends on `NUM_SAM_RULES` and the route entry format.
8. **`id_i` strap-vs-live semantics** — one-shot at boot, or rebindable during operation?

### 3.4 `doc/registers.md` — 6 TODOs (mostly mechanical after §2 conflicts resolved)

1. **All RW reset values** — currently every RW register has reset = `TODO(designer)`. Most should sensibly default to `0x00000000`; `QOS_MODE` defaults to `0` (Bypass).
2. **Register access width statement** — assumed 32-bit but not stated in source.
3. **Sub-word access policy** — typically "byte/half-word writes return PSLVERR".
4. **Unmapped offset policy** — typically "returns PSLVERR".
5. **Counter saturation behavior for `*_BIN_*_COUNT`** — should be saturating per the convention used by `ECC_UNCORR_ERR_CNT`.
6. **`LAST_ERR_INFO` update semantics** — every error or only first since clear?

### 3.5 `dv/plan.md` — 4 TODOs (decisions, low effort once made)

1. **Testbench methodology choice** — UVM 1.2 / cocotb / plain SV. Recommendation: UVM 1.2.
2. **FPV scope** — recommend FPV for the per-RoB-entry FSM.
3. **Credit-Based mode testpoints** — if `ni` will be built in Credit-Based config, add credit-tracking TPs.
4. **ABV spec-vs-implementation classification** — mark each ABV assertion in the existing list.

### 3.6 `README.md` — 1 TODO

1. **Compatibility section** — see §2.4 above.

---

## 4. Recommended order of work

| # | Task | Estimated effort | Blocks |
|---|---|---|---|
| 1 | Resolve **Strategic decision** (§1): RTL or behavior model? | 5 min | Everything |
| 2 | Resolve **conflict §2.1** ERR_STATUS access | 5 min | programmers_guide error recovery |
| 3 | Resolve **conflict §2.2** URGENCY_STEP offset | 5 min | registers.md BASE_QOS layout |
| 4 | Resolve **conflict §2.3** clock/reset enumeration | 10 min | most of theory_of_operation §Resets |
| 5 | Resolve **conflict §2.4** Compatibility | 5 min | README finalization |
| 6 | Fill **theory_of_operation.md §Resets** completely | 1.5 h | reader test pass |
| 7 | Fill **interfaces.md** Resets and Clocks tables | 15 min | (depends on §4) |
| 8 | Decompose bus interface bundles in **interfaces.md** | 1 h | reader test pass |
| 9 | Specify register reset values in **registers.md** | 30 min | reader test pass |
| 10 | Specify register access policies (sub-word, unmapped, access width) in **registers.md** | 15 min | — |
| 11 | Author **programmers_guide.md** in full | **3–4 h** | reader test pass |
| 12 | DV plan: pick testbench methodology and FPV scope | 30 min | — |
| 13 | Run `/spec-status .` from this directory | 1 min | gauges progress |
| 14 | Run `/spec-lint .` to catch import drift | 5 min | — |
| 15 | Run `/spec-review .` for the reader test | 30 min | D1 sign-off evidence |
| 16 | Triage gaps from reader test, iterate | varies | — |
| 17 | Run `/spec-gate D1` | 5 min | formalizes D1 |

**Critical path**: 1 → 6 → 11. Total minimum to D1: **~7–8 hours of focused work**, dominated by `programmers_guide.md` authoring.

The `IMPORT_REPORT.md` "Honest scope statement" (last section of that file) reaches the same conclusion via a different path and adds the warning that if this is a behavior model, ~3 hours of items 6–8 disappear.

---

## 5. Cross-references back to noc-sim source

When updating the produced spec, **also push corrections back to the source noc-sim docs** for the items that are objective bugs (the four conflicts in §2). Otherwise the next person to import will hit the same issues.

Source-doc → spec-file mapping for items most likely to need synchronization:

| Source file | Sections whose content lives in… | Anticipated bidirectional sync |
|---|---|---|
| `04_network_interface.md` §3 | `interfaces.md` § Parameters | Likely changes both ways |
| `04_network_interface.md` §4 | `interfaces.md` § Bus interfaces (after wire-level decomposition) | Source likely upgraded too |
| `04_network_interface.md` §5 FR-05 RoB | `theory_of_operation.md` § Control/FSM | Author RoB allocator policy in both |
| `06_qos.md` §4 | `registers.md` (entire register map) | After resolving conflicts §2.1, §2.2 |
| `02_flit.md` §3 (payload formats) | `theory_of_operation.md` § Datapath references | Probably unchanged |
| `09_verification.md` §3.1 | `dv/plan.md` § Testpoints | One-way: source → spec |

---

## 6. Plugin commands to run on the new machine

After install (see §0), from this directory:

```
# 1. See current state
/spec-status .

# 2. Catch any cross-reference drift
/spec-lint .

# 3. After closing the high-priority TODOs, run the reader test
/spec-review .

# 4. Once reader test passes 8+/8+, formalize
/spec-gate D1 .
```

Expected: `/spec-status` will currently report **pre-D0** because the README has a `TODO` (the Compatibility section). Resolving §2.4 should bump to D0 immediately; from there it's an iterative climb to D1.

---

## 7. What this exercise validated about the plugin

Independent of the noc-sim spec work, this run validated `/spec-import` itself on a real, non-trivial codebase:

- **Doc-mode auto-detected correctly** when only `.md` files were available.
- **Audience separation** correctly routed register tables from `06_qos.md` into `registers.md` even though the source organizes them under "QoS and Performance Monitoring" (semantic, not structural, classification).
- **Conflict detection** caught 4 source-internal inconsistencies that designer review of the original docs had missed.
- **Provenance comments** make it possible to trace every extracted value back to its origin in 3 keystrokes (`grep` for `<!-- source:`).
- **Honest TODO accounting**: 36 explicit TODOs, no silent fabrication. The IMPORT_REPORT's percentage breakdown (50% high-conf / 10% med / 40% TODO) is reliable rather than aspirational.

This is roughly the rate at which a brownfield import should look honest — if any of the percentages had been ≥80% confident or ≤10% TODO, that would have indicated either a much better source (rare) or fabrication (common).

---

## 8. Final note

Keep `IMPORT_REPORT.md` and `HANDOFF.md` together; do **not** commit them into the noc-sim repo when (if) you eventually move the produced spec there. They are import-time artifacts, not spec content. After D1 is reached, you can delete both files — the `<!-- source: -->` provenance comments inside the spec are sufficient for ongoing audit.
