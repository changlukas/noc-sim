# NI Presentation Outline (A5 wave, 2026-05-08)

**Target deck:** 15 slides, AMD pg313 NoC Architecture chapter-aligned ordering.

**Audience assumption:** internal review — engineers familiar with AXI4 + general NoC concepts. Not Versal-specific.

**Scope fence:** out-of-scope AMD chapters (Multi-SLR Boot Paths, MC Interleaving, AXI4-Stream, NMU/NSU Versions, AXI ID Compression) are silently skipped — no scope-fence slide.

**AMD content reuse legend (used throughout this doc):**
- **✓ Verbatim** — quote AMD text directly with attribution (`per AMD pg313 §<Section>`).
- **⚠ Rephrase** — concept aligns but specific Versal numbers / terms differ; rewrite in our spec's terms.
- **✗ Skip** — Versal-specific (PL / AI Engine / CIPS / HBM_NMU / 8×8 Switch / 24-token reg / DST-ID parity 7-bit map etc.) or design diverges.

---

## Slide-by-slide outline

### Slide 1 — Title + Scope

**Topic.** Title page; one-paragraph scope statement.

**Talking points.**
- "AXI4-over-NoC Network Interface — design + spec walkthrough"
- One NI per tile, single-chimney (NMU + NSU sharing one NoC link pair) — FlooNoC-aligned.
- This deck covers NI scope only; NPS (router) is a separate spec.

**AMD reuse.** None.

---

### Slide 2 — NoC Components Overview

**AMD pg313 ref.** §NoC Architecture / §NoC Components.

**Spec source.** `README.md` §Description, `doc/theory_of_operation.md` §Block diagram.

**Talking points.**
- AXI master/slave DUTs ↔ NMU + NSU ↔ NoC Router (NPS) fabric.
- NI = NMU (ingress) + NSU (egress) + CSR file + irq_o.
- Per-tile single-chimney; NMU drives `noc_req_o` + samples `noc_rsp_i`; NSU samples `noc_req_i` + drives `noc_rsp_o`.

**AMD reuse.**
- ✓ AMD's high-level NMU/NSU/NPS role description is generic enough — usable.
- ✗ Versal-specific component listing (PL master, AI Engine, CIPS coupling) — drop.

---

### Slide 3 — NMU Architecture (block diagram)

**AMD pg313 ref.** §NoC Master Unit (Figure 1).

**Spec source.** `images/NMU_block_diagram.md`, `doc/theory_of_operation.md` §NMU sub-blocks.

**Talking points.**
- 10 sub-blocks: AddrTrans, QoSGen, FlitPack, ECC Gen, Injection Buffer, VC Mapping, ECC Check, RoB, FlitUnpack, Outstanding-tx Timeout Tracker.
- AXI on left / NoC on right (AMD layout convention).
- Differences from AMD: added ECC Gen/Check (two-layer scheme), Outstanding-tx Timeout Tracker (sole AXI-rresp synth path), CSR file + irq_o.

**AMD reuse.**
- ⚠ AMD NMU functional list (clock crossing / rate matching / packetizing / address remap / RoB / write buffer / VC mapping) — the *concept names* align; rephrase in our terms ("FlitPack" not "Packetizing", "VC Mapping" not "VC Mapping with 24-token NPS scoping").
- ✗ AMD's specific Versal numbers (RROB 64-entry × 32-byte, HBM 64-entry × 64-byte, 512B Write Buffer) — our defaults differ (`MAX_TXNS=32`, `MAX_RO_TXNS_PER_ID=32`); cite our values.
- ✗ NMU Versions (NMU512 / HBM_NMU / NMU128) — Versal-specific, skip.

**Drift TODO before drafting slide.** Fix `images/NMU_block_diagram.md` D1 (VC Arbiter → VC Mapping) + D3 (route_par 9-bit cover, not 16) per earlier audit.

---

### Slide 4 — NMU: AXI Memory Mapped Support + Burst handling

**AMD pg313 ref.** §NoC Master Unit "AXI Memory Mapped Support".

**Spec source.** `doc/signal_interface.md` §AXI4 Manager port, `doc/theory_of_operation.md` §FlitPack, `doc/protocol_rules.md` §AXI4 host-side rules.

**Talking points.**
- 5 AXI4 channels (AW / W / B / AR / R) in standard ARM IHI 0022 form.
- Burst types: FIXED / INCR / WRAP all supported; AWLEN ≤ 255 (full AXI4 max); WRAP boundary enforced by AXI4 construction.
- 4KB-boundary check on INCR bursts (`AXI4_SLV_AW_BURST_4KB_BOUNDARY`).
- Wide flit physical channel: one AXI message → one flit cycle (no chopping in v0.4.0). **Diverges from AMD's 256-byte chopping.**

**AMD reuse.**
- ✓ "Asynchronous clock domain crossing and rate matching between the AXI master and the NoC" — verbatim, attributable.
- ⚠ AMD burst-conversion rules — concept aligns, but our flit width avoids most chopping.
- ✗ AMD's "256-byte aligned segments" chopping rule — we don't chop in v0.4.0.

---

### Slide 5 — NMU: Addressing + SAM + Destination ID

**AMD pg313 ref.** §NoC Master Unit "Addressing", "Address Decoding and System Address Map", "Destination ID".

**Spec source.** `doc/theory_of_operation.md` §AddrTrans, `doc/signal_interface.md` §Parameters (`Sam`, `XY_ADDR_OFFSET_X/Y`, `X_WIDTH`, `Y_WIDTH`, `ROUTE_ALGO`, `USE_ID_TABLE`).

**Talking points.**
- 3 routing modes: `XYRouting` / `SourceRouting` / `IDRouting` (`ROUTE_ALGO` parameter).
- `dst_id = (X, Y)` — concatenation of `X_WIDTH` + `Y_WIDTH` bits (default 4+4=8).
- SAM table = compile-time parameter `Sam` (FlooNoC `floo_axi_chimney.sv` aligned). Runtime modification out-of-scope in v0.4.0; no `SAM_RULE_*` CSR.

**AMD reuse.**
- ⚠ Concept of address-map / remap — aligns; rephrase as our `Sam` rule structure.
- ✗ AMD's specific Master-Specified ID / Re-mapping / 7-bit address parity bit map — Versal-specific, skip.

---

### Slide 6 — NMU: Read Reorder Buffer

**AMD pg313 ref.** §NoC Master Unit "Read Reorder Buffer".

**Spec source.** `doc/theory_of_operation.md` §RoB allocator, §RoB variants, §`prev_dest` adaptive bypass.

**Talking points.**
- 3 modes per response channel: `NoRoB` / `SimpleRoB` / `NormalRoB` (FlooNoC-aligned naming). Independent for B and R via `B_ROB_TYPE` / `R_ROB_TYPE`.
- NormalRoB: per-AXI-ID linked-list ordering + `prev_dest` adaptive bypass.
- A5 designer-confirmed: lowest-index-first allocation; lower `rob_idx` releases first; tracker still allocated even when `rob_req=0`.

**AMD reuse.**
- ✓ "Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance" — concept verbatim usable.
- ⚠ AMD RROB 64-entry × 32-byte default — our `MAX_TXNS=32`, `MAX_RO_TXNS_PER_ID=32`; cite our values.

---

### Slide 7 — NMU: Outstanding Tx + Write Response Tracker + Write Ordering

**AMD pg313 ref.** §NoC Master Unit "Outstanding Transaction Support", "Write Response Tracker", "Write Ordering".

**Spec source.** `doc/theory_of_operation.md` §Outstanding-tx Timeout, §AR-during-W ordering, `doc/protocol_rules.md` `NOC_MST_WORMHOLE_LOCK` + `AXI4_MST_TIMEOUT_SLVERR`.

**Talking points.**
- `MAX_TXNS=32` outstanding (per-NMU) and `MAX_TXNS_PER_ID=32`.
- `TXN_TIMEOUT` default 10000 aclk cycles → on timeout `bresp/rresp = SLVERR` + `ERR_STATUS[1] timeout_err` + `ERR_COUNT++`.
- W-burst wormhole-lock: AR injection blocked while a W burst is in flight on the same `noc_req_o` link (FlooNoC-aligned).
- A5 designer-confirmed: representative target 1.2 GHz noc_clk / 800 MHz aclk; integrators adjust.

**AMD reuse.**
- ✓ "32 outstanding read and 32 outstanding write transactions" — number aligns with our default.
- ✗ AMD "32-entry interleaved read tracker and 32-entry chop-merge write tracker" — we don't chop, so chop-merge tracker doesn't apply.
- ⚠ Write ordering description — concept aligns; phrase as wormhole-lock (FlooNoC term).

---

### Slide 8 — NMU: Error Conditions + IRQ

**AMD pg313 ref.** §NoC Master Unit "Error Conditions" + §Data Integrity (error reporting).

**Spec source.** `doc/registers.md` §`ERR_STATUS`, §`IRQ_ENABLE`, §`LAST_ERR_INFO`, `doc/theory_of_operation.md` §ECC.

**Talking points.**
- 4 event classes (RW1C-cleared bits in `ERR_STATUS[3:0]`): `ecc_uncorr_err` / `timeout_err` / `route_par_err` / `axi_parity_err`.
- Each bit pairs with a saturating counter; software writes 1 to clear bit + counter atomically.
- `irq_o = OR_i (ERR_STATUS[i] & IRQ_ENABLE[i])` — level-sensitive; default mask = all-zeros (all IRQs masked).
- `LAST_ERR_INFO` sticky — first error captured; subsequent suppressed until cleared.
- (B)-philosophy ECC scheme: log + IRQ, no SLVERR synthesis from ECC path. Aligns with AMD's "uncorrectable ECC errors result in a fatal interrupt" (no rresp synth).

**AMD reuse.**
- ✓ "Uncorrectable ECC errors result in a fatal interrupt" — verbatim; reinforces our (B)-philosophy is industry-aligned.
- ✓ "By default, all interrupts are masked" — verbatim; matches our `IRQ_ENABLE` reset value.
- ✗ AMD's "fatal interrupt" exception escalation policy — Versal SoC-specific, our IRQ is recoverable.

---

### Slide 9 — NSU Architecture (block diagram)

**AMD pg313 ref.** §NoC Slave Unit (overview).

**Spec source.** `images/NSU_block_diagram.md`, `doc/theory_of_operation.md` §NSU sub-blocks.

**Talking points.**
- 10 sub-blocks: ECC Check, W Reassembly, Downsize, FlitUnpack, MetaBuffer, Exclusive Monitor, R Response Buffer, ECC Gen, FlitPack B/R, VC Mapping.
- MetaBuffer = response-path inheritance source (carries `rob_idx` / `src_id` / `qos` / `axi_id`).
- AMD lumps "Rate Matching + Async Boundary Crossing" into one block; we explicitly separate W Reassembly, R Response Buffer, MetaBuffer, CDC FIFO.

**AMD reuse.**
- ✓ "Conversion of NoC packetized data (NPD) to and from AXI protocol data" — verbatim.
- ✓ "Asynchronous clock domain crossing and rate-matching between the AXI slave and the NoC" — verbatim.
- ✓ "Read responses are buffered before forwarding to minimize bubbles" — verbatim, justifies our R Response Buffer.
- ✗ AMD's NSU Versions (NSU512 / NSU128 / DDRMC-NSU / HBM_NSU) — skip.

**Drift TODO before drafting slide.** Fix `images/NSU_block_diagram.md` D2 (VC Arbiter → VC Mapping) + D5 (port_id removed) per earlier audit.

---

### Slide 10 — NSU: Exclusive Monitor + Errors

**AMD pg313 ref.** §NoC Slave Unit "Exclusive Monitor (AXI Exclusive Access)" + "Error Conditions".

**Spec source.** `doc/theory_of_operation.md` §NSU Exclusive Monitor, `doc/registers.md` §`EXCLUSIVE_MONITOR_CTRL` / §`EXCLUSIVE_MONITOR_STATUS`, `doc/protocol_rules.md` `NI_CFG_EXCLUSIVE_CLEAR_RACE` / `NI_CFG_EXCLUSIVE_OCCUPANCY_ACCURACY`.

**Talking points.**
- `EXCLUSIVE_MONITOR_DEPTH=8` per-axi_id reservation slots.
- AXI4 §A7 Exclusive Access semantics: `EXOKAY` on match, `OKAY` on miss.
- Software-clearable via `EXCLUSIVE_MONITOR_CTRL.clear_all` (W1 self-clearing); `EXCLUSIVE_MONITOR_STATUS.occupancy` for live count.
- Race semantics on simultaneous clear + AW match / new AR alloc / overlap-invalidate documented in `NI_CFG_EXCLUSIVE_CLEAR_RACE`.

**AMD reuse.**
- ✓ "AXI exclusive access handling" feature listing — verbatim concept usable.
- ✗ AMD's per-version monitor size variation — Versal-specific, our default 8.
- ✗ AMD's AXI ID Compression — we don't compress (different in/out ID widths via `IN_ID_WIDTH`/`OUT_ID_WIDTH` parameters, not compression).

---

### Slide 11 — NoC Packet Switch (adjacent / out-of-scope)

**AMD pg313 ref.** §NoC Packet Switch (overview only).

**Spec source.** `doc/signal_interface.md` §VC Mapping notes, `doc/protocol_rules.md` `NOC_VC_*` rules.

**Talking points.**
- NPS (router) is separate spec; this slide draws the boundary.
- NI's job: **VC Mapping** at flit-construct time (Hybrid R/W × QoS, fixed at design time per `NOC_VC_MAPPING_HYBRID_RW_QOS`).
- NPS's job: **cycle-level VC arbitration** (LRU across input ports), per-hop `route_par` check.
- `NUM_VC` parameter: 1 (default) to 8 (`VC_ID_WIDTH=3` upper bound).

**AMD reuse.**
- ✓ AMD VC Arbitration paragraph: *"For every cycle, each output port performs Least Recently Used (LRU) arbitration on all virtual channels of the three input ports."* — verbatim, but cite as **NPS scope, out of NI**.
- ✗ AMD 8×8 Switch / 24-token / 3-input-port specifics — Versal-specific, skip.
- ✗ AMD Differentiated QoS (NPS-side) — separate router spec.

---

### Slide 12 — Credit-Based Flow Control

**AMD pg313 ref.** §NoC Packet Switch / §Credit Based Flow Control.

**Spec source.** `doc/signal_interface.md` §NoC credit signals (incl. credit-init handshake), `doc/protocol_rules.md` `NOC_MST_FLIT_ON_CREDIT_ONLY`.

**Talking points.**
- Bi-directional `*_credit_init_ready_*` handshake at startup (per AMD §Credit-Based Flow Control verbatim).
- Per-VC credit return on `noc_*_credit_*[NUM_VC-1:0]` (1 cycle / 1 credit max per VC, AMD verbatim).
- Source must hold ≥1 credit on chosen VC before `noc_*_valid_o = 1`.
- `CREDIT_TIMEOUT` = 10000 noc_clk default — credit starvation triggers outstanding-tx timeout path → SLVERR + `ERR_STATUS[1]`.

**AMD reuse.**
- ✓✓ **Strongest verbatim section.** Quote the full AMD paragraph:
  > "Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."
- This entire paragraph is generic AXI-NoC and matches our spec verbatim. Cite as `per AMD pg313 §Credit-Based Flow Control`.

---

### Slide 13 — NoC Communication: Read / Write Tx + QoS

**AMD pg313 ref.** §NoC Communication §Read Transactions + §Write Transactions + §Quality of Service.

**Spec source.** `doc/theory_of_operation.md` §QoSGen, `doc/registers.md` §QoS family (`QOS_MODE`, `QOS_FIXED_VALUE`, `BANDWIDTH_LIMIT/BUDGET`, `BASE_QOS`, `URGENCY_STEP`, `SOCKET_QOS`).

**Talking points.**
- End-to-end Read flow: AXI master → NMU FlitPack(AR) → noc_req_o → router → noc_req_i → NSU FlitUnpack → local AXI slave AR; response path mirror.
- End-to-end Write flow: AW + W flits per burst (W wormhole-locked); B response routed back.
- QoS Generator 4 modes (NMU-only): **Bypass** (passthrough AXI awqos/arqos) / **Fixed** (`QOS_FIXED_VALUE`) / **Limiter** (bandwidth cap → drop to LOW_PRIORITY when over) / **Regulator** (feedback-controlled urgency escalation, clamped at 15).
- NSU does NOT recompute qos; inherits via MetaBuffer.

**AMD reuse.**
- ⚠ AMD's high-level Read/Write flow ("AXI master sends read/write requests to a connected NoC access point (NMU). The NMU relays the requests through a set of NoC packet switches (NPSs) before the requests reach a destination (NoC slave unit NSU or output port)") — usable, generic enough.
- ✗ AMD's specific Versal QoS (Differentiated QoS in NPS, virtual channel arbitration scoring) — Versal-specific.
- ⚠ Our QoS modes (Bypass/Fixed/Limiter/Regulator) follow FlooNoC + custom design, not AMD's exact set; cite our spec, not AMD.

---

### Slide 14 — NoC Communication: Data Integrity (two-layer ECC + AXI parity)

**AMD pg313 ref.** §NoC Communication §Data Integrity / §Parity / (Data Poisoning N/A).

**Spec source.** `doc/theory_of_operation.md` §ECC, §AXI parity handling, `doc/signal_interface.md` §Optional AXI parity sideband, `doc/protocol_rules.md` ECC + parity rules.

**Talking points.**
- **Two-layer integrity scheme:**
  - Per-hop: 1-bit even parity `route_par` over `{dst_id, last}` (9-bit cover) — checked at every router output.
  - End-to-end: whole-flit SECDED `flit_ecc` (10-bit syndrome over 396-bit) — checked **only** at destination NI sink (routers do not check).
- AXI host-side parity (optional, `ENABLE_AXI_PARITY=true` default): 1 bit per byte for data, 1 bit per byte for address.
- (B)-philosophy: corrupted flits forwarded with `bresp/rresp = OKAY`; observability via `ERR_STATUS` + IRQ + counters. No SLVERR synthesis from ECC/parity path — preserves AXI rresp channel for end-to-end (HBM/DDR) and timeout-driven errors only.
- Parity regeneration: NMU regenerates `axi_rdata_par_o` **after** ECC check stage (AMD-aligned).

**AMD reuse.**
- ✓✓ **Multiple verbatim quotes available.** Use the following directly with `per AMD pg313 §Parity` / `§Data Integrity`:
  > "1 bit per byte for Data" / "1 bit per byte for AxAddress"
  >
  > "The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity."
  >
  > "Parity is checked in the NMU/NSU pipeline when an AXI field is consumed. When an AXI field is modified by NMU/NSU logic, parity is regenerated."
  >
  > "Data parity for read responses is generated as 1 bit per byte after the ECC check stage, when the data is converted from NPP to AXI protocol."
  >
  > "SECDED ECC across the entire flit."
  >
  > "No ECC checking is performed in the switch fabric."
  >
  > "Packet domain parity and ECC generation and checking is always enabled."
  >
  > "Correctable ECC errors are corrected on the fly, and the count of correctable errors is incremented. Uncorrectable ECC errors result in a fatal interrupt."
- ✗ AMD's HBM_NMU "1 parity bit per 32 bits of data and 1 parity bit per 24 bits of address" exception — Versal HBM-specific; our scheme is uniform 1 bit per byte.
- ✗ AMD's "address map/remap regenerates seven parity bits" 7-bit map — Versal-specific.
- ✗ Data Poisoning sub-section (if applicable in pg313) — we don't implement poisoning.

**This is the strongest AMD-aligned slide.** Lean into the verbatim quotes — they reinforce that our spec is industry-aligned, not arbitrary design choices.

---

### Slide 15 — Standards (AXI Conversion) + Clocking + DV Plan + Q&A

**AMD pg313 ref.** §Standards §AXI Conversion + §Clocking + closing.

**Spec source.** `doc/signal_interface.md` §Parameters + §Protocol clock and reset, `doc/pin_level_reset.md`, `doc/theory_of_operation.md` §Width-conversion + §CDC, `dv/plan.md`.

**Talking points.**
- AXI conversion: AxSize/AxLen handling, NSU Downsize block (when `DATA_WIDTH > FLIT_PAYLOAD_WIDTH`), narrow transfer mechanism (per AMD AxCache[1]).
- Dual-clock domain: `aclk_i` (AXI side) + `noc_clk_i` (NoC side); CDC via gray-counter pointer + 2FF synchronizer FIFOs (`CDC_FIFO_DEPTH=16` default, conservative for ratio range [0.1, 10]).
- Reset: `arst_ni` + `noc_rst_ni`, async assertion / sync deassertion, ≥16 cycles hold; partial reset semantics in `pin_level_reset.md`.
- DV summary: 138 protocol rules (128 FAIL + 10 RECOMMEND) → 138 ABV properties; 51 testpoints; UVM 1.2 (A5 designer-confirmed).
- Future: ATOP_SUPPORT=1 (deferred ~3 weeks per A5 designer-confirm); v0.5.0 Protocol Reference Library (plugin-side, see `plan/BFM_MODE_DESIGN.md` §13).

**AMD reuse.**
- ⚠ AMD AxSize/AxLen conversion concept — usable as reference; specific examples are Versal Memory-Controller-targeted, our scope is fabric-only.
- ✗ AMD address chopping examples (256-byte aligned segments) — we don't chop.
- ⚠ Clocking concept (internal system clock + resets) — generic, usable.

---

## AMD content reuse cross-cut summary

| AMD pg313 chapter | Verbatim quotable | Rephrase | Skip (Versal-specific) |
|---|---|---|---|
| §NoC Components | high-level NMU/NSU/NPS roles | — | PL / AI Engine / CIPS coupling |
| §NoC Master Unit | clock-crossing/rate-matching tagline; outstanding-32 number; "fatal interrupt on uncorr ECC" | RoB / VC Mapping / Address remap concepts | NMU Versions; HBM 64×64; 256-byte chopping; 7-bit address parity map; Re-tagging Buffer |
| §NoC Slave Unit | "Conversion of NPD ↔ AXI"; "async clock crossing + rate-matching"; "buffered before forwarding to minimize bubbles"; "AXI exclusive access handling" | Excl Monitor concept | NSU Versions; AXI ID Compression |
| §NoC Packet Switch | (entire NPS) — out of NI scope, only context | — | 8×8 Switch; 24-token; Differentiated QoS specifics |
| §Credit-Based Flow Control | **full paragraph** (highest reuse value) | — | — |
| §Virtual Channel Arbitration | LRU one-liner (cite as NPS scope) | — | 24-token / 3-input-port specifics |
| §Read/Write Transactions | high-level flow narration | QoS modes | Versal-specific QoS scoring |
| §Data Integrity / §Parity | **8 verbatim sentences** (second-highest reuse) | — | HBM 32-bit/24-bit exception; address-remap 7-bit map; Data Poisoning |
| §Standards / §AXI Conversion | AxSize/AxLen overview | Conversion concept | 256-byte chopping examples |
| §Clocking | generic dual-clock concept | — | Internal System Clock specifics |

**Top 3 slides with strongest AMD verbatim leverage:** Slide 12 (Credit) > Slide 14 (Data Integrity) > Slide 8 (Errors / IRQ default-masked).

**Slides where we deliberately diverge from AMD (call out as design choice, not gap):**
- Slide 4 — no chopping (single-flit per AXI message).
- Slide 8 — (B)-philosophy: log + IRQ, no SLVERR synth from ECC.
- Slide 13 — QoS modes (Bypass/Fixed/Limiter/Regulator) follow FlooNoC + custom, not AMD's NPS-side Differentiated QoS.

---

## Open issues to resolve before drafting actual slides

1. **NMU/NSU block-diagram drift** — fix the 7 confirmed stale items + 3 coverage gaps (D1/D2 `VC Arbiter`→`VC Mapping` in NMU+NSU mermaid; D3 `route_par` 9-bit cover not 16; D5 `port_id` removed; D9 stamp `post-A4.5`→`post-A5`; NSU CSR-link missing; **D11 NMU mermaid header `Network Master Unit`→`Network Manager Unit`** [new, found Batch A review]; **D12 `doc/theory_of_operation.md:477` RTL block-diagram mermaid `VCARB_O / VCARB_S` use `VC Arbiter` while §sub-module description (line 508) uses `VC Mapping` — internal ToO inconsistency, touches source spec not only images** [new, found Batch B planning]). Otherwise Slides 3 / 9 will display stale info.
2. **`flit_ecc` coverage** — ✓ resolved 2026-05-08: 396 bit correct per `doc/theory_of_operation.md:369-371` §ECC (`FLIT_DATA_WIDTH = FLIT_WIDTH − FLIT_ECC_WIDTH = 406 − 10`; Layer 2 SECDED excludes only `flit_ecc` itself, so `route_par` is deliberately covered by both Layer 1 per-hop parity and Layer 2 end-to-end SECDED — overlap is intentional on routing-critical bits). AMD pg313 §Data Integrity ("SECDED ECC across the entire flit" + DST-ID parity as "additional") aligns with this two-layer design.
3. **Top-level NI block diagram** — Slide 2 currently uses NMU/NSU diagrams jointly. Could draw one combined top-level (drafted in earlier turn). Decision: include or skip.
4. **DV plan slide depth** — Slide 15 currently bundles AXI conversion + clocking + DV + future. If audience wants DV detail, split into 2 slides (16-slide deck).
