# NI Presentation Outline (A5 wave, 2026-05-08; rewrite v2)

**Target deck:** 9 slides, AMD pg313 NoC Architecture chapter-aligned ordering, IP-datasheet style.

**Audience assumption:** internal review — engineers familiar with AXI4 + general NoC concepts. **This deck introduces the design at architectural level — it is not an implementation walkthrough.**

**Style reference:** AMD pg313 §NoC Master Unit / §NoC Slave Unit. Each major component is presented as one cohesive description: bullet capability list ("What it provides") followed by flowing prose paragraphs covering data flow and sub-block hand-offs. Concrete numbers (counts / sizes / widths) belong on slides; internal names (parameters, register / signal / rule IDs, algorithm internals) stay out of the deck and live only in the spec docs.

**AMD content reuse legend:**
- ✓ Verbatim — quote AMD directly with `per AMD pg313 §<Section>` attribution.
- ⚠ Rephrase — concept aligns; adapt Versal numbers to ours.
- ✗ Skip — Versal-specific (PL / AI Engine / CIPS / HBM variants / 8×8 Switch / NMU-NSU variants / chopping / DDR interleave) or design diverges.

---

## Slide-by-slide outline

### Slide 1 — Title + Scope

**AMD pg313 ref.** —

**What this slide covers.** Title page. Three-bullet scope statement: per-tile single-chimney; NI-only (router separate); chapter ordering follows AMD pg313 NoC Architecture.

**Visual asset.** None (title slide).

**AMD content classification.** —

---

### Slide 2 — NoC Components Overview

**AMD pg313 ref.** §NoC Architecture / §NoC Components.

**What this slide covers.** What sits inside the NI block — NMU + NSU + CSR file + level-sensitive interrupt. Per-tile single-chimney pattern: NMU drives the egress request link and samples the ingress response link; NSU samples the ingress request link and drives the egress response link. Both halves can be independently enabled. Router fabric is adjacent (covered briefly on Slide 5).

**Visual asset.** NMU + NSU block diagrams shown side-by-side (Option B). Drift fix prereq — see §Open issues #1.

**AMD content classification.**
- ✓ Generic NMU/NSU/NPS role description.
- ✗ Versal-specific component coupling (PL master / AI Engine / CIPS).

---

### Slide 3 — NMU (Network Manager Unit)

**AMD pg313 ref.** §NoC Master Unit.

**Slide structure** (per AMD §NMU pattern): "What it provides" bullet list + flowing prose paragraphs.

**Bullet topics** (~13 items, each one capability or concrete parameter value):
async clock crossing · AXI4 ↔ NoC flit conversion · address translation (3 routing modes) · WRAP / INCR / FIXED burst · per-AXI-ID Reorder Buffer (3 modes) · write ordering (W-burst contiguity) · Ingress QoS (4 modes) · Exclusive Access support · data width 64 / 128 / 256 / 512 · up to 32+32 outstanding · wide flit / no chopping · two-layer integrity.

**Prose narrative** (~5 short paragraphs): position + clock-domain split → packetizing path → RoB allocation → QoS + ECC + inject → response path.

**Visual asset.** `images/NMU_block_diagram.md` — drift fix prereq (D1/D3/D5/D9/D11).

**AMD content classification.**
- ✓ Verbatim: *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*
- ⚠ Adapt: AMD WRAP-for-32/64/128-bit → our 64–512; AMD 64 outstanding → our 32; AMD RROB 64×32-byte → our parameterised RoB.
- ✗ Skip: NMU Versions (NMU512 / HBM_NMU / NMU128); AXI4-Stream; DDR controller interleaving; 256-byte chopping; latency-optimized variant; 512B Write Buffer.

---

### Slide 4 — NSU (Network Subordinate Unit)

**AMD pg313 ref.** §NoC Slave Unit.

**Slide structure** (per AMD §NSU pattern): "What it provides" bullet list + flowing prose paragraphs.

**Bullet topics** (~10 items):
async clock crossing (NoC ↔ AXI slave) · NoC flit ↔ AXI4 conversion · W-burst reassembly · data-width down-conversion · AXI4 Exclusive Access (per-AXI-ID monitor, software-clearable, up to 8 concurrent reservations) · read-response buffering · response-side integrity (per-hop parity + end-to-end SECDED on outbound flits) · QoS / ordering metadata inherited from request flit.

**Prose narrative** (~4 short paragraphs): position + clock-domain split → receive path (flit → unpack → reassemble → downsize → drive AXI slave) → Exclusive Monitor → response path (capture B/R → buffer → integrity → inject); MetaBuffer carries inherited request metadata for response.

**Visual asset.** `images/NSU_block_diagram.md` — drift fix prereq (D2/D5/D9).

**AMD content classification.**
- ✓ Verbatim: *"Conversion of NoC packetized data (NPD) to and from AXI protocol data."* / *"buffered before forwarding to minimize bubbles."* / *"AXI exclusive access handling."*
- ✗ Skip: NSU Versions (NSU512 / NSU128 / DDRMC-NSU / HBM_NSU); AXI ID Compression.

---

### Slide 5 — NoC Packet Switch (adjacent / out-of-NI-scope)

**AMD pg313 ref.** §NoC Packet Switch.

**What this slide covers.** Boundary slide. NoC fabric routers sit between NMU and NSU; their detailed spec is separate. Adjacent concerns relevant to NI:
- Per-VC arbitration runs at cycle level inside the router; NI itself only does flit-construct-time VC mapping.
- Per-hop routing-parity check happens at every router output; whole-flit SECDED is end-to-end (routers do not check).
- Credit-based flow control is the inter-component contract — covered on Slide 6.

**Visual asset.** Simple block diagram: NMU → router(s) → NSU.

**AMD content classification.**
- ✓ Verbatim (cite as router scope, out of NI): *"For every cycle, each output port performs Least Recently Used (LRU) arbitration on all virtual channels of the three input ports."*
- ✗ Skip: 8×8 Switch internal; 24-token register; Differentiated QoS specifics.

---

### Slide 6 — Credit-Based Flow Control

**AMD pg313 ref.** §Credit-Based Flow Control.

**Slide structure.** Heavy on the AMD verbatim paragraph (single highest-verbatim slide of the deck). Pair with operational implications.

**Verbatim quote (entire AMD paragraph applies to our NI directly):**
> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."*
> *— AMD pg313 §Credit-Based Flow Control*

**Operational implications (added by us):**
- Bi-directional credit-init handshake at startup before any flits flow.
- Per-VC credit accounting at the source; receiver returns one credit per VC per cycle.
- A source asserts a flit only when ≥ 1 credit is held on the chosen VC.
- Persistent credit starvation = permanent stall on the affected VC (no automatic SLVERR escalation in v0.4.0).

**Visual asset.** Sequence diagram: post-reset → init handshake → credit exchange begins → flit injection → credit return.

**AMD content classification.**
- ✓✓ Full paragraph verbatim — strongest reuse of the deck.

---

### Slide 7 — NoC Communication: Read / Write + QoS

**AMD pg313 ref.** §NoC Communication §Read Transactions / §Write Transactions / §Quality of Service.

**What this slide covers.**
- Read transaction flow (end-to-end): AXI master AR → NMU → request flit → router fabric → NSU → local AXI slave; response path mirror, ordered through the per-AXI-ID Reorder Buffer at the originating NMU.
- Write transaction flow: AW + W flits per burst (W kept contiguous); B response routed back.
- Ingress QoS — four modes (NMU only; NSU inherits via response metadata): bypass / fixed / bandwidth-limiter / urgency-regulator.

**Visual asset.** End-to-end transaction flow diagram (NMU → router → NSU and back).

**AMD content classification.**
- ⚠ Generic Read/Write transaction flow narrative usable.
- ✗ Versal-specific Differentiated QoS / NPS-side QoS scoring.

---

### Slide 8 — Data Integrity

**AMD pg313 ref.** §NoC Communication §Data Integrity / §Parity.

**Slide structure.** Two-layer fabric integrity scheme + AXI host-side parity + (B)-philosophy error reporting policy. Heavy on AMD verbatim (second-highest after Slide 6).

**Layered scheme:**
1. **Per-hop routing parity** — protects routing-critical fields (destination ID + last-flit indicator) at every router and at the destination NI sink. Mismatched flits are dropped at detection.
2. **End-to-end whole-flit SECDED ECC** — covers the entire flit. Generated at the source NI, checked only at the destination NI sink. Routers do not check or regenerate.
3. **AXI host-side parity** (optional sideband, on by default) — verified at the AXI boundary; logged but not enforced as SLVERR.

**Verbatim quotes for the slide:**
> *"SECDED ECC across the entire flit."* — AMD pg313 §Data Integrity
>
> *"No ECC checking is performed in the switch fabric."* — AMD pg313 §Data Integrity
>
> *"1 bit per byte for Data."* / *"1 bit per byte for AxAddress."* — AMD pg313 §Parity
>
> *"The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity."* — AMD pg313 §Parity
>
> *"By default, all interrupts are masked."* — AMD pg313 §Data Integrity
>
> *"Uncorrectable ECC errors result in a fatal interrupt."* — AMD pg313 §Data Integrity

**Error reporting policy (frame as design choice on slide):** Fabric ECC errors raise an interrupt and increment counters but never synthesize SLVERR on AXI rresp / bresp. The corrupted flit is forwarded with `OKAY`. Downstream / application-level integrity (HBM ECC, software CRC) handles recovery. AXI rresp / bresp channels are reserved for end-to-end memory errors only — no fabric-driven SLVERR synthesis in v0.4.0.

**Visual asset.** Two-layer schematic: per-hop parity check at each router; SECDED check only at destination NI; AXI parity check at host boundary.

**AMD content classification.**
- ✓✓ Multiple verbatim quotes directly applicable.
- ✗ Skip: HBM_NMU per-32-bit/per-24-bit exception; address-remap 7-bit parity map; Data Poisoning.

---

### Slide 9 — Closing

**AMD pg313 ref.** —

**What this slide covers.**
- DV plan headline summary — protocol-rule count, testpoint count, ABV / FPV scope, framework choice (UVM 1.2).
- Future work — atomics support deferred; v0.5.0 plugin-side Protocol Reference Library.
- Q & A.

**Visual asset.** Optional spec-deliverable summary table (rule count / testpoint count / coverage targets) as a tidy slide-end artifact.

**AMD content classification.** —

---

## AMD content reuse cross-cut summary

| Slide | Strongest AMD verbatim leverage |
|---|---|
| 3 NMU | clock-crossing tagline; "32 outstanding reads + 32 outstanding writes" pattern |
| 4 NSU | "Conversion of NPD ↔ AXI"; "buffered before forwarding to minimize bubbles"; "AXI exclusive access handling" |
| 5 NPS context | LRU VC arbitration one-liner (cite as router scope, out of NI) |
| 6 Credit | **full Credit Based Flow Control paragraph (highest verbatim density)** |
| 7 Communication | high-level Read/Write narrative |
| 8 Data Integrity | **8 verbatim sentences across §Parity + §Data Integrity (second-highest verbatim density)** |

**Slides where we deliberately diverge from AMD (frame as design choice on slide):**
- Slide 3 NMU — no chopping (single wide flit per AXI message); 32 outstanding (vs AMD's 64).
- Slide 8 Data Integrity — fabric ECC errors raise interrupt only, never SLVERR (the (B)-philosophy lands here).

---

## Drafting progress

| Batch | Slide | Status |
|---|---|---|
| A | 1 Title + 2 Components | pending |
| B | 3 NMU | pending |
| C | 4 NSU | pending |
| D | 5 NPS + 6 Credit | pending |
| E | 7 Communication + 8 Data Integrity | pending |
| F | 9 Closing | pending |

Old `SLIDES.md` (15-slide / 4-section meta-template version, slides 1-8 drafted) is superseded by this v2 plan. New `SLIDES.md` will overwrite the old content; old version is recoverable from git history if needed.

---

## Open issues

1. **NMU/NSU block-diagram drift** — 7 confirmed stale items + 3 coverage gaps from cumulative audit (D1/D2 `VC Arbiter`→`VC Mapping` in NMU+NSU mermaid; D3 `route_par` 9-bit cover not 16; D5 `port_id` removed; D9 stamp `post-A4.5`→`post-A5`; NSU CSR-link missing; D11 NMU mermaid header `Network Master Unit`→`Network Manager Unit`; D12 `doc/theory_of_operation.md:477` RTL block-diagram mermaid `VCARB_O / VCARB_S` use `VC Arbiter` while §sub-module description (line 508) uses `VC Mapping` — internal ToO inconsistency, touches source spec not only images). Slides 3 / 4 visuals require drift fix before final render.
2. **`flit_ecc` 396-bit coverage** — ✓ resolved 2026-05-08 (per `doc/theory_of_operation.md:369-371`).
