# noc-sim NI BFM — Implementer Review Log

This log substantiates the `D1.cross.implementer_review` claim for `E:/03_Learning/noc-sim/spec/ni/`. Per `stage_gates.md`, a `protocol-bfm + has-rtl-counterpart=yes` spec at D1 must have run an implementer review with at least 2 paradigm-paired reviewers. The converged ambiguity list must be either resolved in spec or recorded in `WAIVERS.md`.

**Run timestamp**: 2026-05-10
**Plugin version**: 0.3.1 (run executed manually as dogfood validation; `/spec-implementer-review` command itself landed in this run's plugin source)

## Paradigms

- **`c-bfm`**: senior verification engineer building a C++ / SystemC BFM. Wire-level equivalence with parallel SystemVerilog RTL is the contract.
- **`rtl`**: senior RTL designer building synthesizable SystemVerilog. Wire-level equivalence with parallel C++ / SystemC BFM is the contract.

## Round 1 — independent reads

Both agents read the spec in `E:/03_Learning/noc-sim/spec/ni/` (12 files: `MODE.md`, `README.md`, `doc/theory_of_operation.md`, `doc/signal_interface.md`, `doc/pin_level_reset.md`, `doc/protocol_rules.md`, `doc/channel_handshake.md`, `doc/transaction_api.md`, `doc/channel_api.md`, `doc/active_passive_mode.md`, `doc/registers.md`, `dv/plan.md`).

Full transcripts of Round 1 outputs are in this session's conversation log (2026-05-10). The summary below captures each agent's key findings.

### `c-bfm` — Round 1 summary

**Top-level architecture**: `class Ni` owning `Params`, `ConfigStore`, `ErrLogger`, `Sequencer`, NMU + NSU halves split per clock domain (`NmuAclk` / `NmuNoc`, `NsuNoc` / `NsuAclk`), two `CdcFifo` instances, `CsrPort`, `Probes`, `ApiSurface`. PASSIVE mode = all `*_o` drivers held at `pin_level_reset.md` during-reset values; monitors + `irq_o` continue.

**Key implementation decisions**: two-phase per-cycle scheduler (aclk → noc_clk, sub-phases sample → compute → drive → register-update); flit modeled as packed byte array with `FlitView` accessor; SECDED static lookup table (Hsiao variant); RoB as `std::array<RobEntry, MAX_TXNS>` with intrusive linked list + `prev_dest` map; Wormhole arbiter floo_wormhole_arbiter-equivalent (LockIn=1); credit counters per-VC with bi-directional init handshake; AXI-Lite CSR access with sub-word/misalign/unmapped strict response; PASSIVE force every `*_o` to during-reset value within 1 cycle.

**Round 1 ambiguities raised** (6):

1. **Flit-header bit layout** — described prose-only; SECDED, route_par, every per-field accessor depends on exact bit positions; tentative resolution: jointly publish `02_flit.md` field map.
2. **Hamming SECDED vs Hsiao SECDED** — `theory_of_operation.md` §ECC says "Hamming"; `transaction_api.md set_inject_ecc_error` says "Hsiao"; tentative: implement Hsiao.
3. **`INPUT_BUFFER_DEPTH`** described as router-side parameter, no NI port exists; tentative: BFM construction parameter, default 4.
4. **ECC counter widths** — only `ECC_CORR_ERR_CNT` width is `ERR_COUNTER_WIDTH=16`; other 3 counters not pinned; tentative: all four are 16-bit in `[15:0]`.
5. **Wormhole RR pointer initial value + advancement rule** not pinned post-reset; tentative: resets to 0, advances by 1 per granted cycle.
6. **ACTIVE→PASSIVE "within 1 cycle" deadline** counted in aclk vs noc_clk; tentative: per-domain on next clock edge, not synchronised.

**Wire-level lockdowns proposed**: 10 specific cycle-counts / bit-positions to confirm with RTL team (AW→AW-flit injection latency; W-burst accumulation timing; RoB allocator priority direction; credit-init epoch cycle; route_par drop semantics; axi_rdata_par_o for 2-bit ECC; CDC FIFO partial-reset behavior; PENDING decrement timing; NSU back-pressure direction; ERR_STATUS RW1C atomic-clear cycle).

### `rtl` — Round 1 summary

**Top-level architecture**: `ni_top` owning `csr_file` (aclk), `err_irq_aggregator` (aclk), `nmu` (mixed clocks), `nsu` (mixed clocks), probes. NMU sub-modules: 18 leaf modules covering AXI ingress, address translation, QoS gen, upsize, flit pack, RoB, ECC gen / check, route_par gen / check, VC arb / demux, injection buffer, credit ctrl, CDC FIFOs, pending counters. NSU symmetric mirror plus MetaBuffer, Exclusive Monitor, R-response buffer, downsize.

**Key implementation decisions**: CSR + IRQ at `ni_top` (single instance); VC arbiter is `rr_arb_tree`-style with `LockIn=1`; drop on route_par mismatch with no AXI synthesis; on flit_ecc double-bit forward corrupted with `OKAY`; R-RoB flop array at `MAX_TXNS≤32`, SRAM macro at `MAX_TXNS≥64`; `NMU_BUFFER_DEPTH=2` per VC; `axi_rdata_par_o` regenerated post-flit_ecc-check; `quiesce_idle` combinational over latched aclk; RoB allocator lowest-FREE-index-first; SAM compile-time parameter, first-match-wins.

**Pipeline / timing**: `noc_clk_i` 1.2 GHz, `aclk_i` 800 MHz, ASIC 7nm. CUT_AX=0/1 → 1/2 cycles, CUT_RSP=0/1 → 1/2 cycles. CDC: gray ptr + 2FF, depth 16. flit_ecc decode registered. route_par combinational drop.

**Round 1 ambiguities raised** (9):

1. **Flit format / field offsets** not in `spec/ni/` (in `noc-sim/docs/design/02_flit.md`).
2. **NSU MetaBuffer indexing** — slot vs `axi_id`-keyed vs rob_idx-mirrored; tentative: slot-keyed, `(rob_idx, src_id)` lookup.
3. **`NMU_BUFFER_DEPTH=2` vs `CDC_FIFO_DEPTH=16`** relationship; tentative: different stages.
4. **Hamming vs Hsiao SECDED**; tentative: Hsiao (per dv/plan + test-knob references).
5. **`NOC_VC_MAPPING_HYBRID_RW_QOS` exact mapping function** for NUM_VC=4/8; tentative: bit-extract qos[3] for NUM_VC=4, qos[3:2] for NUM_VC=8.
6. **Address parity regeneration when NMU rewrites address** — definition of "modify" ambiguous; tentative: byte-by-byte differing regenerated.
7. **`rob_req` decision policy** — `NOC_FLIT_HDR_ROB_REQ_GEN` doesn't specify; tentative: tied to `R_ROB_TYPE`/`B_ROB_TYPE`.
8. **CDC `flush_on_full_reset` trigger condition** — "both resets eventually align" undefined; tentative: 2FF-sync sticky observation.
9. **`axi_rready_o = 1` "always-ready while in reset to drain"** — what happens to slave-driven B/R during reset; tentative: lost.

**Wire-level lockdowns proposed**: 10 specific behaviors for cross-team confirmation (flit_ecc encode bit pattern; route_par polarity; vc_id mapping function; wormhole-lock release cycle; credit-init epoch; PENDING counter timing; EXCLUSIVE_MONITOR_CTRL.clear_all race; LAST_ERR_INFO sticky update; reset-during-burst behavior; axi_rdata_par_o regeneration on 2-bit ECC error).

## Round 2 — cross-paradigm peer review

Each agent received the OTHER agent's Round 1 output and produced a peer review. Full transcripts in conversation log (2026-05-10).

### `c-bfm` reviewing `rtl` — summary

**Agreement** (7 points): CSR + IRQ at ni_top; FlooNoC wormhole arbiter; route_par drop semantics; (B)-philosophy on flit_ecc double-bit; rob_req=0 still allocates tracker; lowest-FREE-index allocator; NMU_BUFFER_DEPTH=2.

**Disagreement** (3 points):

- "axi_rdata_par_o regenerated" — wording understates; per `theory_of_operation.md:398` it's *generated*, not regenerated. Cosmetic.
- "quiesce_idle combinational over aclk values, no CDC" — would report idle while flit is mid-CDC; recommend adding 2FF-sync noc_clk-side empty AND. **Material concern.**
- "axi_rready_o always-ready in reset" — re-read `pin_level_reset.md:432`; it should be 0 during reset for axi_*ready_o, not 1. RTL agent's interpretation may be wrong.

**New ambiguities surfaced** (5):

- **Credit-init epoch handshake exact cycle** — same-cycle vs next-cycle seed.
- **MetaBuffer indexing key** — slot vs `axi_id`-keyed vs `(rob_idx, src_id)` lookup tuple. C-bfm leans toward `(rob_idx, src_id)` because rob_idx alone is not unique across sources.
- **`CUT_AX`/`CUT_RSP` spill register placement** — pre-pack vs post-pack vs pre-CDC.
- **`prev_dest` adaptive-bypass arming/disarming exact cycle** — at AW issue, at flit-pack, or at allocation?
- **`R_ROB_TYPE=NoRoB` ordering trust assumption** — `NOC_FLIT_INORDER_PER_VC` only guarantees same-`(src_id, dst_id, vc_id)`; if NMU's VC mapping spreads same-`(src,dst)` traffic across VCs by qos, NoRoB silently breaks ordering. **NEW DISCOVERY.**

### `rtl` reviewing `c-bfm` — summary

**Agreement** (6 points): two-domain decomposition; wormhole arbiter LockIn until last & accept; RoB lowest-FREE / lower-rob_idx / prev_dest; CSR access semantics; test-only knobs have no RTL counterpart; PASSIVE drives during-reset values except `irq_o`.

**Disagreement** (3 points):

- **`axi_rready_o = 0` to back-pressure on R-buffer-full** — what spec says (`theory_of_operation.md:100`) and BFM should match. But `pin_level_reset.md:107,311,422` says `=1` "always-ready to drain". Genuine spec contradiction; needs erratum.
- **Hsiao vs Hamming** — RTL agent votes Hamming (3 of 4 spec sites use it; one Hsiao mention is typo). **Direct disagreement with c-bfm vote.**
- **CUT_AX=0 latency "T+1 not T combinational"** — agree on T+1, but spec phrasing "combinational pack + immediate inject" is misleading; worth tightening.

**New ambiguities surfaced** (5):

- **CDC FIFO depth and gray-code pointer width** unspecified beyond "synthesis-time parameter".
- **`CUT_AX` / `CUT_RSP` defaults** — both `bool, false` per signal_interface.md but never explicitly noted to flow through latency table.
- **`prev_dest[axi_id]` reset value + same-cycle update collision** — when AW + AR with same axi_id arrive same cycle, who writes prev_dest first?
- **AW + AR same-cycle RoB allocation order** — confirms designer ruling needed (RTL convention is AW-first).
- **`NMU_BUFFER_DEPTH=2`** documented but no equivalent NMU W-side buffer depth — which buffer absorbs W-burst back-pressure when noc_req credit exhausted?

## Synthesis

### Converged ambiguity list (ranked)

| Rank | Issue | Both flagged? | Affected wire/cycle behavior | Proposed resolution |
|------|-------|---------------|------------------------------|---------------------|
| 1 | Flit bit-layout (`02_flit.md` outside `spec/ni/`) | Both R1 #1 | Every flit on `noc_*_flit_o` | Publish `flit_layout.h` (SV pkg + C struct) checked into `spec/ni/`, version-locked |
| 2 | Hamming vs Hsiao SECDED | Both R1, **R2 disagree on which** | Every bit of `flit_ecc` | **Designer ruling** — spec self-contradicts; one side must change (3 sites Hamming vs 1 Hsiao) |
| 3 | `(R/W, qos) → vc_id` mapping function | RTL R1 #5; both R2 | Which VC each flit lands on (NUM_VC≥4) | Publish full `(R/W, qos[3:0], NUM_VC) → vc_id` table (not formula) |
| 4 | Credit-init epoch exact cycle | Both R1 wire-level lockdowns; C R2 expansion | Link bring-up timing + credit accounting | Seed counters cycle both `*_credit_init_ready_*` first concurrently HIGH; first injectable cycle = next |
| 5 | NSU MetaBuffer indexing | RTL R1 #2; C R2 expansion | NSU response packing correctness | Slot-keyed allocation, `(rob_idx, src_id)` lookup tuple |
| 6 | **NoRoB + Hybrid VC mapping ordering bug** | C R2 only — **NEW DISCOVERY** | Same-AXI-ID OoO across VCs violates AXI4 | NoRoB must force single-VC pinning OR `NOC_FLIT_INORDER_PER_VC` must be widened to `(src_id, dst_id)` only |
| 7 | CDC `flush_on_full_reset` trigger | RTL R1 #8 | Post-partial-reset link recovery | 2FF-sync sticky bit per side observing other's reset event; flush when both observe alignment |
| 8 | `CUT_AX` / `CUT_RSP` spill register placement | C R2 only | AXI-side observable latency | Pre-pack on AX, post-unpack on RSP; document in ToO §RTL pipeline |
| 9 | Same-cycle AW + AR allocation order | RTL R2 expansion | RoB index assignment + prev_dest evolution | Designer ruling: AW takes priority over AR same cycle |
| 10 | **`axi_rready_o` reset/operational contradiction** | RTL R2 only — **NEW SPEC CONTRADICTION** | R-channel back-pressure observable | Spec erratum: `pin_level_reset.md` row reads "0 during reset, 1 in steady-state, 0 when NSU R-buffer full" |
| 11 | ECC counter widths | C R1 #4; partial RTL R2 | ERR_STATUS register field placement | All four counters = `ERR_COUNTER_WIDTH=16` in `[15:0]`, `[31:16]` Reserved |
| 12 | **`quiesce_idle` missing NoC-domain empty AND** | C R2 only — **NEW DISCOVERY** | Reports idle while flit is mid-CDC | Add 2FF-sync noc_clk-side empty indicator AND |
| 13 | Wormhole RR pointer reset value + advancement rule | C R1 #5; RTL R2 expansion | First-cycle arbitration + multi-VC test reproducibility | Resets to 0; advances by 1 per granted **packet** (not per flit) |
| 14 | `prev_dest` arming/disarming + same-cycle update | C R2 + RTL R2 | NormalRoB fast-path correctness | Sample at allocation; same-cycle AW+AR resolved by AW-first rule |

### NEW DISCOVERIES (Round 2 only)

- **#6 NoRoB + Hybrid VC mapping ordering bug** (C-agent R2). Reader test could not have caught this — it required cross-paradigm reasoning about implementations.
- **#10 `axi_rready_o` reset/operational contradiction** (RTL R2). Spec self-contradicts across `pin_level_reset.md` and `theory_of_operation.md:100`.
- **#12 `quiesce_idle` missing NoC-domain empty AND** (C-agent R2). Operational correctness issue masked by spec staying purely aclk-domain in the formula.

### DISAGREEMENTS (need designer ruling)

| Issue | `c-bfm` position | `rtl` position |
|-------|------------------|----------------|
| Hsiao vs Hamming SECDED | Hsiao (dv/plan and test-knob authority; FlooNoC + OpenTitan ship Hsiao) | Hamming (3 of 4 spec sites; transaction_api.md mention is typo) |
| `axi_rready_o` reset value (1) | spec says 1 (`pin_level_reset.md` for-real value) | should be erratum to 0; current spec text contradicts ToO §100 |

### SPEC CONTRADICTIONS (1-line erratum class)

- **Hamming vs Hsiao**: `theory_of_operation.md:351,493`, `protocol_rules.md NOC_FLIT_HDR_FLIT_ECC_GEN` say Hamming. `protocol_rules.md NI_CFG_INJECT_ECC_ERROR` (test-knob row) says Hsiao. Resolution requires designer ruling first.
- **`axi_rready_o` value**: `pin_level_reset.md` rows for AW_OUT / R_OUT say `=1`. `theory_of_operation.md:100` operational text says NSU drives `=0` when buffer full. Erratum: pin_level_reset.md row should clarify "post-reset steady-state default; operational value depends on R-buffer occupancy".

## Verdict

- `D1.cross.implementer_review`: ✓ ran successfully. 14-item converged ambiguity list produced; 3 NEW DISCOVERIES surfaced by Round 2 cross-review beyond Round 1 independent reads.
- Resolutions required before D2:
  - **2 items** flagged for designer ruling (Hsiao vs Hamming; AW+AR same-cycle allocation order).
  - **2 items** as 1-line spec erratum (`axi_rready_o` reset/operational text; Hamming wording in 3 spec sites if Hsiao is chosen).
  - **10 items** deferred to combined D1→D2 transition planning meeting (flit_layout.h publish, vc_id mapping table, credit-init epoch, MetaBuffer indexing, CDC flush algorithm, CUT_AX/CUT_RSP placement, RR pointer reset, ECC counter widths, prev_dest arming, NoRoB single-VC pinning).

The 14 items must be resolved before either C-model or RTL implementation work proceeds at D2, or each ambiguity will encode a different assumption in the two implementations and bit-equivalence testing will surface them as silent divergence.
