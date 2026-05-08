# NI Presentation Outline (A5 wave, 2026-05-08; rewrite v3)

**Target deck:** 11 slides, IP-datasheet style, **overview + design-decision-expansion** pattern.

**Audience assumption:** internal review — engineers familiar with AXI4 + general NoC concepts. **This deck introduces the design at architectural level — it is not an implementation walkthrough.**

**Style reference:** AMD pg313 §NoC Master Unit / §NoC Slave Unit. Each component overview slide presents one cohesive description: bullet capability list ("What it provides") + simple labeled block diagram on the right (ref.jpg style). Each design-decision-expansion slide focuses on one decision (3 RoB modes, 4 QoS modes, two-layer ECC, etc.) with table or bullet form. Concrete numbers belong on slides; internal parameter / signal / rule-ID names live in spec docs only.

**AMD content reuse legend:**
- ✓ Verbatim — quote AMD directly with `per AMD pg313 §<Section>` attribution.
- ⚠ Rephrase — concept aligns; adapt Versal numbers to ours.
- ✗ Skip — Versal-specific (PL / AI Engine / CIPS / HBM variants / 8×8 Switch / NMU-NSU variants / chopping / DDR interleave) or design diverges.

**Spec context (post-A5 cleanup):** Outstanding-tx Timeout feature deleted in v0.4.0. ERR_STATUS / IRQ_ENABLE bits renumbered to compact [0]/[1]/[2] = ecc_uncorr / route_par / axi_parity. Credit starvation = permanent stall (no SLVERR escalation). Quiesce is best-effort. AXI rresp/bresp reserved for end-to-end memory errors only — no fabric-driven SLVERR synthesis in v0.4.0.

---

## Slide-by-slide outline

### Slide 1 — Title + Scope

**Type.** Title.

**AMD pg313 ref.** —

**What this slide covers.** Title page + 3-bullet scope: per-tile single-chimney NI; deck covers NI scope only (router NPS is separate); chapter ordering follows AMD pg313 NoC Architecture.

**Visual asset.** None (or small tile thumbnail in corner).

**AMD content classification.** —

---

### Slide 2 — NoC Components Overview

**Type.** Overview.

**AMD pg313 ref.** §NoC Architecture / §NoC Components.

**What this slide covers.** What sits inside the NI block — NMU + NSU + CSR file + level-sensitive `irq_o`. Per-tile single-chimney: NMU drives egress request link + samples ingress response link; NSU samples ingress request link + drives egress response link. Both halves independently enabled. NoC fabric router (NPS) is adjacent — touched briefly on Slide 10.

**Visual asset.** Simple top-level diagram: AXI master → NMU → router → NSU → AXI slave, with response path mirror; CSR + irq_o on the side.

**AMD content classification.**
- ✓ Generic NMU/NSU/NPS role description.
- ✗ Versal-specific component coupling (PL master / AI Engine / CIPS).

---

### Slide 3 — NMU (Network Manager Unit) overview

**Type.** Overview (ref.jpg style — bullet list + simple block diagram).

**AMD pg313 ref.** §NoC Master Unit.

**Bullet list (left) — "The NMU provides":**

- Asynchronous clock domain crossing and rate matching between the AXI master and the NoC.
- Conversion from/to AXI4 protocol to NoC flit format.
- Address matching and route control — three routing modes (XY-routed / source-routed / ID-table).
- WRAP / INCR / FIXED burst support.
- Read re-ordering via Reorder Buffer (RoB) — three modes for area / reorder tradeoff (→ Slide 5).
- Write order enforcement — W-burst kept contiguous on egress link.
- Ingress QoS control — four modes: bypass / fixed / bandwidth-limiter / urgency-regulator (→ Slide 6).
- Two-layer ECC integrity — per-hop routing parity + end-to-end whole-flit SECDED (→ Slide 4).
- AXI4 Exclusive Access support (forwarded to NSU Exclusive Monitor).
- Configurable AXI data width: 64, 128, 256, or 512 bits.
- Up to 32 outstanding AXI reads + 32 outstanding AXI writes.

**Block diagram (right) — simple labeled boxes, ref.jpg style:**

AXI Slave Interface (Async Data Boundary Crossing) on left with AW/W/AR/R/B; NMU box in middle containing: Address Map · Packetizing · QoS Order Control · ECC Gen · VC Mapping · ECC Check · De-packetizing · Read Re-Ordering; NoC on right.

**AMD content classification.**
- ✓ Verbatim: *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*
- ⚠ Adapt: AMD WRAP-for-32/64/128-bit → our 64–512; AMD 64 outstanding → our 32; AMD RROB 64×32-byte → our parameterised RoB.
- ✗ Skip: NMU Versions (NMU512 / HBM_NMU / NMU128); AXI4-Stream; DDR controller interleaving; 256-byte chopping; latency-optimized variant; 512B Write Buffer; AXI ID Compression.

---

### Slide 4 — ECC Scheme (NMU + cross-cut)

**Type.** Design-decision expansion.

**AMD pg313 ref.** §NoC Communication §Data Integrity / §Parity.

**What this slide covers.** Two-layer integrity scheme — per-hop routing parity + end-to-end whole-flit SECDED — plus AXI host-side parity at the boundary, plus the (B)-philosophy error reporting policy.

**Layered scheme (3 layers):**
1. **Per-hop routing parity** — protects routing-critical fields (destination ID + last-flit indicator) at every router and at the destination NI sink. Mismatched flits dropped at detection.
2. **End-to-end whole-flit SECDED ECC** — covers entire flit. Generated at source NI, checked only at destination NI sink. Routers do not check or regenerate.
3. **AXI host-side parity** (optional sideband, on by default) — verified at AXI boundary; logged but not enforced as SLVERR.

**Verbatim quotes for the slide:**
> *"SECDED ECC across the entire flit."* — AMD pg313 §Data Integrity
>
> *"No ECC checking is performed in the switch fabric."* — AMD pg313 §Data Integrity
>
> *"1 bit per byte for Data."* / *"1 bit per byte for AxAddress."* — AMD pg313 §Parity
>
> *"The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity."* — AMD pg313 §Parity
>
> *"Uncorrectable ECC errors result in a fatal interrupt."* — AMD pg313 §Data Integrity
>
> *"By default, all interrupts are masked."* — AMD pg313 §Data Integrity

**Error reporting policy ((B)-philosophy):** Fabric ECC and parity errors raise an interrupt and increment counters but never synthesize SLVERR on AXI rresp / bresp. The corrupted flit is forwarded with `OKAY`; downstream / application-level integrity (HBM ECC, software CRC) handles recovery. AXI rresp / bresp reserved for end-to-end memory errors only.

**Visual asset.** Two-layer schematic: per-hop parity check at each router; SECDED check only at destination NI; AXI parity check at host boundary.

**AMD content classification.**
- ✓✓ Multiple verbatim quotes directly applicable (highest verbatim-density slide).
- ✗ Skip: HBM_NMU per-32-bit/per-24-bit exception; address-remap 7-bit parity map; Data Poisoning.

---

### Slide 5 — Reorder Buffer (RoB) types (NMU expansion)

**Type.** Design-decision expansion.

**AMD pg313 ref.** §NoC Master Unit §Read Reorder Buffer.

**What this slide covers.** Three RoB modes balancing area against reorder support. B-channel and R-channel modes are independently configurable.

**On-slide table (3 modes):**

| Mode | Area cost | Use case |
|---|---|---|
| NoRoB (default) | minimal — no allocation | NoC preserves same-source-same-dest in-order delivery |
| SimpleRoB | small — single shared release pointer | naive FIFO; cross-AXI-ID HoL blocking acceptable |
| NormalRoB | largest — per-AXI-ID linked-list + adaptive bypass | full out-of-order across AXI IDs; same-destination fast-path |

**Plus:**
- B and R RoB modes independent.
- Typical multi-destination deployment: NormalRoB on R, SimpleRoB on B (B is metadata-only).

**Verbatim quote for the slide:**
> *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."* — AMD pg313 §NoC Master Unit §Read Reorder Buffer

**Visual asset.** RoB state machine: FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE.

**AMD content classification.**
- ✓ Verbatim: linked-list RROB phrasing.
- ⚠ Adapt: AMD RROB 64×32-byte default → our 32 outstanding entries.

---

### Slide 6 — QoS Generator (NMU expansion)

**Type.** Design-decision expansion.

**AMD pg313 ref.** §NoC Communication §Quality of Service.

**What this slide covers.** NMU-side ingress QoS Generator with four modes selectable at runtime via CSR.

**On-slide table (4 modes):**

| Mode | Behavior |
|---|---|
| Bypass | Pass through AXI awqos / arqos directly. |
| Fixed | Override every flit with a CSR-set fixed value. |
| Bandwidth-limiter | When traffic exceeds the configured bandwidth, drop priority. |
| Urgency-regulator | Feedback-controlled urgency escalation against a bandwidth target. |

**Plus:**
- NMU-only feature; NSU inherits per-flit qos via response-side metadata.
- QoS does not preempt wormhole-locked W-bursts — arbitration grain is per-packet (HEAD flit), not per-flit.

**Visual asset.** Optional: bandwidth-vs-priority graph for Limiter / Regulator modes.

**AMD content classification.**
- ⚠ AMD's high-level QoS narrative usable.
- ✗ Skip: Versal-specific Differentiated QoS; NPS-side QoS scoring.

---

### Slide 7 — NSU (Network Subordinate Unit) overview

**Type.** Overview (ref.jpg style — bullet list + simple block diagram, mirror of Slide 3).

**AMD pg313 ref.** §NoC Slave Unit.

**Bullet list (left) — "The NSU provides":**

- Asynchronous clock domain crossing and rate matching between the NoC and the AXI slave.
- Conversion from/to NoC flit format to AXI4 protocol.
- W-burst reassembly before driving the local AXI slave.
- Data-width down-conversion when AXI slave is wider than NoC payload (→ Slide 9).
- AXI4 Exclusive Access — per-AXI-ID monitor with software-clearable reservation table (→ Slide 8).
- Read response buffering to decouple slave-side timing from NoC injection back-pressure.
- Response-side integrity — same two-layer ECC scheme on outbound response flits (per Slide 4).
- QoS / ordering metadata inherited from the inbound request flit (no NSU-side QoS recomputation).

**Block diagram (right) — simple labeled boxes, mirror of NMU layout:**

NoC on left; NSU box in middle containing: ECC Check · De-packetizing · W Reassembly · Downsize · Exclusive Monitor · Read Response Buffer · Packetizing B/R · ECC Gen · VC Mapping; AXI Master Interface (Async Data Boundary Crossing) on right with AW/W/AR/R/B (NSU is host-side AXI master, drives the local AXI slave).

**AMD content classification.**
- ✓ Verbatim: *"Conversion of NoC packetized data (NPD) to and from AXI protocol data."*
- ✓ Verbatim: *"buffered before forwarding to minimize bubbles."*
- ✓ Verbatim: *"AXI exclusive access handling."*
- ✗ Skip: NSU Versions (NSU512 / NSU128 / DDRMC-NSU / HBM_NSU); AXI ID Compression.

---

### Slide 8 — Exclusive Monitor (NSU expansion)

**Type.** Design-decision expansion.

**AMD pg313 ref.** §NoC Slave Unit (Exclusive Monitor sub-section).

**What this slide covers.** AXI4 Exclusive Access (LDREX/STREX-style atomic primitives via AxLOCK=Exclusive). NSU-side per-AXI-ID monitor table tracks pending Exclusive read reservations.

**On-slide bullets:**
- Per-AXI-ID reservation entry — captures `(axi_id, awaddr, awsize, awlen)` on Exclusive AR.
- Up to 8 concurrent reservations (configurable).
- Exclusive AW match → `bresp = EXOKAY`; mismatch (different ID, addr, or normal write to overlapping line) → write becomes normal write, `bresp = OKAY`.
- Software-clearable trigger via CSR — typical use case: OS bookkeeping when a process is killed mid-Exclusive.
- Single-NI scope — multi-master coherency across multiple NIs is out of v0.4.0 (would require directory or snoop protocol).

**Visual asset.** Reservation table state diagram: AR Excl → entry allocated → match check on AW Excl → invalidate on overlap normal write.

**AMD content classification.**
- ✓ Verbatim concept: "AXI exclusive access handling".
- ✗ Skip: Per-NSU-version reservation depth differences.

---

### Slide 9 — Downsize (NSU expansion)

**Type.** Design-decision expansion.

**AMD pg313 ref.** —  (AMD's AXI Conversion §AxSize/AxLen Conversion is a related concept; but our Downsize block is NSU-specific.)

**What this slide covers.** NSU data-width down-conversion when local AXI slave is wider than NoC payload (`DATA_WIDTH > FLIT_PAYLOAD_WIDTH`). Symmetric to NMU Upsize, but on the slave side.

**On-slide bullets:**
- W path: NSU breaks each wide W flit into multiple narrow AXI W beats matching the local slave's `DATA_WIDTH`. Lane mapping uses the original `awaddr` + per-beat offset.
- R path: NSU accumulates multiple narrow AXI R beats from the slave into wide R flits before injecting on the response link.
- No-conversion case (`DATA_WIDTH == FLIT_PAYLOAD_WIDTH`): block degenerates to pass-through.
- Per-port `DATA_WIDTH` is fixed at design time.
- `wstrb` honoured throughout — over-fetched lanes carry `wstrb=0` so the slave only commits the bytes the original master drove.

**Visual asset.** Conceptual schematic: 1 wide NoC W flit → N narrow AXI W beats; reverse on R path.

**AMD content classification.**
- ⚠ Concept aligns with AMD AXI Conversion; specifics differ (AMD has chopping, we don't; AMD has variants).
- ✗ Skip: AMD Memory Controller bandwidth-matching specifics.

---

### Slide 10 — Credit-Based Flow Control (cross-cut + NPS scope footnote)

**Type.** Cross-cut (applies to NMU + NSU + router).

**AMD pg313 ref.** §Credit-Based Flow Control + §NoC Packet Switch (NPS scope footnote).

**What this slide covers.** Credit-based flow control is the fabric-wide flit transfer contract. Single highest-AMD-verbatim slide of the deck.

**Verbatim quote (entire AMD paragraph applies to our NI directly):**
> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."*
> *— AMD pg313 §Credit-Based Flow Control*

**Operational implications:**
- Bi-directional credit-init handshake at startup before any flits flow.
- Per-VC credit accounting at the source; receiver returns one credit per VC per cycle.
- A source asserts a flit only when ≥ 1 credit is held on the chosen VC.
- Persistent credit starvation = permanent stall on the affected VC (no automatic SLVERR escalation in v0.4.0; software detects via PENDING counters / IRQ and handles externally).

**NPS (NoC Packet Switch) scope footnote — adjacent / out-of-NI-scope:**
- NoC fabric routers (NPS) sit between NMU and NSU; their detailed spec is separate.
- Per-VC arbitration runs at cycle level inside the router; NI itself only does flit-construct-time VC mapping. AMD pg313 verbatim (cite as router scope): *"For every cycle, each output port performs Least Recently Used (LRU) arbitration on all virtual channels of the three input ports."*
- Per-hop routing-parity check happens at every router output (covered on Slide 4 ECC).

**Visual asset.** Sequence diagram: post-reset → init handshake → credit exchange begins → flit injection → credit return.

**AMD content classification.**
- ✓✓ Full Credit-Based Flow Control paragraph verbatim.
- ✓ Verbatim (NPS scope): LRU VC arbitration one-liner.
- ✗ Skip: Versal 8×8 Switch internal; 24-token register; Differentiated QoS specifics.

---

### Slide 11 — Closing

**Type.** Closing.

**AMD pg313 ref.** —

**What this slide covers.**
- DV plan headline summary — protocol-rule count (post-A5: 136 rules = 126 FAIL + 10 RECOMMEND), testpoint count, ABV / FPV scope, framework choice (UVM 1.2).
- Future work — atomics support deferred; v0.5.0 plugin-side Protocol Reference Library; debug / safety mechanisms (Outstanding-tx Timeout, watchdog) revisited post-v1.
- Q & A.

**Visual asset.** Optional spec-deliverable summary table.

**AMD content classification.** —

---

## AMD content reuse cross-cut summary

| Slide | Strongest AMD verbatim leverage |
|---|---|
| 3 NMU overview | clock-crossing tagline; "32 outstanding reads + 32 outstanding writes" pattern |
| 4 ECC scheme | **8 verbatim sentences across §Parity + §Data Integrity (highest verbatim density alongside Slide 10)** |
| 5 RoB types | linked-list RROB phrasing |
| 6 QoS Generator | high-level Read/Write narrative |
| 7 NSU overview | "Conversion of NPD ↔ AXI"; "buffered before forwarding to minimize bubbles"; "AXI exclusive access handling" |
| 8 Exclusive Monitor | "AXI exclusive access handling" concept |
| 10 Credit + NPS | **full Credit-Based Flow Control paragraph (highest verbatim density)** + LRU VC arbitration one-liner |

**Slides where we deliberately diverge from AMD (frame as design choice on slide):**
- Slide 3 NMU — no chopping (single wide flit per AXI message); 32 outstanding (vs AMD's 64); no AXI ID Compression.
- Slide 4 ECC — fabric ECC errors raise interrupt only, never SLVERR ((B)-philosophy).
- Slide 6 QoS — modes follow FlooNoC + custom design, not AMD NPS Differentiated QoS.

---

## Drafting progress

| Batch | Slides | Status |
|---|---|---|
| A | 1 Title + 2 Components | pending |
| B | 3 NMU overview | pending |
| C | 4 ECC + 5 RoB + 6 QoS (NMU expansions) | pending |
| D | 7 NSU overview | pending |
| E | 8 Exclusive Monitor + 9 Downsize (NSU expansions) | pending |
| F | 10 Credit + NPS | pending |
| G | 11 Closing | pending |

Old `SLIDES.md` (8-slide / 4-section meta-template version) is superseded by this v3 plan. New `SLIDES.md` will overwrite the old content; old version recoverable from git history.

---

## Open issues

1. **NMU/NSU block-diagram drift** — earlier `images/*_block_diagram.md` mermaid drift items (D1–D12) noted in v2 are mostly **moot for the deck**: ref.jpg-style slide block diagrams are simple labeled boxes drawn directly in PowerPoint, not rendered from the heavy mermaid sources. Drift fix on `images/*.md` is a separate spec-level concern (still relevant for spec docs) but does not block slide drafting.
2. **`flit_ecc` 396-bit coverage** — ✓ resolved 2026-05-08 (per `doc/theory_of_operation.md` §ECC).
3. **Outstanding-tx Timeout** — ✓ resolved 2026-05-08 (deleted from spec; commit `0a2c458`).
