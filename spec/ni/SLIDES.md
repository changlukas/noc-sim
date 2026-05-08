# NI Presentation Slides

**Source of truth** for slide content — PPT / Google Slides drafting consumes this file.

- **Audience:** internal review (engineers familiar with AXI4 + general NoC concepts).
- **Style:** IP-datasheet (AMD pg313 §NoC Master Unit / §NoC Slave Unit pattern). Each slide is one cohesive description with bullets + (where applicable) a simple block diagram or table; speaker notes belong in the PPT speaker-notes pane.
- **Deck length:** 12 slides.
- **Drafting status:** A5 wave 2026-05-08, in progress.

**Drafting progress:**

| Batch | Slides | Status |
|---|---|---|
| 1 | 1-6 (Title / Components / NMU overview / Address Map / QoS / ECC) | ✓ drafted (deeper-style retro-touch) |
| 2 | 7-12 (RoB / NSU overview / Excl Monitor / Downsize / Credit / Closing) | pending |

---

## Slide 1 — Title + Scope

> **AXI4-over-NoC Network Interface**
>
> Design walkthrough — v0.4.0, A5 wave 2026-05-08

**Scope:**

- Per-tile single-chimney NI — NMU + NSU share one NoC link pair.
- Deck covers NI scope only; NoC fabric router (NPS) is a separate spec.
- Chapter ordering follows AMD pg313 NoC Architecture.

**Speaker notes:** This deck introduces the AXI4-over-NoC Network Interface block. The NI is the per-tile chimney that converts AXI4 traffic to and from NoC flits — both manager-side ingress (NMU) and subordinate-side egress (NSU) live inside one NI. We follow AMD pg313's chapter ordering so reviewers familiar with Versal NoC have a direct cross-reference; design choices that diverge from AMD are called out explicitly. The router (NPS in AMD's terminology) is adjacent infrastructure with its own spec.

---

## Slide 2 — NoC Components Overview

**Inside the NI:**

- **NMU** (Network Manager Unit) — receives AXI from local master; injects request flits onto the NoC.
- **NSU** (Network Subordinate Unit) — receives request flits from the NoC; drives AXI to local slave.
- **CSR file** — software-visible runtime control (QoS / Probes / Errors / Quiesce / Exclusive Monitor clear).
- `irq_o` — single level-sensitive interrupt to host CPU.

**Per-tile single-chimney pattern:**

| Link | NMU | NSU |
|---|---|---|
| Outbound request | drives | — |
| Inbound request | — | samples |
| Outbound response | — | drives |
| Inbound response | samples | — |

NMU and NSU can be independently enabled. One NI per tile. **Tiles with multiple IPs (CPU + DMA + accelerator) mux through an upstream AXI crossbar before reaching the NI** — per-IP identification is by AXI ID, not by any flit-header field, which keeps the flit format compact.

**Visual asset:** Top-level block diagram — one tile showing CPU / DMA / accelerator on the left → upstream AXI crossbar → NI block (NMU + NSU + CSR) → NoC fabric. Dashed arrows for response path mirror; `irq_o` line out to CPU.

**Speaker notes:** The NI is the boundary between AXI4 protocol on the host side and the NoC's flit protocol on the fabric side. Inside the NI sit two functionally independent halves — NMU and NSU — that share the physical NoC link wires (single-chimney). They never communicate inside the NI; their only coupling is the shared link. This matches FlooNoC's `floo_axi_chimney.sv` topology and AMD pg313's NMU + NSU separation. Tiles with multiple IPs share one NI: an upstream AXI crossbar arbitrates them, and inside the NoC they're distinguished by AXI ID alone — no per-IP field is added to the flit header. The CSR file is software's window: QoS configuration, performance probes, error status (3 RW1C event-class bits in v0.4.0), quiesce control for the NMU, Exclusive Monitor clear trigger for the NSU. `irq_o` is a single level-sensitive line asserted when any unmasked error-status bit is set.

---

## Slide 3 — NMU (Network Manager Unit) overview

**The NMU provides:**

- Asynchronous clock domain crossing and rate matching between the AXI master and the NoC.
- Conversion from/to AXI4 protocol to NoC flit format.
- Address matching and route control — three routing modes (→ Slide 4).
- WRAP / INCR / FIXED burst support.
- Read re-ordering via Reorder Buffer (RoB) — three modes for area / reorder tradeoff (→ Slide 7).
- Write order enforcement — W-burst kept contiguous on the egress link.
- Ingress QoS control — four modes (→ Slide 5).
- Two-layer ECC integrity — per-hop routing parity + end-to-end whole-flit SECDED (→ Slide 6).
- AXI4 Exclusive Access support (forwarded to NSU Exclusive Monitor).
- Configurable AXI data width: 64, 128, 256, or 512 bits.
- Up to 32 outstanding AXI reads + 32 outstanding AXI writes.

**Block diagram (right side, ref.jpg style — simple labeled boxes):**

```
AXI Slave I/F                  NMU                        NoC
(Async Boundary)
  AW ────►   ┌────────────────────────────────────┐
  W  ────►   │  Address Map  →  Packetizing       │
  AR ────►   │                  ↓                 │  ──►
  B  ◄────   │  ECC Gen     →   QoS Order Control │
  R  ◄────   │                  ↓                 │  ◄──
             │  De-packetizing  VC Mapping        │
             │      ↑           ↓                 │
             │  ECC Check  ←  Read Re-Ordering    │
             └────────────────────────────────────┘
```

**AMD verbatim (inline):** *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."* (AMD pg313 §NoC Master Unit)

**Speaker notes:** Walk through the data flow. AW/AR enter from the left → Address Map resolves the destination tile ID → Packetizing assembles the flit payload → QoS Order Control assigns the per-flit qos field → ECC Gen attaches the per-hop routing parity bit and the whole-flit SECDED syndrome → VC Mapping picks the egress virtual channel → flit injected on the outbound request link. Each sub-block is a registered stage; the request-to-NoC critical path is short (a handful of cycles) end-to-end, plus async FIFO crossing into the NoC clock domain. Response path is the mirror: inbound response flit → ECC Check (silent single-bit correct, log-on double-bit) → Read Re-Ordering enforces per-AXI-ID response order → De-packetizing reconstructs B/R for the AXI master. We're conservative on outstanding count (32 vs AMD's 64) and we don't chop bursts — one wide flit carries one AXI message end-to-end. Our wide flit format makes AMD's 256-byte chopping unnecessary.

---

## Slide 4 — Address Map mechanism

**Three routing modes** — selected at design time:

| Mode | Mechanism | Use case |
|---|---|---|
| **XY-routed** (default) | Bit-extraction from AXI `awaddr` / `araddr` — configurable bit fields decode to (X, Y) mesh coordinate. | Regular 2D mesh deployments. |
| **Source-routed** | Pre-computed flit-header route path. | Static / compile-time route optimization. |
| **ID-table** (SAM) | System Address Map lookup — match address ranges to destination tile IDs. | Multi-region address space; software-defined region mapping. |

**Default configuration:**

- `X_WIDTH = 4 bits`, `Y_WIDTH = 4 bits` → 8-bit destination tile ID — supports up to 16 × 16 mesh = 256 tiles.
- Address bit-extraction offsets configurable per axis at design time.

**Bit-field example (XY-routed mode, default offsets):**

```
   awaddr [63:0]
            │
            ├─ bits [39:36] → Y coordinate (4 bits)
            ├─ bits [35:32] → X coordinate (4 bits)
            └─ bits [31:0]  → local address (passes through to destination NSU)
```

**Plus:**

- SAM table is a compile-time parameter — no runtime modification in v0.4.0 (re-elaborate to change).
- Address translation runs in parallel with packetizing — no extra pipeline-stage cost.

**Visual asset:** Bit-field decomposition figure of `awaddr` (above ASCII tree, redrawn in PPT for clarity).

**Speaker notes:** Three routing modes give integrators flexibility for different topologies. XY-routed is the default for regular meshes — fast, simple, deterministic. Source-routed suits pre-computed paths where the route is known at flit-construct time. ID-table maps an AXI region range to the destination tile ID via a compile-time SAM lookup table — useful when the address space is divided into regions belonging to different tiles. For the default configuration: 4-bit X plus 4-bit Y supports up to a 16 × 16 mesh with 256 destination tiles. The address-bit offset is a per-axis design-time parameter — integrators choose where in `awaddr` the coordinates live, based on their address-space layout. The `local address` portion is passed through unchanged to the destination NSU, which uses it to drive the local AXI slave. We deliberately picked a uniform 3-mode selector instead of AMD Versal's Master-Specified ID + Re-mapping dual mechanism, which is tied to their PL-interconnect coupling.

---

## Slide 5 — QoS Generator

**Four modes** — selectable at runtime via CSR:

| Mode | Behavior |
|---|---|
| Bypass | Pass through AXI awqos / arqos directly. |
| Fixed | Override every flit with a CSR-set fixed value. |
| Bandwidth-limiter | Drop priority when traffic exceeds the configured bandwidth. |
| Urgency-regulator | Feedback-controlled urgency escalation against a bandwidth target. |

**Typical per-master deployment:**

| Master profile | Recommended mode | Rationale |
|---|---|---|
| CPU | Bypass | CPU has its own qos discipline; trust the master |
| DMA engine | Bandwidth-limiter | High-throughput but bursty; cap prevents starving others |
| Real-time accelerator | Urgency-regulator | Needs adaptive priority to meet bandwidth target |
| Test / debug | Fixed | Force uniform priority for reproducibility |

**Plus:**

- NMU-only feature; NSU inherits per-flit qos via response-side metadata (no QoS recomputation on the response path).
- QoS does not preempt wormhole-locked W-bursts — arbitration grain is per-packet (HEAD flit), not per-flit.

**Visual asset (optional):** Bandwidth-vs-priority graph for Limiter / Regulator modes, showing how priority drops on overflow (Limiter) or escalates on under-target throughput (Regulator).

**Speaker notes:** Four QoS modes span a spectrum from purely passthrough (Bypass — trust the AXI master's own qos) to actively shaping (Regulator — feedback loop). A typical SoC mix has a CPU master in Bypass, a DMA in Limiter, and a real-time accelerator in Regulator — each picks the mode that matches the master's behavior profile. Bypass is appropriate when AXI masters already produce well-behaved qos. Fixed lets an integrator force a single uniform priority for a port, useful for test or for trivial low-priority masters. Bandwidth-limiter caps a noisy master to keep it from starving others on the same fabric. Urgency-regulator is the most adaptive — it watches observed response bandwidth and escalates urgency (raising the effective qos) when the master falls behind a target. AMD Versal NoC uses a different model — per-NPS Differentiated QoS with cycle-level scoring — which is router-side and Versal-specific. Wormhole-lock interaction: once a W-burst's HEAD flit is granted at any arbitration point, the entire burst holds the chosen output port until `wlast`. A higher-QoS packet arriving mid-burst cannot preempt — it waits for the lock to release.

---

## Slide 6 — ECC Scheme

**Three integrity layers:**

1. **Per-hop routing parity** — protects routing-critical fields (destination ID + last-flit indicator) at every router and at the destination NI sink. Mismatched flits dropped at detection.
2. **End-to-end whole-flit SECDED ECC** — covers the entire flit. Generated at source NI, checked only at destination NI sink. Routers do not check or regenerate.
3. **AXI host-side parity** (optional sideband, on by default) — verified at the AXI boundary; logged but not enforced as SLVERR.

**Per-layer summary (gen / check points):**

| Layer | Generated at | Checked at |
|---|---|---|
| Per-hop routing parity (dst_id + last-flit indicator) | NMU / NSU on flit injection | every router output **+** destination NI sink (every hop) |
| End-to-end whole-flit SECDED | NMU / NSU on flit injection | destination NI sink only (no router check) |
| AXI host-side parity (data + address, per-byte) | AXI master, NSU output | NMU input, AXI slave input, NMU output |

**AMD verbatim (used directly on slide):**

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

**Error reporting policy:**

Fabric ECC and parity errors raise an interrupt and increment counters but **never synthesize SLVERR** on AXI rresp / bresp. The corrupted flit is forwarded with `OKAY`; downstream / application-level integrity (HBM ECC at endpoint, software CRC) handles recovery. AXI rresp / bresp is reserved for end-to-end memory errors only. **No fabric-driven SLVERR synthesis in v0.4.0.**

**Visual asset:** AMD pg313 §Data Integrity *End-to-End Protection* figure (X25510070521) — file `images/End-to-End Protection.png`. The figure's 4 rows map to our 3 layers as follows:

- AMD *Data Parity* + AMD *Addr Parity* → our **AXI host-side parity** (one layer covering both).
- AMD *ECC* → our **end-to-end whole-flit SECDED**.
- AMD *DST ID Parity* → our **per-hop routing parity** (`route_par` covers DST ID + last-flit indicator).

Caption to overlay on slide: *"Source: AMD pg313 §Data Integrity. Mapping to our scheme: AMD Data + Addr Parity = our AXI parity layer; AMD ECC = our whole-flit SECDED; AMD DST ID Parity = our per-hop routing parity (dst_id + last)."*

**Speaker notes:** Three layers of integrity, each at a different scope. Per-hop routing parity is the cheapest check — a 1-bit XOR over the destination-ID and last-flit-indicator fields. Routers verify it on every output port; if parity fails, the flit is dropped immediately rather than risking misroute to the wrong tile. Whole-flit SECDED is the heavyweight check — covers the full flit (header plus payload, excluding the syndrome itself); generated at the source NI and checked only at the destination. Routers don't check it because doing so would add roughly one cycle per hop and an order of magnitude more gates than the parity bit. AXI host-side parity is the optional third layer at the AXI boundary, per-byte for both data and address — verified but never escalated to SLVERR. The AMD figure on the right shows exactly this pattern, drawn for the Versal NoC; our scheme implements every row of that figure with the small relabel that AMD's separate Data and Addr Parity rows correspond to our single combined AXI host-side parity layer. The error-reporting policy in v0.4.0 is uniform: no fabric event ever synthesizes a SLVERR back to the AXI master. All fabric-side errors surface through the CSR + IRQ path only. AXI rresp / bresp is reserved for end-to-end memory errors (HBM / DDR endpoint ECC propagating to the master via natural rresp). This is consistent with — and a clean uniform extension of — AMD's stance on uncorrectable ECC.
