# NI Presentation Slides

Single source of truth for slide drafting. Technical terms, mode names, and AMD pg313 verbatim quotes stay in English. Signal names, register names, parameter constants, and rule IDs do not appear in slide bodies (they live in the spec docs). Each slide follows a Takeaway → Body → Source layout.

---

## Slide 1. AXI4-over-NoC Network Interface

**Design walkthrough, v0.4.0, A5 wave 2026-05-08**

**Takeaway:** An AXI4-NoC boundary IP with built-in QoS, ECC, and RoB. A single wide flit carries one AXI message end-to-end. No chopping overhead.

- **Scope**
  - One NI per tile, single-chimney topology.
  - Covers NMU, NSU, CSR file, and the interrupt output.
  - The fabric router (NPS) is a separate spec and is not expanded in this deck.
- **Chapter alignment**
  - Section order follows AMD pg313 NoC Architecture.
  - Reviewers familiar with Versal NoC can cross-reference section by section.
- **Design inspiration**
  - **AMD pg313 (Versal NoC):** two-layer ECC, Data Integrity policy, (B)-philosophy error reporting.
  - **Arteris FlexNoC:** 4-mode QoS Generator and the heterogeneous traffic class taxonomy.
  - **FlooNoC (PULP / ETH):** single-chimney structure, wormhole arbiter, RoB and MetaBuffer pattern.

> **Deck roadmap (14 slides):**
> Overview (S2 to S3) → NMU internals (S4) → Address map (S5) → QoS (S6) → ECC (S7 to S8) → RoB (S9) → NSU internals (S10) → Exclusive (S11) → Width conversion (S12) → Credit flow control (S13) → DV status and roadmap (S14)

---

## Slide 2. NoC Communication Overview

**Takeaway:** Push complexity to the edges. Keep routers simple and fast. Wide flits carry whole AXI messages end-to-end, removing serialization overhead.

- **Network access and relay**
  - NMU (Network Manager Unit) packetizes AXI4 / AXI4-Lite requests into NoC flits.
  - NoC router (NPS) performs cycle-level forwarding and VC arbitration.
  - NSU (Network Subordinate Unit) de-packetizes NoC flits and drives the local AXI slave.
- **Wide flit, no chopping**
  - One AXI4 message (header + data) traverses end-to-end as a single wide flit.
  - The longest AXI burst stays under one wormhole-lock, with no slicing or reassembly delay.
  - Trades flit width for clock-frequency headroom, removing the endpoint serialization tax that chopping-based NoCs pay.
- **Single unified fabric**
  - All protocols share one physical link plane. Multiplexing buys area savings.
  - Multiple IPs in a tile (CPU, DMA, accelerator) share one NI through an upstream AXI crossbar.
  - IP identity rides on the AXI ID. No extra flit-header field is needed.

> **Design philosophy (FlooNoC inspiration):**
> Move complexity to the edges. Keep routers simple and fast.
> Wide flits transmit entire AXI4 messages in a single cycle, with no chopping and no serialization tax.

> **NPS feature envelope (adjacent spec):** full-duplex switch, multiple virtual channels per port, credit-based flow control, two-cycle minimum through-switch latency, boot-time programmable routing table (per AMD pg313 §NoC Packet Switch).

---

## Slide 3. NoC Components

**Takeaway:** A boundary IP that isolates the AXI clock domain from the NoC clock domain. The fabric closes timing independently of external IPs.

- **NMU (Network Manager Unit)**
  - Accepts read and write requests from the local AXI master.
  - Packetizes them into request flits.
  - Receives response flits, reorders them through the RoB, and drives AXI responses back to the master.
- **NSU (Network Subordinate Unit)**
  - Receives request flits and drives the local AXI slave.
  - Collects slave responses, packetizes them into response flits, and injects them back into the fabric.
- **Per-tile single chimney**
  - NMU and NSU operate independently and share one NoC link pair.
  - Each half is independently enabled, supporting NMU-only, NSU-only, or both. One design covers different boundary-IP scenarios.
- **CSR file and interrupt**
  - A dedicated AXI4-Lite configuration port carries QoS, Probe, Error, and Quiesce settings.
  - One level-sensitive interrupt aggregates every unmasked error event.

---

## Slide 4. NMU Overview

**Takeaway:** Serializes AXI semantics into NoC flits. Handles address translation, QoS shaping, ECC generation, and ingress flow control at the boundary.

- **Protocol conversion**
  - Bidirectional conversion between AXI4's five channels and NoC packetized data.
  - Supports AXI data widths from 64-bit to 512-bit.
- **Clock crossing and rate matching**
  - Asynchronous bridge between the AXI domain and the NoC domain.
  - Async FIFOs absorb frequency mismatch and back-pressure variation.
- **Address map and route control**
  - Resolves the AXI address into a destination coordinate plus a local address.
  - Three routing modes detailed in Slide 5.
- **Ingress QoS control**
  - The 4-mode QoS Generator shapes priority before traffic enters the fabric (Slide 6).
- **Outstanding transactions tracker**
  - Tracks up to 32 outstanding reads and 32 outstanding writes independently.
  - Cross-AXI-ID out-of-order responses are reordered by the RoB (Slide 9).
- **Pipeline timing (RTL)**
  - AW / AR injection takes 1 or 2 cycles depending on the optional spill register.
  - CDC traversal adds 3 to 4 cycles each direction.
  - Synthesis target: 7nm process, 1.2 GHz on the NoC clock and 800 MHz on the AXI clock (representative).

> **Block diagram** (per `docs/images/NMU.png`, AMD pg313 §NoC Master Unit style):
> Request path: AXI Slave I/F → [Async Boundary] → Address Map → Packetizing → QoS Order Control → VC Mapping → ECC Gen → NoC.
> Response path: NoC → ECC Check → De-packetizing → Read Re-Ordering → AXI Slave I/F (mirror).

> **AMD pg313 verbatim:** *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*

---

## Slide 5. Address Decoding and Map

**Takeaway:** Three routing modes (regular mesh, pre-computed route, irregular topology) trade area, flexibility, and latency.

| Routing mode | Mechanism | Use case | Cost |
|---|---|---|---|
| **XY-routed (DOR)** | NI bit-extracts (X, Y) from the AXI address. Routers compute next direction from XY arithmetic. | Regular 2D mesh with top-down address layout. | Combinational. No SRAM. Deadlock-free by construction. |
| **Source-routed** | NI looks up the AXI address in its SAM table and fetches a pre-computed port sequence into the flit header. Each router pops the next port. | Custom paths in non-XY topology. Load balancing or fault avoidance. | NI SAM. Wider flit header (route bits grow with mesh diameter). Cycle-free routes are the integrator's responsibility. |
| **ID-table** | NI looks up the AXI address in its SAM table and fetches a numeric destination ID. Each router consults its elaboration-time id-to-port mapping. | Arbitrary topology with multiple address ranges per destination, or address layout decoupled from mesh coordinates. | NI SAM plus per-router id-to-port mapping. Cycle-free mappings are the integrator's responsibility. |

- **Topology range**
  - Baseline configuration: 4 × 4 mesh, 16 tiles.
  - Same parameter envelope scales up to 16 × 16, 256 tiles.
- **All routing structures are compile-time only**
  - NI SAM (when used) and per-router id-to-port mappings are RTL parameters baked at elaboration.
  - Not runtime-modifiable in v0.4.0. Re-elaborate to change.
- **No routing-mode latency penalty**
  - All three modes complete address resolution in the same cycle as packetizing.
  - SAM lookup (Source-routed and ID-table) is combinational and folded into the packetize stage.

> **Design difference:** AMD Versal NoC pairs Master-Specified ID with Re-mapping. This NI exposes a uniform 3-mode selector instead and lets the integrator pick one per topology.

---

## Slide 6. End-to-End Quality of Service

**Takeaway:** A 4-mode QoS Generator delivers Arteris-style coordinated control of latency, bandwidth, arbitration, and flow management. Heterogeneous IPs stop interfering with each other.

**Heterogeneous traffic classes (Arteris verbatim):**

| Initiator | Traffic Profile | Reason |
|---|---|---|
| **CPU** | *Latency sensitive* | Processing stops for many cycles when there is a cache miss. |
| **Video Display** | *Real time & latency critical* | The video display subsystem's buffer must never be empty or the end-user will see black pixels. |
| **Imaging System** | *Real time & bandwidth sensitive* | The imaging system operates on several frames in advance and is able to adjust its output quality. |
| **Background File Download** | *Best effort* | File downloads can be stalled without compromising the end user's experience. |

- **4-mode QoS Generator (CSR-programmable, runtime-selectable)**
  - **Bypass:** pass the AXI master's qos hint through. Trust the master's own classification.
  - **Fixed:** override every flit with a fixed value. Used for tests and trivial masters with no QoS scheme.
  - **Bandwidth-Limiter (Cap):** drop qos to low priority once the configured throughput is exceeded.
    - Arteris verbatim: *"prevent a socket from accepting new requests once a programmable throughput threshold is exceeded."*
  - **Urgency-Regulator (Feedback Floor):** raise urgency dynamically against observed response bandwidth.
    - Arteris verbatim: *"demote socket transactions when bandwidth usage exceeds configured levels ... smooths traffic without halting initiators entirely."*
- **VC mapping**
  - QoS tiers map automatically onto virtual channels through a design-time-fixed policy.
  - Independent VC pools eliminate priority inversion in hardware.
  - Partition granularity: each physical channel supports 1, 2, 4, or 8 virtual channels. QoS tiers map evenly onto VC IDs through a bit-extract from the qos field.
- **Wormhole is never preempted**
  - QoS arbitration is per packet, at the HEAD flit.
  - A locked W-burst holds its path until its final beat. No higher-priority preemption mid-burst.

> **Design choice:** QoS lives at the NMU ingress, not at the router. AMD Versal NoC takes the opposite choice. Aligned with Arteris' end-to-end QoS philosophy: coordinated control of latency, bandwidth, arbitration, and flow management, preventing interference among real-time, best-effort, and background classes.

---

## Slide 7. Data Integrity

**Takeaway:** Routing-field per-hop parity plus payload end-to-end SECDED. A two-layer scheme hitting the sweet spot of area, timing, and coverage.

> *"The Versal adaptive SoC programmable NoC supports **end-to-end data protection** for AXI memory mapped transactions."* (AMD pg313 §Data Integrity)
>
> *"**No ECC checking is performed in the switch fabric.**"* (AMD pg313 §Data Integrity)
>
> *"**Uncorrectable ECC errors result in a fatal interrupt.**"* (AMD pg313 §Data Integrity)

- **Layer 1: Per-hop routing parity**
  - 1-bit parity over the routing-critical fields (destination ID and last-flit indicator).
  - Generated by the NMU / NSU at injection. Checked at every router output and at the destination NI.
  - A mismatch drops the flit immediately, preventing misroute to the wrong node.
  - The event is counted and raises a fatal interrupt.
- **Layer 2: End-to-end whole-flit SECDED**
  - Hsiao SECDED covers the entire flit (header plus payload).
  - Checked only at the destination NI. Routers neither decode nor re-encode.
  - Saves roughly 10× the gate count and one cycle per hop compared with per-hop SECDED.
  - Single-bit errors are silently corrected and counted.
  - Double-bit errors are forwarded unchanged and reported via interrupt.
- **Layer 3: AXI host-side parity (boundary, optional sideband)**
  - 1 bit per byte of data and 1 bit per byte of address (per AMD pg313 §Parity standard).
  - Checked at the AXI boundary inside the NMU / NSU.
  - Regenerated inside the NI when the NMU rewrites an address field.
- **(B)-philosophy error reporting**
  - Fabric ECC and parity errors never synthesize an AXI SLVERR.
  - All events surface through one fatal interrupt on the NI interrupt output.
  - AXI rresp and bresp are reserved for end-to-end memory errors such as HBM / DDR endpoint ECC propagation.

**Why two layers and not one whole-flit SECDED per hop?**

| Choice | Per-hop SECDED | Two-layer (this design) |
|---|---|---|
| Router logic | Decode + encode the full flit at every hop | One 1-bit XOR over routing fields per hop |
| Per-hop latency | +1 cycle per hop | ~0 cycle (combinational) |
| Gate count | ~10× | baseline |
| Routing-fault catch | ✓ | ✓ (routing parity suffices) |
| Payload integrity | per-hop check | end-to-end check at sink |

---

## Slide 8. End-to-End Protection Flow

**Takeaway:** Check → Generate handshakes at every protocol boundary. AXI, NoC, and Slave fault domains stay isolated.

**Mapping (AMD pg313 §Data Integrity *End-to-End Protection* figure, 4 rows) → this NI's 3 layers:**

| AMD pg313 layer | This NI mechanism | Generate / Check points |
|---|---|---|
| Data Parity + Address Parity | AXI host-side parity | AXI master and slave boundaries |
| ECC (NoC packet domain) | Whole-flit SECDED | Generated at NMU / NSU injection. Checked only at the destination NI. |
| DST ID Parity | Per-hop routing parity | Generated at NMU / NSU. Checked at every router output and at the destination NI. |

- 🟢 Generate vs 🔺 Check handshake model.
- **Solid line, request path (Master → Slave)**
  - 🔺 NMU checks AXI data and address parity at ingress.
  - 🟢 NMU generates routing parity and whole-flit SECDED.
  - 🔺 Every router output checks routing parity. Mismatched flits are dropped on the spot.
  - 🔺 NSU checks SECDED and routing parity on arrival.
  - 🟢 NSU regenerates AXI data and address parity for the local slave.
- **Dashed line, response path (Slave → Master)**
  - The same flow runs in reverse.
  - 🟢 NMU regenerates read-data parity for the master after the SECDED check.
  - Aligned with AMD pg313: *"Data parity for read responses is generated as 1 bit per byte after the ECC check stage."*

---

## Slide 9. Read Re-Order Buffer (RoB)

**Takeaway:** The Reorder Table (control plane) and the Reorder Buffer (data plane) work together to bridge NoC out-of-order delivery with AXI's same-ID ordering rule.

> **AXI ordering requirement:** Transactions with the same ID must be ordered in AXI.

- **Reorder Table (control plane)**
  - Keeps track of outstanding transactions of each AXI ID.
  - Keeps state of the order of responses.
- **Reorder Buffer (data plane)**
  - Dynamically allocated.
  - Temporarily stores AXI responses that are out of order.
- **Optimized for deterministic routing**
  - First / single response is always in order.
  - Order is guaranteed for the same destination.
  - Same-destination same-ID follow-up requests take a fast path. Release latency drops from a roughly 3-cycle linked-list walk to about 1 cycle.

**Three RoB modes (choose by area budget):**

| Mode | Structure | Area | Use case |
|---|---|---|---|
| **NormalRoB** | Per-AXI-ID linked-list with adaptive bypass | Largest | Multi-destination, high cross-ID concurrency. Same-dst same-ID takes the fast path. |
| **SimpleRoB** | Single shared release pointer (single FIFO) | Medium | Cross-ID head-of-line blocking is acceptable. |
| **NoRoB** | No allocation | None | Relies on NoC same-source, same-destination, same-VC in-order delivery. Forces single-VC configuration. |

- **B and R RoBs configured independently**
  - Typical deployment: R uses NormalRoB (multi-beat data). B uses SimpleRoB (metadata only).
- **Allocator policy**
  - Lowest-index-first among FREE entries.
  - AW / AR ties in the same cycle are resolved by fair round-robin.

> **AMD pg313 verbatim:** *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."*
> **FlooNoC reference:** `floo_rob.sv` `prev_dest` adaptive-bypass semantics.

---

## Slide 10. NSU Overview

**Takeaway:** Reverses NoC flits back into AXI semantics. Provides the MetaBuffer, the Read Response Buffer, and the Exclusive Monitor at the egress boundary.

- **Protocol conversion and re-sizing**
  - NoC packetized data ↔ AXI4 reverse decoding.
  - Multiple AXI data widths handled by NSU Downsize (Slide 12).
- **MetaBuffer (FlooNoC `floo_meta_buffer.sv` style)**
  - Snapshots the original request metadata when the request flit arrives.
  - Looked up when the response is generated. The NoC never carries round-trip metadata.
- **Egress Read Response Buffer**
  - Buffers the slave's R responses.
  - Decouples slave-side timing from NoC injection back-pressure.
  - Keeps high-speed NoC injection bubble-free even when the slave runs slower.
- **AXI Exclusive Access Monitor**
  - Handles AXI4 Exclusive at the boundary. Reservation tracking for hardware atomics (Slide 11).
- **Clock crossing**
  - Asynchronous bridge from the NoC domain to the AXI domain.
  - Smooths the frequency gap between a fast NoC and a slow slave.

> **Block diagram** (per `docs/images/NSU.png`, AMD pg313 §NoC Slave Unit style):
> Request path: NoC → ECC Check → De-packetizing → W Reassembly → Downsize → AXI Master I/F.
> Response path: AXI Master I/F → Read Response Buffer → MetaBuffer Lookup → Packetizing B/R → ECC Gen → NoC.

> **AMD pg313 verbatim:** *"Read responses are buffered before forwarding to minimize bubbles."* / *"Conversion of NoC packetized data (NPD) to and from AXI protocol data."*

---

## Slide 11. Exclusive Monitor

**Takeaway:** A local reservation table at the NSU replaces global bus locks. Network parallelism improves and transaction latency drops.

- **Lock-free semantics**
  - No hardware lock channel in the network.
  - State tracking stays local to the NSU.
- **Reservation table**
  - On Exclusive read arrival, records the AXI ID, address, size, and burst length.
  - Supports multiple concurrent reservations.
- **Match resolution**
  - On match, the corresponding Exclusive write returns Exclusive OK.
  - When a normal write modifies the same cache line, the reservation is invalidated. The Exclusive write downgrades to a normal write and software retries.
  - When the table is full, a new Exclusive read returns an error response. Software falls back to non-exclusive.
- **Software clear knob**
  - A single CSR write clears all reservations.
  - Typical use: an OS releases mid-Exclusive holds when a process is killed.
- **Single-NI scope**
  - The mechanism covers one boundary.
  - Global multi-NI coherency (directory or snoop protocol) is future work.

---

## Slide 12. Data Width Conversion

**Takeaway:** NMU Upsize and NSU Downsize decouple the internal NoC payload width from the external AXI width. Saves routing resources while preserving AXI4 byte-level semantics.

- **Configuration examples**
  - 64-bit AXI master into a 256-bit NoC. NMU Upsize packs 4 AXI W beats into one wide flit.
  - 512-bit AXI slave behind a 256-bit NoC. NSU Downsize splits one wide flit into 2 AXI W beats.
- **NMU Upsize (narrow AXI)**
  - Write path: narrow beats map to wide-flit lanes by address offset, then accumulate before injection.
  - Read path: wide flits split back into narrow beats, carrying the original AXI ID and last-beat marker.
- **NSU Downsize (wide AXI)**
  - Write path: a wide flit splits into multiple narrow beats written to the slave in order.
  - Read path: narrow beats accumulate into wide flits before injection.
- **Byte strobe flows end-to-end and preserves consistency**
  - NMU regenerates the wide-flit byte strobe to reflect the bytes the master actually drove.
  - NSU filters writes by byte strobe. Over-fetched bytes are harmlessly ignored by the slave.
- **Synthesis optimization**
  - When widths match, the conversion block degenerates to a zero-delay pass-through.
- **No-chop policy**
  - The NI never chops an AXI burst.
  - The longest burst rides through under a single wormhole-lock.

---

## Slide 13. Credit-Based Flow Control

**Takeaway:** Credits decouple the injection condition from long-wire latency. No valid/ready round-trip pressure, and deadlock risk is eliminated.

> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. ... The destination unit can send up to one credit per cycle, per virtual channel."* (AMD pg313 §Credit-Based Flow Control)

- **Latency hiding**
  - Senders inject based on credit balance rather than on a round-trip valid/ready handshake.
  - Long links sustain full throughput.
- **Bi-directional init handshake**
  - After reset, a bi-directional ready handshake establishes credit exchange.
  - Both sides must signal ready before any credit moves.
  - The initial credit pool is seeded from the receiver's buffer depth.
- **Per-VC accounting**
  - Each virtual channel has an independent credit pool.
  - The receiver returns up to one credit per cycle per VC after consuming a flit.
- **Starvation handling**
  - A permanent zero-credit condition counts as a permanent stall on that VC.
  - v0.4.0 does not auto-synthesize an SLVERR.
  - Software observes via the outstanding-count CSR and the interrupt. Recovery is manual when needed.

---

## Slide 14. Closing

**Takeaway:** v0.4.0 spec and DV foundation are locked. 136 protocol rules, 51 testpoints, 136 ABV properties. Next focus is ATOPs and Debug / Safety.

- **Dual implementation (BFM + RTL)**
  - C++ BFM and synthesizable RTL.
  - Behaviorally equivalent at the AXI4 and NoC pin boundaries.
  - The BFM exposes test-only knobs (ECC error injection, response delay, ACTIVE / PASSIVE mode).
  - The RTL has fixed pipeline timing.
  - Both share the same CSR memory map.
- **Spec deliverables (post-A5 baseline)**
  - **Protocol rules:** 136 total, 126 FAIL plus 10 RECOMMEND.
  - **DV testpoints (UVM 1.2):** 51, covering AXI, NoC, CDC, RoB, ECC, QoS, Probes, IRQ, Quiesce, and Exclusive.
  - **ABV assertions:** 136 SVA properties, one-to-one with protocol rules.
  - **FPV scope:** RoB allocator state machine, SECDED gen + check round-trip, routing-parity drop, interrupt function, CDC async FIFO, reset entry sequencing.
- **Next steps**
  - Complete ATOPs (AXI4 Atomic Operations). Currently sample-only. Estimated ~3 weeks of design + DV.
  - Factor out a Protocol Reference Library for shared DV modules.
  - Layer on Debug / Safety: outstanding-tx timeout watchdog and cross-NI coherency (directory or snoop).

> **A5 wave baseline locked 2026-05-08.** Outstanding-tx Timeout removed from v0.4.0 (moved post-v1). Error status and IRQ enable compressed to three event classes.

---

## Appendix. Reference Sources

- **AMD pg313 (Versal Adaptive SoC Programmable NoC Product Guide)**
  - §NoC Architecture, §NoC Master Unit, §NoC Slave Unit, §NoC Packet Switch, §Data Integrity, §Parity, §Credit-Based Flow Control.
- **Arteris FlexNoC Interconnect IP**
  - End-to-End QoS whitepaper. FlexNoC 5 datasheet.
- **FlooNoC (PULP Platform / ETH Zürich)**
  - GitHub `pulp-platform/FlooNoC`: `floo_axi_chimney.sv`, `floo_rob.sv`, `floo_meta_buffer.sv`, `floo_wormhole_arbiter.sv`.
  - arXiv 2305.08562, arXiv 2409.17606, IEEE TVLSI 2025.
