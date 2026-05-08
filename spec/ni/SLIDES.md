# NI Presentation Slides

**Source of truth** for slide content. PPT / Google Slides drafting consumes this file.

- **Audience:** internal review (engineers familiar with AXI4 + general NoC concepts; not Versal-specific).
- **Deck length:** 15 slides, AMD pg313 NoC Architecture chapter-aligned.
- **Drafting status:** A5 wave 2026-05-08, in progress.

**Drafting progress:**

| Batch | Slides | Status |
|---|---|---|
| A | 1–2 (Title + Components) | ✓ drafted |
| B | 3–8 (NMU) | ✓ drafted |
| C | 9–10 (NSU) | pending |
| D | 11–12 (NPS context + Credit) | pending |
| E | 13–14 (Communication + Data Integrity) | pending |
| F | 15 (Closing) | pending |

---

## Slide format convention

Each slide section contains:

1. **On-slide content** — the bullets / tables / figure refs that actually render on the slide. Terse, max ~6 bullets.
2. **Speaker notes** — what the presenter says aloud. Discursive, complete sentences.
3. **Visual asset** — diagram path / source for the slide's main figure (if any).
4. **AMD verbatim quotes (if used)** — pre-formatted quote blocks ready to paste, with attribution.

Cross-references to spec files use relative paths from `spec/ni/`.

---

## Slide 1 — Title + Scope

### On-slide content

> **AXI4-over-NoC Network Interface**
>
> Design + spec walkthrough — v0.4.0 spec, A5 wave 2026-05-08

- One NI per tile, single-chimney (NMU + NSU share one NoC link pair) — FlooNoC-aligned.
- Deck covers NI scope only; NoC Packet Switch (router / NPS) is a separate spec.
- Chapter ordering follows AMD pg313 NoC Architecture (industry reference for cross-checking).

### Speaker notes

This deck walks through the AXI4-over-NoC Network Interface block. The NI is the per-tile chimney that converts AXI4 traffic to and from NoC flits — both manager-side ingress (NMU) and subordinate-side egress (NSU) live inside one NI. We follow AMD pg313's chapter ordering so that readers familiar with Versal's NoC have a direct cross-reference; where our design diverges from AMD it is a deliberate choice and called out explicitly. The router (NPS in AMD's terminology) is adjacent infrastructure; its detailed spec is separate.

### Visual asset

None — title slide. Optional small tile-thumbnail at corner.

### AMD verbatim quotes

None.

---

## Slide 2 — NoC Components Overview

### On-slide content

**What's inside the NI**

- **NMU** (Network Manager Unit) — receives AXI from local master; injects request flits onto NoC; receives response flits.
- **NSU** (Network Subordinate Unit) — receives request flits from NoC; drives AXI to local slave; injects response flits.
- **CSR file** — software-visible runtime control (QoS / Probes / Errors / Quiesce / Exclusive Monitor).
- `irq_o` — level-sensitive interrupt to host CPU.

**Per-tile single-chimney pattern**

| Direction | NMU role | NSU role |
|---|---|---|
| `noc_req_o` (egress req link) | drives | — |
| `noc_req_i` (ingress req link) | — | samples |
| `noc_rsp_o` (egress rsp link) | — | drives |
| `noc_rsp_i` (ingress rsp link) | samples | — |

- Both halves independently enabled (`EN_MGR_PORT` / `EN_SBR_PORT` parameters).
- One NI per tile (`(x, y)` mesh coordinate); upstream multi-IP fan-in via AXI crossbar.

### Speaker notes

The NI block is the boundary between AXI4 protocol and the NoC's flit protocol. Inside the NI sit two functionally independent halves — NMU and NSU — that share one physical pair of NoC link wires per tile. NMU handles outbound requests from the local AXI master plus inbound responses; NSU handles inbound requests destined for the local AXI slave plus outbound responses. The two never directly communicate inside the NI — their only coupling is the shared NoC link pair (single-chimney pattern). This matches FlooNoC's `floo_axi_chimney.sv` topology and AMD pg313's NMU + NSU separation. The CSR file is software's window into the NI: it hosts QoS configuration, Performance Probes, ERR_STATUS for the four error event classes, runtime quiesce control for the NMU, and the NSU's Exclusive Monitor clear trigger. `irq_o` is a single level-sensitive line that asserts when any unmasked ERR_STATUS bit is set.

### Visual asset

**Chosen: Option B — NMU + NSU diagrams shown side-by-side.**

- NMU diagram source: `images/NMU_block_diagram.md` (mermaid).
- NSU diagram source: `images/NSU_block_diagram.md` (mermaid).
- Pre-render: export both to SVG via mermaid CLI before pasting into slides.
- **⚠ Prerequisite:** both diagrams have known drift items (see `PRESENTATION_OUTLINE.md` §Open issues #1 — `VC Arbiter` → `VC Mapping`, `route_par` 9-bit cover, `port_id` removal, A4.5 stamp, NMU header text "Master" → "Manager"). Drift fix must precede rendering — slides will display stale info otherwise.

### AMD verbatim quotes

- ✓ AMD's high-level NMU/NSU/NPS role description is generic and usable.
- ✗ Versal-specific component mentions (PL master, AI Engine, CIPS coupling) — do not include.

---

## Slide 3 — NMU Architecture overview

### On-slide content

**10 sub-blocks** (AXI on left, NoC on right per AMD pg313 layout convention):

AddrTrans · QoSGen · FlitPack · ECC Gen · Injection Buffer · VC Mapping · ECC Check · RoB · FlitUnpack · Outstanding-tx Timeout Tracker.

**Differences from AMD pg313 NMU**

- Added two-layer ECC (`route_par` per-hop + `flit_ecc` end-to-end).
- Added Outstanding-tx Timeout Tracker (sole AXI-rresp synth path on fabric error).
- Added CSR file + `irq_o`.

**Detail split** across Slide 4 (burst), 6 (RoB), 7 (timeout), 8 (errors), 12 (credit), 14 (data integrity).

### Speaker notes

Walk through data flow direction. AW/AR enter from the left → AddrTrans resolves `dst_id` → QoSGen computes flit `qos` → FlitPack assembles the flit payload → ECC Gen attaches `route_par` + `flit_ecc` → Injection Buffer queues per-VC → VC Mapping picks the egress VC → flit injected on `noc_req_o`. Response path mirror: `noc_rsp_i` → ECC Check → RoB enforces per-AXI-ID order → FlitUnpack reconstructs B/R → AXI master. Outstanding-tx Timeout sits as the safety net — any allocated tracker that doesn't see its response within `TXN_TIMEOUT` cycles is forcefully resolved with SLVERR.

### Visual asset

`images/NMU_block_diagram.md` (mermaid) → render to SVG via mermaid CLI.

**⚠ Prerequisite:** drift fix per `PRESENTATION_OUTLINE.md` §Open issue #1 (D1 `VC Arbiter`→`VC Mapping`, D3 `route_par` 9-bit cover, D5 `port_id` removal, D9 A4.5 stamp, D11 NMU header `Master`→`Manager`) must precede rendering.

### AMD verbatim quotes

> "Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."
>
> *— AMD pg313 §NoC Master Unit*

- ✗ skip: NMU Versions (NMU512 / HBM_NMU / NMU128); RROB 64-entry × 32-byte; 512B Write Buffer.

---

## Slide 4 — AXI Memory Mapped Support + Burst handling

### On-slide content

- **5 AXI4 channels** (AW / W / AR / B / R) — ARM IHI 0022 form on host side.
- **3 burst types supported**: FIXED / INCR / WRAP.
- `AWLEN ≤ 255` (full AXI4 max length); WRAP boundary auto-bounded by AXI4 construction.
- INCR 4KB-boundary check enforced (`AXI4_SLV_AW_BURST_4KB_BOUNDARY`).
- **No chopping** (D8 design choice): one wide flit per AXI message group. Diverges from AMD's 256-byte chopping.
- Narrow transfer (`AxSIZE < log2(DATA_WIDTH/8)`) honoured via `wstrb` regen + over-fetch.

### Speaker notes

The AXI master sees standard AXI4 protocol — no surprises at the wire. Internally, NMU FlitPack accumulates W beats into a wide flit (Upsize block) when the master is narrower than the NoC. `wstrb` is regenerated per wide flit so the destination slave only commits the bytes the master actually drove. We deliberately do not chop bursts at any boundary — this contrasts with AMD Versal's 256-byte chopping. Our wider flit format avoids the need; the 4KB-cross check still applies for safety, matching AXI4 spec mandate.

### Visual asset

Conceptual schematic: AXI master narrow W beats (e.g., 4 × 64-bit) → accumulated into 1 wide W flit (256-bit payload) on `noc_req_o`. PPT-drawn; no mermaid asset to reuse.

### AMD verbatim quotes

- ⚠ rephrase: AMD burst-conversion concept aligns; specifics differ (no chopping). Frame as "our wider flit avoids chopping".
- ✗ skip: AMD "256-byte aligned segments" chopping rule; AMD's specific 512B Master-write-buffer size.

---

## Slide 5 — NMU Addressing + SAM + Destination ID

### On-slide content

- **3 routing modes** (`ROUTE_ALGO` parameter):
  - `XYRouting` (default) — 2D mesh deterministic.
  - `SourceRouting` — pre-computed per-flit path.
  - `IDRouting` — `Sam` table lookup by AXI ID.
- `dst_id = (X, Y)` — 2D mesh coordinate; default `X_WIDTH=4 + Y_WIDTH=4 = 8 bits`.
- Address bit extraction: `XY_ADDR_OFFSET_X` / `XY_ADDR_OFFSET_Y` parameters select the awaddr/araddr bit fields holding the coordinates.
- `Sam` table = compile-time parameter (FlooNoC `floo_axi_chimney.sv` aligned). **Runtime modification out of v0.4.0 scope** — no `SAM_RULE_*` CSR; re-elaborate to change.
- AddrTrans block: combinational lookup, registered output.

### Speaker notes

Three routing modes give integrators flexibility for different topologies. XYRouting is the default for regular meshes — fast, simple, deterministic. SourceRouting suits pre-computed paths when the route is known at flit-construct time. IDRouting maps AXI ID directly to `dst_id` via the SAM lookup table. AMD's Versal NoC has its own Master-Specified ID and Re-mapping mechanisms tied to PL-interconnect coupling; those are Versal-specific and not part of general AXI4-NoC concepts.

### Visual asset

Address breakdown diagram showing where in `axi_awaddr_i` the X/Y coordinate bits are extracted (per `XY_ADDR_OFFSET_X` / `XY_ADDR_OFFSET_Y`). PPT-drawn or simple ASCII bit-field figure.

### AMD verbatim quotes

- ⚠ rephrase: address remap concept aligns; our `Sam` rule structure differs from AMD's address-map.
- ✗ skip: Master-Specified ID, Re-mapping, 7-bit address parity bit map — Versal-specific.

---

## Slide 6 — NMU Read Reorder Buffer

### On-slide content

| Mode | Area cost | Use case |
|---|---|---|
| `NoRoB` (param default) | minimal — no allocation | NoC preserves same-source-same-dest in-order delivery; single-issue master |
| `SimpleRoB` | small — single shared release ptr | naive FIFO; tolerates cross-ID HoL blocking |
| `NormalRoB` | largest | per-AXI-ID linked-list + `prev_dest` adaptive bypass |

- B-channel and R-channel RoB modes **independent** (`B_ROB_TYPE` / `R_ROB_TYPE`).
- Typical multi-destination deployment: `R_ROB_TYPE = NormalRoB`, `B_ROB_TYPE = SimpleRoB` (B is metadata-only via `ONLY_METADATA_B=true`).
- A5 designer-confirmed (2026-05-08): lowest-index-first allocation; lower `rob_idx` releases first on tie; tracker still allocated when `rob_req=0`.

### Speaker notes

RoB sizing dominates total NMU area. At maximum-config `R_ROB_TYPE=NormalRoB, MAX_TXNS=32, DATA_WIDTH=256, MAX_BURST_LEN=256`, the worst-case R-RoB storage is 2 Mbits. At default `MAX_BURST_LEN=16` the same NormalRoB drops to 128 Kbits — the typical-deployment number. NormalRoB's `prev_dest` adaptive bypass is the performance optimisation: when consecutive same-AXI-ID requests target the same destination NSU, NormalRoB skips per-ID linked-list overhead and uses the fast-path. AMD's Versal NoC RROB is sized 64-entry × 32-byte (HBM variant 64×64); we're more conservative at MAX_TXNS=32 by default.

### Visual asset

RoB state machine: `FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE`. PPT-drawn or mermaid `stateDiagram` block.

### AMD verbatim quotes

> "Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."
>
> *— AMD pg313 §NoC Master Unit §Read Reorder Buffer*

- ⚠ rephrase: AMD's RROB 64-entry × 32-byte default; cite our `MAX_TXNS=32` instead.

---

## Slide 7 — Outstanding Tx + Write Response Tracker + Write Ordering

### On-slide content

- **Outstanding limits**: `MAX_TXNS = 32` per NMU; `MAX_TXNS_PER_ID = 32` per AXI ID.
- **Timeout**: `TXN_TIMEOUT = 10 000 aclk_i` cycles default. On expiry: `bresp/rresp = SLVERR` + `ERR_STATUS[1] timeout_err` + `ERR_COUNT++` + (if enabled) `irq_o`.
- **Sole AXI-rresp synth path** for fabric errors. Covers 3 indistinguishable scenarios: slave-never-responds / flit-loss / route_par-drop. Software disambiguates via per-class counters.
- **Write ordering**: W-burst wormhole-locked at NMU output (`NOC_MST_WORMHOLE_LOCK`). AR injection blocked while W in flight on the same `noc_req_o`.
- A5 designer-confirmed (2026-05-08): representative synthesis target 1.2 GHz `noc_clk_i` / 800 MHz `aclk_i`; integrators adjust per actual deployment.

### Speaker notes

Outstanding-tx tracking is the safety net. The NoC error path produces three otherwise-indistinguishable wire-level symptoms — no response ever — and the timeout converts each into a deterministic AXI SLVERR within 10 µs at 1 GHz `aclk_i`. The ISR reads the per-class counters (`ROUTE_PAR_ERR_CNT`, `ECC_UNCORR_ERR_CNT`) plus `LAST_ERR_INFO` to figure out which scenario fired. Wormhole-lock at NMU output serializes W-burst beats on `noc_req_o` — keeps the burst contiguous for tight reassembly at NSU. Reference implementation: FlooNoC `rr_arb_tree LockIn=1` releases only on `last & ready`.

### Visual asset

Timing diagram: AW handshake → flit injection → no response over many cycles → `TXN_TIMEOUT` counter expires → SLVERR delivered to AXI master. Plus an inset showing wormhole-locked W-burst sequence (W beats contiguous on `noc_req_o`).

### AMD verbatim quotes

> "32 outstanding read and 32 outstanding write transactions."
>
> *— AMD pg313 §NoC Master Unit §Outstanding Transaction Support*

- ✗ skip: AMD "32-entry interleaved read tracker and 32-entry chop-merge write tracker" — chop-merge tracker N/A (we don't chop).

---

## Slide 8 — NMU Error Conditions + IRQ

### On-slide content

| `ERR_STATUS` bit | Event class | Paired counter |
|---|---|---|
| `[0] ecc_uncorr_err` | flit_ecc 2-bit detected | `ECC_UNCORR_ERR_CNT` |
| `[1] timeout_err` | NMU outstanding-tx timeout | `ERR_COUNT` |
| `[2] route_par_err` | route_par mismatch (router or sink drop) | `ROUTE_PAR_ERR_CNT` |
| `[3] axi_parity_err` | AXI host-side parity mismatch | `AXI_PARITY_ERR_CNT` |

- **Uniform RW1C**: software writes 1 → bit + paired counter cleared atomically.
- **IRQ**: `irq_o = OR_i(ERR_STATUS[i] & IRQ_ENABLE[i])` — level-sensitive; reset = 0 (all masked).
- `LAST_ERR_INFO` **sticky** — first un-cleared error wins; subsequent errors do not overwrite.
- **(B)-philosophy**: ECC / parity errors → log + IRQ, never SLVERR synthesis. AXI rresp/bresp reserved for end-to-end (HBM/DDR) and timeout-driven SLVERR.

### Speaker notes

Four event classes share one uniform pattern: each `ERR_STATUS` bit pairs with a saturating counter, both cleared atomically by RW1C write. `irq_o` is level-sensitive — software's ISR sees the bit pattern, reads `LAST_ERR_INFO` for the offending-transaction context (`err_axi_id`, `err_src_id`, `err_dst_id`), and consults the per-class counter for cumulative count. The (B)-philosophy decision is deliberate: fabric-level ECC errors are NOT promoted to AXI SLVERR. Instead the corrupted flit is forwarded to its destination with `bresp/rresp = OKAY`, and any application-level integrity check (HBM ECC at endpoint, software CRC) handles recovery. This aligns with AMD Versal NoC stance — they too escalate uncorrectable ECC to a fatal interrupt rather than synthesizing rresp.

### Visual asset

Register layout view: `ERR_STATUS[3:0]` + `IRQ_ENABLE[3:0]` + `LAST_ERR_INFO` field map side-by-side. PPT-drawn.

### AMD verbatim quotes

> "Uncorrectable ECC errors result in a fatal interrupt."
>
> *— AMD pg313 §Data Integrity*

> "By default, all interrupts are masked."
>
> *— AMD pg313 §Data Integrity*

- ✗ skip: Versal-specific "fatal interrupt" exception escalation policy.
