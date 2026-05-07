# Theory of Operation

## Block diagram

```mermaid
flowchart TB
    subgraph TB[Testbench]
        TBAPI[Transaction API + Channel API + CSR access]
    end
    subgraph BFM[ni BFM]
        SEQ[Sequencer<br>API dispatch + RoB + outstanding-txn tracker]
        DRV_AXI[AXI Driver<br>per-channel state machines<br>aclk domain]
        DRV_NOC[NoC Driver<br>per-link state machines<br>noc_clk domain]
        MON_AXI[AXI Monitor<br>aclk domain]
        MON_NOC[NoC Monitor<br>noc_clk domain]
        CDC[Async FIFO<br>aclk ↔ noc_clk]
        CFG[Configuration store<br>QoS / Probes / Errors / BFM knobs]
        QOSGEN[QoS Generator<br>4 modes — Bypass/Fixed/Limiter/Regulator]
        PROBE[Performance Probes<br>Packet / Transaction]
        ECC[FlitECC<br>whole-flit SECDED + route_par]
        ROB[RoB<br>per-AXI-ID order]
    end
    subgraph DUT[Surrounding fabric]
        AXIMST[AXI master DUT]
        AXISLV[AXI slave DUT]
        ROUTER[Router fabric]
    end
    TBAPI --> SEQ
    SEQ --> DRV_AXI
    SEQ --> DRV_NOC
    SEQ --> CFG
    SEQ --> ROB
    CFG --> QOSGEN
    CFG --> PROBE
    DRV_AXI <--> CDC
    DRV_NOC <--> CDC
    QOSGEN --> DRV_NOC
    ECC --> DRV_NOC
    DRV_AXI -->|drives| AXIMST
    AXIMST -->|drives| MON_AXI
    DRV_AXI -->|drives| AXISLV
    AXISLV -->|drives| MON_AXI
    DRV_NOC -->|drives| ROUTER
    ROUTER -->|drives| MON_NOC
    MON_AXI --> SEQ
    MON_NOC --> SEQ
    PROBE --> CFG
    CFG -->|irq_o, level-sensitive,<br>OR over ERR_STATUS & IRQ_ENABLE| TB
```

The `CFG → TB` edge labelled `irq_o` represents the level-sensitive interrupt output asserted when any unmasked `ERR_STATUS` bit is set. Software ISR in the testbench reads `ERR_STATUS` to disambiguate the event class and `LAST_ERR_INFO` for the offending-transaction context. Per `protocol_rules.md` `NI_IRQ_LEVEL`.

## BFM internal architecture

This section is **always required** in protocol-bfm mode.

### Driver

The BFM has **two driver instances** running in different clock domains:

- **AXI Driver** (in `aclk_i` domain): owns per-channel state machines for AW/W/B/AR/R on both manager port (`axi_*_i`) and subordinate port (`axi_*_o`). Plus AXI4-Lite state machine for the CSR port. Fully registered outputs.
- **NoC Driver** (in `noc_clk_i` domain): owns per-link state machines for `noc_req_o` (valid + flit + per-VC credit return + credit-init handshake) and `noc_rsp_o` (mirror). Fully registered outputs.

Both drivers are disabled when `bfm_mode == PASSIVE`; outputs follow `pin_level_reset.md` during-reset values.

### Monitor

Two monitor instances, one per clock domain. Same activity in active and passive modes.

- **AXI Monitor**: samples all 5 channels of both AXI ports + CSR port. Reconstructs full AXI transactions. Reports violations per `protocol_rules.md` `AXI4_*` rules.
- **NoC Monitor**: samples both NoC links in both directions. Reconstructs full flit packets (header + payload). Validates ECC fields. Reports violations per `NOC_*` rules.

### MetaBuffer (NSU sub-block)

NSU-side store that snapshots the original request's header on AW/AR flit reception, retrievable when the corresponding response is generated. Naming convention follows FlooNoC (`floo_meta_buffer.sv`); previously called `ReqInfoStore` in earlier drafts.

Holds at minimum:

| Field | Source | Used by |
|-------|--------|---------|
| `rob_idx` | request flit header | Response flit header (per `NOC_FLIT_RSP_ROB_IDX_INHERIT`) |
| `src_id` | request flit header | Response flit header (routes back to NMU's node) |
| `qos` | request flit header | Response flit header (per `NOC_FLIT_RSP_QOS_INHERIT`) |
| `axi_id` | request flit payload | Response flit payload (B/R `bid`/`rid`) |

Capacity: equal to NSU outstanding-transaction limit (`MAX_TXNS`-bounded). Implements one entry per outstanding NSU request; FREE entries reused after corresponding response injected.

### NSU Read response buffer (NSU sub-block)

Per-AXI-ID elastic buffer at the NSU that absorbs R response data flits arriving from the local AXI subordinate before they are packed into NoC R flits and injected. Distinct from MetaBuffer (which holds request metadata only).

Purpose:

- **Decouples** local AXI slave's R-response timing from NoC injection back-pressure. A slow downstream NoC link must not stall the local AXI slave's R channel.
- **Repacks** narrow AXI R beats into wider NoC R flits when DATA_WIDTH < FLIT_PAYLOAD_WIDTH (full/narrow transfer; see §NSU Downsize / Full-narrow transfer).
- **Reorders within a single AXI ID** is NOT performed here — AXI4 mandates in-order R per ID, and the buffer preserves issue order. Cross-ID reordering happens implicitly via flit injection arbitration.

Capacity: `NSU_R_BUFFER_DEPTH` parameter (default 16 entries; each entry is one NoC R flit's worth of data).

When full, NSU back-pressures the local AXI slave's R channel by holding `axi_rready_o = 0`, propagating back-pressure naturally to the slave.

Reset behavior: cleared on `arst_ni` (AXI domain).

### NSU Exclusive Monitor (NSU sub-block)

NSU-side state for AXI4 Exclusive Access (LDREX/STREX-style atomic primitives via AxLOCK=Exclusive). Tracks pending Exclusive read reservations and validates Exclusive write attempts.

Behavior summary (full normative behavior in `protocol_rules.md` `AXI4_EXCLUSIVE_*` rules):

- **Exclusive AR (AxLOCK=01)**: NSU records `(axi_id, awaddr, awsize, awlen)` into an exclusive-monitor entry. AXI4 restricts Exclusive bursts to single-beat (`awlen=0`); Exclusive cache-line-aligned, naturally-aligned sizes only.
- **Exclusive AW + W**: NSU checks each Exclusive AW arrival's `(axi_id, awaddr, awsize, awlen)` against pending entries. Match → write proceeds, `bresp=EXOKAY`. Mismatch (different ID, different addr, or a normal write to the same line in between) → write becomes a *normal* write (still committed to memory) but `bresp=OKAY` (not EXOKAY).
- **Exclusive monitor invalidation**: any normal write to an address overlapping a pending Exclusive read invalidates that exclusive entry.

Capacity: `EXCLUSIVE_MONITOR_DEPTH` parameter (default 8 entries). When full, NSU rejects new Exclusive AR with `rresp=SLVERR` (cannot guarantee exclusivity tracking) — software is expected to retry or fall back to non-exclusive.

Coherency scope: this is a *single-NI* exclusive monitor. Multi-master coherency across multiple NIs is OUT OF SCOPE for v0.4.0 (would require directory or snoop protocol).

Reset behavior: all entries cleared on `arst_ni`.

Software-visible monitor state and clear knob: the `EXCLUSIVE_MONITOR_STATUS` CSR (per `registers.md`) reports the live `occupancy` field — the number of currently-pending Exclusive read reservations (range 0..`EXCLUSIVE_MONITOR_DEPTH`). Software invalidates all pending entries by writing `1` to `EXCLUSIVE_MONITOR_CTRL.clear_all` (W1 self-clearing trigger); the typical use case is OS bookkeeping when a process is killed mid-Exclusive. Race semantics for clear vs concurrent NSU events are formalised in `protocol_rules.md` `NI_CFG_EXCLUSIVE_CLEAR_RACE`; live-occupancy accuracy contract in `NI_CFG_EXCLUSIVE_OCCUPANCY_ACCURACY`.

### Sequencer

Single sequencer instance (logically domain-spanning). Translates Transaction API calls into AXI Driver + NoC Driver activity, coordinated through:

- **Outstanding-transaction tracker**: per-AXI-ID; bounded by `MAX_TXNS` × `MAX_TXNS_PER_ID`.
- **RoB** (Reorder Buffer): see §RoB sub-block below.
- **CSR file** (in aclk domain): software-visible registers per `registers.md`. CSR writes to QoS / Probe / Error fields update the configuration store; reads return current state.
- **CDC orchestration**: cross-domain transactions (AXI → NoC → AXI) are tracked via correlated tracker entries spanning both domains; sequencer manages the lifecycle across the async FIFOs.

### Configuration store

Per-domain config state; both software-writable (via CSR) and testbench-API-writable (via `set_*` knobs):

"Reset (wire)" column = behaviour on `arst_ni` assertion. "Reset (state API)" column = behaviour on `reset_state()` BFM API call (does NOT toggle wire reset). Where the two columns disagree, that's intentional — wire reset is hardware-driven; `reset_state()` is a BFM-only convenience for inter-test isolation.

| Field | Domain | Write source | Reset (wire) | Reset (state API) |
|-------|--------|--------------|--------------|-------------------|
| `QOS_MODE` (Bypass / Fixed / Limiter / Regulator) | aclk | CSR | reset to default (Bypass) | preserved |
| `BANDWIDTH_LIMIT`, `SATURATION_THRESHOLD`, `LOW_PRIORITY` | aclk | CSR | reset to default | preserved |
| `BANDWIDTH_BUDGET`, `BASE_QOS`, `URGENCY_STEP`, `SOCKET_QOS_EN`, `SOCKET_QOS` | aclk | CSR | reset to default | preserved |
| `PKT_PROBE_EN`, `PKT_PROBE_MODE`, `PKT_WINDOW_SIZE` | aclk | CSR | reset to default (0) | preserved |
| `TXN_PROBE_EN`, `TXN_THRESHOLD_*` | aclk | CSR | reset to default (0) | preserved |
| `ERR_STATUS[3:0]` (RW1C), `ERR_COUNT`, `ECC_UNCORR_ERR_CNT`, `ECC_CORR_ERR_CNT`, `ROUTE_PAR_ERR_CNT`, `AXI_PARITY_ERR_CNT`, `LAST_ERR_INFO` | aclk | hardware writes; CSR write-1-to-clear by software (counters auto-clear with their paired `ERR_STATUS` bit; `ECC_CORR_ERR_CNT` has no clear path — saturating cumulative) | reset to 0 | preserved |
| `IRQ_ENABLE[3:0]` | aclk | CSR | reset to 0 (all masked) | preserved |
| `QUIESCE_CTRL.quiesce_req` | aclk | CSR | reset to 0 (resume) | preserved |
| `EXCLUSIVE_MONITOR_CTRL.clear_all` (one-shot W1 trigger) | aclk | CSR | self-clear; effectively 0 | reset to 0 |
| `bfm_mode` (ACTIVE/PASSIVE) | testbench-only | `set_bfm_mode` | preserved | preserved |
| `set_response_delay_axi`, `set_response_delay_noc` | testbench-only | knob | preserved | reset to (0, 0) |
| ECC error injection one-shot, response fault one-shot | testbench-only | knob | reset | reset |

### Implementation-specific algorithms

#### AddrTrans (Address Translation)

Combinational lookup at NMU that converts an incoming AXI address into NoC `dst_id + local_addr`. Behaviour depends on `ROUTE_ALGO` parameter:

**XYRouting + `USE_ID_TABLE=0`** (default): bit-extraction from AXI address.

```
dst_x       = awaddr[XY_ADDR_OFFSET_X + X_WIDTH - 1 : XY_ADDR_OFFSET_X]
dst_y       = awaddr[XY_ADDR_OFFSET_Y + Y_WIDTH - 1 : XY_ADDR_OFFSET_Y]
dst_id      = {dst_y, dst_x}
local_addr  = awaddr[XY_ADDR_OFFSET_X - 1 : 0]   // bits below the X offset
```

For default parameters (`X_WIDTH=4`, `Y_WIDTH=4`, `XY_ADDR_OFFSET_X=32`, `XY_ADDR_OFFSET_Y=36`): `dst_x = awaddr[35:32]`, `dst_y = awaddr[39:36]`, `local_addr = awaddr[31:0]`.

If extracted `(dst_x, dst_y)` falls outside `[0, MESH_COLS) × [0, MESH_ROWS)`, NMU asserts a protocol violation per `protocol_rules.md` `NOC_FLIT_HDR_DST_ID_VALID`. The flit is not injected; the originating RoB entry remains pending and ultimately surfaces an AXI SLVERR via the outstanding-transaction timeout path (per `AXI4_MST_TIMEOUT_SLVERR`). This avoids a separate immediate-SLVERR mechanism — the timeout path is the single AXI-rresp-generating contract for all fabric-side fault categories.

**SourceRouting** + **IDRouting** (alternatives selectable via `ROUTE_ALGO`): use a SAM (System Address Map) rule table. The table is the compile-time parameter `Sam` (per `signal_interface.md` §Parameters), aligning with FlooNoC `floo_axi_chimney.sv` `Sam` parameter convention. All NIs in the system share the same `Sam` content.

```
for each rule in Sam (NUM_SAM_RULES rules total):
  if (awaddr & rule.mask) == rule.match:
    dst_id = rule.dst_id
    local_addr = awaddr & ~rule.mask  // bits outside the mask are local
    break
no rule matches → NMU returns DECERR
```

The `sam_rule_t` type contains `match`, `mask`, and `dst_id` fields per rule. Rule order matters: first-match wins. The `Sam` parameter is **fixed at instantiation**; runtime modification is out of scope for v0.4.0 (no `SAM_RULE_*` CSR exists in `registers.md`). To change the SAM table, re-elaborate the design with the new `Sam` value.

#### QoS Generator

Per source-doc 06_qos.md §2 (4 modes):

- **Bypass**: `flit.hdr.qos = AXI awqos / arqos`
- **Fixed**: `flit.hdr.qos = QOS_FIXED_VALUE`
- **Limiter**: bandwidth_counter increments per request bytes, decrements per cycle by `BANDWIDTH_LIMIT`; when counter > `SATURATION_THRESHOLD`, qos becomes `LOW_PRIORITY`. Saturating arithmetic.
- **Regulator**: feedback loop on observed response bandwidth; bandwidth_counter accumulates response_bytes − `BANDWIDTH_BUDGET` per cycle; urgency_level adjusts per `URGENCY_STEP` per `BASE_QOS` register field; final qos = `clamp(BASE_QOS + urgency_level, SOCKET_QOS, 15)`.

QoS is computed at AW/AR flit injection. The W flit qos inherits from the corresponding AW. Response flit qos inherits from the request (NSU's MetaBuffer preserves it across the NSU latency).

#### RoB allocator

Per source-doc 04_network_interface.md §FR-05. State machine: `FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE`. Per-AXI-ID release order enforced by linked-list of rob_idx within each ID's outstanding queue.

**RoB allocator policy when multiple FREE entries are available**: lowest-index-first allocation. Each NMU has a static priority encoder over its `MAX_TXNS`-entry RoB array; the lowest-numbered FREE entry is assigned to the next incoming AW or AR. Rationale: deterministic, matches typical ARM-style RoB implementations, simplifies coverage analysis. *Reviewer assumption: please confirm or override.*

**Tie-breaking when two RoB entries become READY_TO_RELEASE in the same cycle on the same `axi_id`**: release in `rob_idx` order (lower rob_idx releases first, reflecting the issue order from the per-AXI-ID linked list). The per-AXI-ID linked list is the canonical ordering source — when two entries on the same axi_id chain are simultaneously eligible, the one allocated first (lower rob_idx) wins. *Reviewer assumption: this matches standard AXI4 per-ID ordering semantics; please confirm.*

**RoB behavior when `rob_req = 0` in the flit header (i.e., master indicates it doesn't need RoB)**: NMU still allocates a tracker entry (to back-pressure on RoB-full), but releases responses immediately on receive without waiting for in-order release. Equivalent to "fast-path" / NoRoB-effective semantics for that transaction. *Reviewer assumption: confirm vs alternative (skip allocation entirely; degenerate stall).*

**RoB variants** (FlooNoC-aligned naming; chosen *per response channel* via two independent build-time parameters `B_ROB_TYPE` and `R_ROB_TYPE`, each in `{NoRoB, SimpleRoB, NormalRoB}`):

- **NoRoB** (default for both B and R): never allocate. Used when the local master is single-issue or guaranteed to receive responses in-order from the NoC fabric. Smallest area footprint; relies on the network to preserve order.
- **SimpleRoB**: allocate one entry per outstanding request, release strictly in issue order. Naive but small. Single shared release-pointer; no per-AXI-ID tracking.
- **NormalRoB**: per-AXI-ID linked-list ordering with `prev_dest` adaptive bypass (see below). Largest but most performant.

The B and R RoBs are independent because B is metadata-only (`bid` + `bresp` + `buser`) — far smaller per entry than R (which carries `MAX_BURST_LEN × DATA_WIDTH` payload). Typical configuration: `R_ROB_TYPE = NormalRoB` (large but needed for read-burst reordering across destinations), `B_ROB_TYPE = SimpleRoB` (single-beat metadata; ID-tracker complexity rarely justified). The `ONLY_METADATA_B` parameter further enables data-SRAM elision for B-RoB.

**`prev_dest` adaptive bypass** (NormalRoB only): when a new request arrives with the same `axi_id` as the most recent prior outstanding request to the **same destination NSU** (`dst_id` equal), and the prior request has not yet returned, NormalRoB enters a fast-path where:

- The new request's RoB entry chains directly to the prior entry's tail.
- On response arrival, both entries are released without re-checking the per-ID linked list — the FIFO ordering is guaranteed by same-source-same-dest in-order delivery on the NoC (per `NOC_FLIT_INORDER_PER_VC` rule) and by SLV-side ordering.

When `prev_dest` differs (cross-destination same-`axi_id`), the standard linked-list allocation applies — entries from the new destination cannot bypass; they wait until prior-destination entries release. This avoids R-channel re-ordering across destinations on the same `axi_id`, which AXI4 prohibits.

Rationale for adaptive bypass: same-destination same-ID is the common case (CPU re-fetches from same memory region); cross-destination same-ID is rare (only if the master uses a pathological ID assignment). Adaptive bypass cuts the common-case release-decision path from ~3 cycles (linked-list walk) to ~1 cycle.

#### Outstanding-transaction timeout

Each NMU RoB entry carries a per-entry timeout counter, incremented on every `aclk_i` cycle the entry remains in `ALLOCATED` state without its response arriving. When the counter reaches `TXN_TIMEOUT` cycles (default 10 000 `aclk_i` cycles; integrator-tunable via the same-named parameter), the entry's response path is forcefully resolved:

- For a write transaction: NMU drives `bresp = SLVERR` to the AXI master and increments `ERR_COUNT`.
- For a read transaction: NMU drives `rresp = SLVERR` on the affected beat (further beats in the same burst, if any, are not generated; AXI master observes the burst as terminated early via this single SLVERR beat).
- In both cases: `ERR_STATUS[1] timeout_err` is set; `LAST_ERR_INFO` captures `(err_axi_id, err_src_id, err_dst_id)` if no prior un-cleared error is sticky; `irq_o` asserts if `IRQ_ENABLE[1]` is set; the RoB entry is released (returned to `FREE`).

This timeout is the **sole AXI-rresp-generating mechanism** on the NoC error path. It covers three operational scenarios that are otherwise indistinguishable to the NMU at the wire level:

- Slave never responds (downstream NSU stuck or attached AXI slave unresponsive).
- Flit lost in fabric (fabric-internal hardware fault not caught by ECC; rare but possible).
- Flit dropped by `route_par` mismatch at a router or sink (per `NOC_FLIT_HDR_ROUTE_PAR_CHECK` — the drop-then-timeout chain is how route_par failures eventually manifest as observable AXI errors).

Software disambiguates the cause by reading `ROUTE_PAR_ERR_CNT`, `ECC_UNCORR_ERR_CNT`, and `LAST_ERR_INFO` in the ISR. Formalised in `protocol_rules.md` `AXI4_MST_TIMEOUT_SLVERR`.

`TXN_TIMEOUT` value selection guidance: 10 000 `aclk_i` cycles at 1 GHz = 10 µs, which is a comfortable upper bound for typical NoC-traversal + slave-response latency (microseconds) while remaining short enough that a hung path returns an error within a software-noticeable window. Integrators with longer expected latencies (e.g., off-chip DRAM with refresh storms) should raise this; integrators wanting tighter SLA on hung-detection should lower it.

#### RoB area-reduction techniques

R-RoB sizing dominates total RoB area. At maximum-config `R_ROB_TYPE=NormalRoB, MAX_TXNS=32, DATA_WIDTH=256, MAX_BURST_LEN=256` the worst-case R-RoB storage is `32 × 256 × 256 = 2 Mbits`. At default `MAX_BURST_LEN=16` the same NormalRoB drops to `32 × 16 × 256 = 128 Kbits` — the typical-deployment number. B-RoB is much smaller (metadata-only when `ONLY_METADATA_B=true`).

For deployments where R-RoB area is still too large, the following techniques (FlooNoC-derived) trade performance for area:

- **Reduce `MAX_TXNS`**: from 32 to 16 → 50% area reduction. Trade-off: `MAX_TXNS_PER_ID` upper bound also drops, lowering achievable per-ID outstanding throughput.
- **Cap `MAX_BURST_LEN`**: bound the parameter range upper bound at 64 instead of 256 → 4× reduction in worst-case payload accumulator. Trade-off: long bursts (`awlen ≥ 64`) require master-side splitting; the NI does not chop bursts internally (per D8 no-chop policy).
- **Switch `R_ROB_TYPE` from `NormalRoB` to `SimpleRoB`**: drops per-AXI-ID tracker (~10% area). Trade-off: cross-ID HoL blocking — a slow response on one ID blocks responses on all others until released.
- **Switch `R_ROB_TYPE` to `NoRoB`** (the parameter default): eliminate RoB area entirely. Trade-off: requires same-VC same-source-same-dest in-order delivery guarantees from the NoC and a master that does not need response reordering. NoRoB is appropriate for I/O peripheral-class masters and the default for the NI parameter.
- **SRAM-backed RoB storage** (RTL-only, integrator option): for `MAX_TXNS ≥ 64`, replace flop-array RoB with single-port SRAM macro. ~4× area reduction at high entry counts; adds 1 cycle pipeline read latency. Not modelled in the BFM (BFM uses unbounded behavioural arrays; the RTL counterpart picks the implementation).

#### NMU Upsize / NSU Downsize (data-width conversion)

The internal NoC data width (`FLIT_PAYLOAD_WIDTH`, derived) is a parameter, default 256-bit. The local AXI port's `DATA_WIDTH` (range `{64, 128, 256, 512}`) may differ. The NI bridges this gap inline at the AXI ↔ flit boundary via two complementary blocks.

**NMU Upsize** (AXI narrower than NoC, `DATA_WIDTH < FLIT_PAYLOAD_WIDTH`):

- AW path: `awsize` and `awlen` are passed through unchanged on the flit header. NMU records the AXI master's burst geometry for use by the W path.
- W path: NMU accumulates W beats from the local master into a wide flit-payload buffer until either a full flit's worth is collected, the burst ends (`wlast=1`), or a 4KB boundary is reached. The accumulated payload is injected as one wide W flit on the NoC.
- W beat-to-flit lane mapping: the AXI byte address (lower bits of `awaddr` plus per-beat offset from `awsize`) selects which lane(s) in the wide flit each AXI W beat populates. Unpopulated lanes carry zero in the data field; their `wstrb` bits in the regenerated wide-flit `wstrb` field are 0 (see §Over-fetch and WSTRB regeneration).
- AR path: `arsize`, `arlen`, `araddr` pass through. NMU records geometry for the R path.
- R path: NMU receives wide R flits from the NoC, and **repacks** them back into narrow AXI R beats matching the original master's `arsize`. Only the lanes addressed by the original `araddr` + per-beat offset are forwarded; other lanes are discarded. Each AXI R beat has the original `arid`. The final beat carries `rlast=1`.

Latency cost: AXI-W-beat-to-NoC-W-flit injection waits for a full wide flit to fill (worst case `FLIT_PAYLOAD_WIDTH / DATA_WIDTH` AXI cycles). For a 64-bit AXI master to a 256-bit NoC, that is up to 4 cycles of accumulation per flit. R repack is single-cycle per AXI beat (registered).

**NSU Downsize** (AXI wider than NoC at the slave side, `DATA_WIDTH > FLIT_PAYLOAD_WIDTH`):

Symmetric to NMU Upsize. NSU receives wide W flits from the NoC and **breaks them down** into multiple narrow AXI W beats matching the local slave's `DATA_WIDTH`. R direction: NSU accumulates multiple AXI R beats from the slave into wide R flits before injection.

**No-conversion case** (`DATA_WIDTH == FLIT_PAYLOAD_WIDTH`): both blocks degenerate to pass-through (1 beat ↔ 1 flit, lanes copied verbatim).

Per-port `DATA_WIDTH` is fixed by the NI parameter and does not change at runtime.

#### Full / narrow transfer mechanism

AXI4 supports `awsize` / `arsize` smaller than `DATA_WIDTH` ("narrow transfer", e.g., a 32-bit beat on a 256-bit bus). The NI honours this:

- **Narrow transfer (AxSIZE < log2(DATA_WIDTH/8))**: only the addressed lanes carry data. Unaddressed lanes use `wstrb=0` on writes; on reads, the slave is expected to only return data on the addressed lanes (other lanes' read data is don't-care).
- **Full transfer (AxSIZE == log2(DATA_WIDTH/8))**: all lanes are valid; `wstrb` is all-ones for non-final beats (last beat may be partial if address is unaligned).
- **AxLEN handling**: passed through unchanged. NI does **not** chop bursts into shorter ones (D8: no-chop policy, FlooNoC-aligned). For `awlen=255` (max AXI4 burst), the entire 256-beat burst traverses as one wormhole-locked W-burst.
- **AxBURST handling**: `INCR` (most common) and `WRAP` (cache-line refill) are supported; `FIXED` is supported but with the AXI4 restriction that NI cannot resize FIXED bursts (see §`AXI4_SLV_NSU_AW_BURST_FIXED_REPLAY` in protocol_rules.md). For Exclusive bursts, AXI4 mandates single-beat (`awlen=0`).

#### Over-fetch and WSTRB regeneration

A consequence of upsize at NMU: when narrow AXI W beats are accumulated into a wide W flit, *the lanes not driven by the master are still part of the flit*. We call this **over-fetch** at the NoC layer. The NSU receives the full wide flit but must respect the original master's intent (only commit the addressed lanes to the slave).

NMU **regenerates `wstrb` per wide flit** to match: each NMU-input `wstrb` byte at AXI byte `b` maps to flit-payload byte `b'` (computed from `awaddr` + per-beat offset + `awsize`), and the wide-flit `wstrb` field carries that exact mask. Bytes the master didn't drive carry `wstrb=0` in the wide flit. Bytes outside the addressed lanes for narrow transfer also carry `wstrb=0`.

NSU on the receiving end uses the wide-flit `wstrb` to gate which bytes are written to the local slave's W beats: only `wstrb=1` bytes are committed.

Why this works without needing per-lane data clearing: AXI4 `wstrb` is the canonical "this byte is valid" mask, and the slave's behaviour is defined to ignore data on lanes with `wstrb=0`. So over-fetched data bytes are harmless — they're filtered out at the slave.

Over-fetch read direction is **not** an issue: NSU reads the entire wide flit's worth from the slave (slave returns full lanes), and NMU discards unaddressed lanes when repacking back to the narrow master.

Out of scope (D8 alternative considered + rejected):

- **Bus chopping** (NMU breaks long bursts into shorter ones to interleave around DDR open-page boundaries) — rejected for v0.4.0; would require Chop Trackers per outstanding burst, complex re-merge at NSU, and only benefits DDR-controller-fronting traffic, not our typical NI use case (router → switch → endpoint slave).
- **AxSIZE conversion** (NMU promotes narrow beats to a single wide beat) — rejected; the over-fetch + per-flit `wstrb` regen scheme already gives one wide flit per AXI beat group, no AxSIZE rewrite needed on the wire.

#### VC Mapping

The NoC links carry `NUM_VC` parallel virtual channels (parameter, range 1..8, default 1). NMU and NSU treat each VC as an independent flow with its own credit pool and per-VC injection FIFO inside the NI. The forward data link is shared. Per-flit `vc_id` in the flit header (see `02_flit.md` §1.2) identifies the owning VC.

NMU performs two distinct VC functions, separately scoped:

**1. VC Mapping (traffic → vc_id)**: at flit-construct time, NMU assigns each outbound flit to a VC using the **Hybrid R/W × QoS policy**. The R/W bit selects the subset (request vs response, per §VC partition policy below). Within the subset, the qos field selects which VC. Mapping is a pure function of `(R/W, qos)`. Policy is fixed at design time, no runtime alternative. Per `protocol_rules.md` `NOC_VC_MAPPING_HYBRID_RW_QOS`.

**2. Wormhole arbiter (per-cycle injection ordering)**: when multiple VCs have flits queued, an internal arbiter picks one VC per cycle to drive onto the shared link, respecting the wormhole-lock (per `NOC_FLIT_VC_HARDLOCK`). Local to NMU. Distinct from the cycle-level VC arbitration that runs in the network switch (NPS, per AMD pg313 §Virtual Channel Arbitration — out of NI scope).

**NMU input (per-VC demux)**: the inbound `noc_*_flit_i` carries the source's `vc_id`. NMU demuxes the inbound flit to one of `NUM_VC` per-VC reception FIFOs based on the header field.

**NSU output / input**: symmetric to NMU.

**VC partition policy**: NUM_VC ∈ {1, 2, 4, 8} are pre-validated. Recommended partition (from `protocol_rules.md` `NOC_VC_PARTITION`):

| NUM_VC | Request VCs | Response VCs | Notes |
|--------|-------------|--------------|-------|
| 1 | VC[0] (shared) | VC[0] (shared) | No partition; relies on protocol-rules-level deadlock avoidance |
| 2 | VC[0] | VC[1] | Standard request/response separation; deadlock-free by construction |
| 4 | VC[0..1] | VC[2..3] | Allows QoS-tier within each subset (high/low per direction) |
| 8 | VC[0..3] | VC[4..7] | Full QoS-tier × R/W cross-product |

**Hard-lock rule**: once a VC's wormhole-arbiter wins for a packet, the full packet's flits must be served from that same VC at every NMU/router/NSU. No mid-packet VC switching (per `NOC_FLIT_VC_HARDLOCK` rule).

#### CDC (async FIFO)

NMU AXI ingress → NoC injection: aclk-domain producer, noc_clk-domain consumer. Gray-counter pointer + 2FF synchronizer. Default depth: 16 entries (sized to absorb 2× the maximum expected aclk-cycle round-trip at the slowest clock-ratio combination, plus 2 entries for synchroniser pipeline depth). *Reviewer assumption: 16 is conservative; tune down if area-critical.*

NMU NoC ingress → AXI egress: mirror direction.

NSU has analogous FIFOs in the inverse data flow.

#### Software quiesce flow

Software can request NMU-side quiesce before runtime reconfiguration that requires no in-flight transactions on the NMU path. Two CSRs implement this:

- `QUIESCE_CTRL.quiesce_req` (RW): software sets `1` to enter quiesce; clears to `0` to resume.
- `QUIESCE_STATUS.quiesce_idle` (RO): asserts when `(QUIESCE_CTRL.quiesce_req=1) AND (PENDING_R_COUNT=0) AND (PENDING_W_COUNT=0)`. All three terms are `aclk_i`-domain (no CDC).

While `quiesce_req=1`:

- NMU stops accepting new AW/AR by holding `axi_awready_o = axi_arready_o = 0`.
- In-flight outstanding transactions continue to drain through normal response paths.
- NSU is **NOT** quiesced — NSU continues to service inbound NoC `noc_req_i` requests and drive the local AXI subordinate. This NI's quiesce is NMU-only, scoped to the NMU-reconfig use case. Full-NI drain (e.g., for power-down) would require an additional NSU-side quiesce knob; intentionally out of scope for v0.4.0.

Software polling protocol: write `quiesce_req=1`, poll `quiesce_idle` until set, do reconfig, write `quiesce_req=0` to resume. Polling timeout SHOULD be ≥ `MAX_TXNS × TXN_TIMEOUT` `aclk_i` cycles to accommodate worst-case drain (every outstanding transaction times out one-by-one at `TXN_TIMEOUT` cycles per `AXI4_MST_TIMEOUT_SLVERR`). Default = `32 × 10 000` = 320 000 cycles ≈ 320 µs at 1 GHz.

`PENDING_R_COUNT` / `PENDING_W_COUNT` (RO CSRs per `registers.md`) increment on AXI master-side AW/AR handshake completion at `axi_*_i`, decrement on B / R-with-`rlast` handshake completion at `axi_*_i`. Both counters are `aclk_i`-domain native (no CDC); the AXI-edge increment/decrement contract is the software-observable definition (formalised in `protocol_rules.md` `NI_CFG_PENDING_COUNT_ACCURACY`). Counter width = `ceil(log2(MAX_TXNS+1))` per direction; saturation at `MAX_TXNS` is impossible by construction (NMU back-pressures `awready`/`arready` before exceed).

Reset interaction: `arst_ni` clears `quiesce_req` and the outstanding tracker → `quiesce_idle` returns to 0 because both quiesce_req and the (now-zero) PENDING counts make the AND-condition's `quiesce_req=1` term false. Any in-progress quiesce is therefore abandoned by reset.

Formalised in `protocol_rules.md` `NI_CFG_QUIESCE_FLOW` (steady-state contract) and `NI_CFG_QUIESCE_LIVENESS` (drain upper bound).

#### ECC

Two-layer protection scheme aligned with the v0.4.0 flit format restructure (see `docs/design/02_flit.md` §ECC). Replaces the v0.3.0 per-granule scheme.

**Layer 1 — `route_par` (per-hop routing parity)**:

- 1-bit even parity computed over routing-critical header fields `{dst_id, last}` (9 bits at default). Aligned with AMD pg313 §Parity verbatim: "The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity. DST-ID parity is generated by the NMU/NSU and checked by the NPS."
- Generated at NMU/NSU injection. Checked at every router output port and at every NI sink.
- Purpose: catch single-bit corruption on routing fields *before* a flit is misrouted, and on `last` *before* wormhole arbiter is misled. A failed `route_par` triggers an immediate error report at the router (or sink) where the check fails.
- Computed as `^{dst_id, last}` (XOR-reduction). `route_par` is set so that the total parity over `{dst_id, last, route_par}` is 0 (even).
- Cost: 1 bit per flit, 1 XOR-tree per router output and per NI sink. Far cheaper than rerunning the whole-flit SECDED at every router.
- Why `src_id` is not in coverage: `src_id` is protected by end-to-end `flit_ecc` (whole-flit SECDED) at the destination NI sink. `src_id` corruption only mis-routes the response (not the request); the `flit_ecc` SECDED at NMU R-flit reception will catch any single-bit src_id flip. Per-hop parity stays focused on the fields routers actually use to decide next-hop direction.

**Layer 2 — `flit_ecc` (whole-flit SECDED at endpoint)**:

- SECDED Hamming code computed over the entire flit (header + payload, *excluding* the `flit_ecc` field itself).
- Width parameterised by `FLIT_ECC_WIDTH` (default 10 bits for the 396-bit protected payload at default parameters).
- SECDED bound: `FLIT_ECC_WIDTH` (= `p`) must satisfy `2^(p-1) ≥ FLIT_DATA_WIDTH + p + 1`, where `FLIT_DATA_WIDTH = FLIT_WIDTH - FLIT_ECC_WIDTH` is the protected-bits count. Derivation: Hamming SEC over `k` data bits requires `r` check bits with `2^r ≥ k + r + 1`. SECDED adds one overall-parity bit, so total `p = r + 1`. The canonical bound is therefore `2^(p-1) ≥ k + p`. The spec uses the slightly stricter `2^(p-1) ≥ k + p + 1` form (one bit of margin against future flit-format growth that may push `k` to the boundary). Default config: `FLIT_DATA_WIDTH = 396, p = 10` → `2^9 = 512 ≥ 396 + 10 + 1 = 407` ✓. This formula is shared verbatim with `signal_interface.md` §Parameter constraints and `docs/design/02_flit.md` §3.6.
- Generated at NMU/NSU injection (whole flit). Checked **only at the destination NI sink** — NOT at intermediate routers. Routers neither check nor regenerate `flit_ecc`; they trust it end-to-end.
- Purpose: catch single-bit (correct) and double-bit (detect) errors anywhere in the flit (header or payload) over the entire NoC traversal.

**Single-bit (correctable) errors**:

- The receiving NI silently corrects the bit, increments `ECC_CORR_ERR_CNT` (saturating, no clear path; pure informational counter per `registers.md`), and propagates corrected data downstream (to AXI master via R, or to AXI slave via W).
- No protocol-level signalling — AXI consumer sees correct data with `OKAY` resp. Software polls `ECC_CORR_ERR_CNT` for health monitoring; no IRQ source.

**Double-bit (uncorrectable) errors**:

- The receiving NI **cannot correct, but does NOT synthesise an AXI rresp value from this check** — the corrupted flit is forwarded to the AXI consumer as-is with `bresp=OKAY` / `rresp=OKAY`. This is consistent with the (B)-philosophy decision that fabric-level ECC checks are observation-only at the AXI boundary; AXI rresp is reserved for end-to-end (HBM/DDR-style) and timeout-driven SLVERR. Visibility goes through CSR + IRQ.
- The NI increments `ECC_UNCORR_ERR_CNT` (saturating, cleared via `ERR_STATUS[0]` RW1C), sets `ERR_STATUS[0] ecc_uncorr_err`, captures `LAST_ERR_INFO` if no prior un-cleared error is sticky, and asserts `irq_o` if `IRQ_ENABLE[0]` is set. Formalised in `protocol_rules.md` `NOC_FLIT_HDR_FLIT_ECC_CHECK`.
- The downstream consumer (AXI master for R, AXI slave for W) sees data which is provably corrupted by the time it lands; the application-layer integrity (HBM/DDR ECC at endpoint, software CRC, etc.) is the recovery mechanism. The NoC fabric's job is detect-and-record, not synthesise-AXI-error.

**Why this design rather than fabric-driven SLVERR?** Aligns with AMD pg313 §Data Integrity stance: NoC switches do not check ECC mid-flight, and uncorrectable detection at endpoints raises a fatal interrupt rather than altering the AXI rresp channel. Forwarding the corrupted flit also preserves "end-to-end ECC" semantics in the strict sense — the destination endpoint (HBM/DDR) sees the actual bits the fabric delivered, allowing endpoint-layer ECC to make its own determination. Substituting SLVERR or dropping would prevent endpoint ECC from running on the data path it was designed for.

**Routing-fault errors (`route_par` mismatch)**:

- A router output port or NI sink detecting a `route_par` mismatch MUST drop the flit (per `protocol_rules.md` `NOC_FLIT_HDR_ROUTE_PAR_CHECK`). Forwarding a flit whose routing fields are corrupted would risk misrouting (delivery to the wrong NSU, with secondary side effects on the wrong AXI slave) — drop is the safer choice.
- The drop event increments `ROUTE_PAR_ERR_CNT`, sets `ERR_STATUS[2] route_par_err`, captures `LAST_ERR_INFO` if no prior un-cleared error is sticky, and asserts `irq_o` if `IRQ_ENABLE[2]` is set.
- The originating NMU's outstanding-transaction tracker eventually times out (default 10 000 `aclk_i` cycles) and signals SLVERR back to the AXI master via `protocol_rules.md` `AXI4_MST_TIMEOUT_SLVERR`. This timeout-driven SLVERR is decoupled from the fabric ECC mechanism — the same path also handles slave-never-responds and other flit-loss scenarios. Software disambiguates the cause via `LAST_ERR_INFO` + counters.

**Why two layers, not one whole-flit SECDED applied per-hop?** Per-hop SECDED would require every router to decode + re-encode 406 bits, adding ~1 cycle per hop and ~10× the gate count of `route_par` parity. The two-layer scheme matches AMD pg313 NPS guidance: routing-critical fields get cheap per-hop check, full payload integrity is end-to-end.

**Out of scope** (not v0.4.0):

- Per-granule data ECC (deprecated; whole-flit `flit_ecc` is sufficient at our flit sizes).
- Per-router whole-flit SECDED (redundant with end-to-end `flit_ecc`).
- ECC over reserved fields' future allocations — when a new field claims `rsvd` bits, `flit_ecc` automatically covers the new field with no spec change.

#### AXI parity handling (per AMD pg313 §Parity alignment)

Independent of NoC-fabric `flit_ecc` / `route_par`, the AXI host boundaries carry per-byte parity per AMD pg313 §Parity standard configuration ("1 bit per byte for Data" and "1 bit per byte for AxAddress"). Active when `ENABLE_AXI_PARITY = true` (default). All log-only at AXI boundary — no SLVERR injection (per (B)-philosophy).

**NMU manager-side parity flow (request path)**:

1. AXI master drives `axi_awaddr_par_i[ADDR_WIDTH/8-1:0]`, `axi_araddr_par_i[ADDR_WIDTH/8-1:0]`, `axi_wdata_par_i[DATA_WIDTH/8-1:0]` per AMD §Parity convention.
2. NMU verifies parity at AW/AR/W handshake. Mismatch → log `ERR_STATUS[3]` + `AXI_PARITY_ERR_CNT` + `LAST_ERR_INFO`. Transaction proceeds.
3. NMU forwards address into AddrTrans (which may rewrite upper bits via address-map / SAM lookup). When NMU modifies an address byte, the corresponding parity byte is regenerated (per AMD §Parity: "When an AXI field is modified by NMU/NSU logic, parity is regenerated"). Bytes the NMU does not modify carry source parity through.
4. Once data enters the NoC fabric, `flit_ecc` (whole-flit SECDED) takes over. AXI parity does not propagate inside the NoC.

**NMU manager-side parity flow (response path) — A4.6 addition**:

1. NMU receives R flit on `noc_rsp_i`, runs `flit_ecc` SECDED check (1-bit silent correct, 2-bit forward + log).
2. **After the `flit_ecc` check stage**, NMU regenerates per-byte parity over the corrected `axi_rdata_o` bytes and drives `axi_rdata_par_o[DATA_WIDTH/8-1:0]` back to the AXI master.
3. AXI master verifies `axi_rdata_par_o` per byte at its R handshake.

This regeneration point is the verbatim AMD pg313 §Parity prescription: "Data parity for read responses is generated as 1 bit per byte after the ECC check stage, when the data is converted from NPP to AXI protocol." Formalised in `protocol_rules.md` `AXI4_MST_PARITY_GEN_R`.

**NSU subordinate-side parity flow**:

1. NSU receives request flits, runs `flit_ecc` check, unpacks AXI fields.
2. NSU **generates** per-byte parity for `axi_awaddr_par_o`, `axi_araddr_par_o`, `axi_wdata_par_o` after ECC check stage, before driving the local AXI slave (per AMD §Parity: "Address parity for read/write requests and data parity for write requests is generated by the NSU after the ECC check").
3. Local AXI slave drives `axi_rdata_par_i` back to NSU on R reception.
4. NSU verifies `axi_rdata_par_i`. Mismatch → log path same as NMU. R beat forwarded to NoC with `rresp = OKAY`.

**Why log-only and not SLVERR**: AXI-side parity detects local-wire / local-IP corruption, not fabric corruption. (B)-philosophy reserves the AXI rresp/bresp channel for end-to-end (HBM/DDR endpoint ECC) and timeout-driven SLVERR (via `AXI4_MST_TIMEOUT_SLVERR`). Parity errors surface via CSR + `irq_o` only.

### Reset entry sequencing

1. Either (or both) of `arst_ni` / `noc_rst_ni` asserts asynchronously. All BFM outputs in the affected domain follow `pin_level_reset.md` during-reset values.
2. While the relevant reset is low: trackers in that domain dropped; pending `set_response_delay` countdowns cancelled; one-shot fault flags cleared; observation lists NOT cleared.
3. CDC FIFOs in the asserted domain hold reset values; FIFO read on the un-asserted side sees empty / FIFO write sees not-ready.
4. Reset deasserts → state machines remain IDLE; outputs transition to `pin_level_reset.md` after-reset values.
5. Cross-domain partial reset behavior: see `pin_level_reset.md` §Reset entry sequencing item 4.

### Performance commitments (BFM behavior model)

- **Per-link injection rate**: max 1 flit/cycle per `noc_*_o` link (no parallel multi-flit injection on a single link). Combined NMU `noc_req_o` + NSU `noc_rsp_o` give max 2 flits/cycle per NI when both halves are active simultaneously.
- **NMU injection latency** (AXI AW handshake → noc_req_o flit injection):
  - `CUT_AX=0`: 1 cycle (combinational pack + immediate inject).
  - `CUT_AX=1`: 2 cycles (one extra spill register at AW/AR path for timing closure).
- **NMU response latency** (W phase handshake → noc_req_o W flit): 1 cycle (W path bypasses CUT_AX).
- **NMU response unpack** (`noc_rsp_i` reception → `axi_b`/`axi_r` handshake):
  - `CUT_RSP=0`: 1 cycle.
  - `CUT_RSP=1`: 2 cycles.
- **NSU latency**: mirror of NMU (`noc_req_i` → `axi_*_o` and `axi_*_o` → `noc_rsp_o`).
- **CDC traversal**: aclk → noc_clk crossing adds 3-4 noc_clk cycles depending on `CDC_FIFO_DEPTH` and clock ratio (per CDC §); same on the inverse direction.
- **NMU vs Router-Router latency comparison**: an NI's flit at `noc_*_o` reaches the next router 1 cycle later than a flit forwarded between two routers, because the NI sets the output in the simulation pipeline's NI Process phase (defined in `docs/design/08_simulation.md §6` — outside this BFM spec) whereas router-to-router uses the wire-propagation phase directly. Account for this 1-cycle overhead in cycle-accurate co-simulation.
- **Throughput**: 1 AXI transaction per cycle (best case, no QoS regulation, no RoB back-pressure, no CDC stall, no wormhole-lock contention).
- **Resource model**: BFM tracks up to `MAX_TXNS` outstanding transactions; RoB depths per `B_ROB_SIZE` / `R_ROB_SIZE`; CDC FIFO depth per `CDC_FIFO_DEPTH`; W reassembly buffer depth per `MAX_BURST_LEN`.

### QoS does not preempt wormhole

QoS-aware arbitration (per `06_qos.md §5` extension to FlooNoC's RR baseline) operates on **packet (HEAD-flit) granularity only**. Once a packet's first flit is granted at any arbitration point, the wormhole-lock per `protocol_rules.md` `NOC_MST_WORMHOLE_LOCK` holds the path until the packet's `last=1` flit is consumed. A higher-QoS packet arriving mid-burst CANNOT preempt the locked low-QoS packet — it must wait for the lock to release. This applies at both the NMU output arbiter (W burst vs AR vs new AW) and at every router output port. Test plan TP32 (deadlock-prevention) covers wormhole + QoS interaction.

## RTL internal architecture

`MODE.md` declares `has-rtl-counterpart: yes` — this NI has a paired RTL implementation, behaviorally equivalent at the AXI4 and NoC pin boundaries.

### RTL block structure

The RTL implementation follows the same external functional decomposition as the BFM (NMU + NSU + sub-modules per source-doc §2.2), but with synthesizable hardware modules instead of behavioral state machines:

```mermaid
flowchart TB
    subgraph NMU_RTL[NMU RTL]
        ATX[AddrTrans<br>combinational lookup]
        QGEN[QoSGen<br>per-mode logic]
        UPSZ[Upsize<br>narrow→wide W accum,<br>wide→narrow R repack]
        FPK[FlitPack AW/W/AR<br>combinational + register]
        EGEN[FlitECC Gen<br>whole-flit SECDED + route_par]
        ROB_RTL[RoB Storage<br>flop array, MAX_TXNS entries]
        FUP[FlitUnpack B/R]
        ECHK[FlitECC Check]
        VCARB_O[VC Arbiter<br>NUM_VC →1 link]
        VCDMX_I[VC Demux<br>1 link → NUM_VC]
        IBF[InjectionBuffer<br>per-VC FIFO]
        CDC_F1[Async FIFO<br>aclk → noc_clk]
        CDC_F2[Async FIFO<br>noc_clk → aclk]
    end
    subgraph NSU_RTL[NSU RTL]
        FUP_S[FlitUnpack AW/W/AR]
        MBF[MetaBuffer<br>flop array]
        EXCMON[Exclusive Monitor<br>EXCLUSIVE_MONITOR_DEPTH entries]
        DNSZ[Downsize<br>wide→narrow W split,<br>narrow→wide R accum]
        RRSP[R Response Buffer<br>NSU_R_BUFFER_DEPTH entries]
        ECHK_S[FlitECC Check]
        FPK_S[FlitPack B/R]
        EGEN_S[FlitECC Gen + route_par]
        VCARB_S[VC Arbiter NSU]
        VCDMX_S[VC Demux NSU]
        CDC_F3[Async FIFO<br>noc_clk → aclk]
        CDC_F4[Async FIFO<br>aclk → noc_clk]
    end
```

Sub-modules:
- **AddrTrans (NMU)**: combinational; AXI awaddr / araddr → (dst_id, local_addr) per ROUTE_ALGO and USE_ID_TABLE config.
- **QoSGen (NMU)**: per-mode (Bypass / Fixed / Limiter / Regulator). Stateful for Limiter / Regulator (bandwidth_counter, urgency_level).
- **Upsize (NMU) / Downsize (NSU)**: data-width converter at the AXI ↔ flit boundary; degenerates to pass-through when `DATA_WIDTH == FLIT_PAYLOAD_WIDTH`. See §NMU Upsize / NSU Downsize.
- **FlitPack / FlitUnpack**: combinational logic + 1 pipeline register; `CUT_AX` / `CUT_RSP` parameters add spill register.
- **RoB Storage (NMU)**: flop-based array of `MAX_TXNS` entries, each carrying state, axi_id, rob_idx, response data accumulator. Per-AXI-ID linked-list tracking with `prev_dest` adaptive bypass (NormalRoB variant).
- **MetaBuffer (NSU)**: per-outstanding-NSU-request snapshot of request-flit metadata (rob_idx, src_id, qos, axi_id). FlooNoC `floo_meta_buffer.sv` aligned.
- **R Response Buffer (NSU)**: `NSU_R_BUFFER_DEPTH`-entry elastic buffer that decouples local AXI slave R timing from NoC injection back-pressure.
- **Exclusive Monitor (NSU)**: `EXCLUSIVE_MONITOR_DEPTH`-entry table tracking pending Exclusive read reservations per AXI4 §A7.
- **VC Mapping / Demux**: per-NMU/NSU VC mapping block. NMU assigns `vc_id` to each outbound flit per Hybrid R/W × QoS policy (fixed at design time per `protocol_rules.md` `NOC_VC_MAPPING_HYBRID_RW_QOS`). Cycle-level VC arbitration is a NPS (switch) function, not NI, per AMD pg313 §Virtual Channel Arbitration.
- **Async FIFOs**: gray-counter pointer + 2FF synchronizer; depth synthesis-time parameter.
- **InjectionBuffer (NMU)**: small per-VC FIFO (`NMU_BUFFER_DEPTH` from `NocConfig`, default 2 in BFM). RTL uses the same default (2 entries) per BFM-RTL behavioral equivalence; *Reviewer assumption: confirm if RTL choice differs.*
- **FlitECC Gen / Check**: whole-flit SECDED Hamming over flit (header + payload) plus 1-bit `route_par` parity over `{dst_id, last}` (per AMD pg313 §Parity). Width parameterised by `FLIT_ECC_WIDTH` (default 10 bits). See §ECC.

### RTL pipeline / timing

- AXI handshake → flit injection: 1-2 cycles (`CUT_AX` parameter).
- NoC flit reception → AXI handshake: 1-2 cycles (`CUT_RSP` parameter).
- CDC traversal: 3-4 cycles each direction.
- RoB entry lifecycle: 1 cycle ALLOCATED → traffic round trip → 1 cycle to release.

Fixed timing (no runtime configurability beyond `CUT_AX` / `CUT_RSP` synthesis parameters). The BFM's `set_response_delay_*` knobs are testbench-only and have no RTL equivalent.

### RTL reset behavior

On `arst_ni` assertion:
- All AXI-domain registered outputs reset to `pin_level_reset.md` during-reset values.
- AXI in-flight tracker, RoB allocator state reset.
- AXI-domain configuration registers (CSR file) reset to defaults per registers.md (e.g., `QOS_MODE = 0` Bypass).
- CDC FIFO write-pointer (aclk side) reset; read-pointer on noc_clk side persists until `noc_rst_ni` asserts.

On `noc_rst_ni` assertion: mirror behavior.

Cross-domain partial reset → CDC FIFO is in inconsistent state; integrator must ensure both resets eventually deassert in the same power-on epoch.

### RTL-vs-BFM behavioral equivalence

| BFM feature | RTL counterpart |
|---|---|
| `set_response_delay_axi` / `set_response_delay_noc` | **Test-only.** RTL has fixed pipeline timing (`CUT_AX` / `CUT_RSP` synthesis params only). BFM knob exists for stress-testing master DUT response-latency tolerance. |
| `set_inject_ecc_error(channel, kind)` | **Test-only.** RTL only generates ECC errors when input data is genuinely corrupted (single-event upset, etc.). BFM knob exists for stress-testing downstream ECC-handling paths. |
| `set_response_fault(channel, SLVERR/DECERR)` | **Test-only.** RTL only generates SLVERR/DECERR on real conditions: outstanding-transaction timeout (per `protocol_rules.md` `AXI4_MST_TIMEOUT_SLVERR` — covers slave-never-responds, fabric flit loss, route_par-induced drop), AXI 4KB boundary crossing (`AXI4_SLV_AW_BURST_4KB_BOUNDARY` / `AXI4_SLV_AR_BURST_4KB_BOUNDARY`), unmapped address (`AXI4LITE_SLV_UNMAPPED_DECERR` for CSR access; SAM no-match for data-path), Exclusive monitor overflow (`AXI4_EXCLUSIVE_MONITOR_OVERFLOW`). **flit_ecc uncorrectable does NOT generate SLVERR** — the corrupted flit is forwarded with `bresp/rresp=OKAY` and the error surfaces only via CSR + IRQ (per (B)-philosophy ECC scheme; see §ECC §"Double-bit (uncorrectable) errors"). |
| `bfm_mode = ACTIVE / PASSIVE` | **Test-only.** RTL is always active; PASSIVE is a verification convenience only. |
| `apply_axi_*` / `expect_axi_*` / `expect_noc_*` | **Test-only.** RTL is the DUT (in some scenarios) or the AXI responder (in others); it has no method API. |
| `get_observed_*` lists | **Test-only.** RTL has no observation buffers; observation happens via the BFM (in passive mode) or external scoreboards. |
| CSR-mapped QoS / Probe / Error registers | **Identical between BFM and RTL.** Software accesses the same CSR memory map (per `registers.md`). The BFM models the same CSR file; RTL implements it as actual flop-based registers. |
| ECC generation / validation | **Identical at the wire level.** Same two-layer scheme: whole-flit SECDED Hamming on `flit_ecc` field (parameterised `FLIT_ECC_WIDTH`, default 10 bits) checked end-to-end at the destination NI; 1-bit `route_par` even-parity over `{dst_id, last}` checked per-hop at every router (per AMD pg313 §Parity). |
| RoB ordering | **Identical at the wire level.** Same per-AXI-ID order release; same back-pressure on `awready` / `arready` when full. |

### RTL implementation notes

- Synthesis target: ASIC 7nm process; target frequency 1.2 GHz on `noc_clk_i` and 800 MHz on `aclk_i`. *Reviewer assumption: representative target; adjust for actual deployment.*
- RoB Storage: flop-based at MAX_TXNS=32 (default); for larger MAX_TXNS, integrator should evaluate SRAM macro.
- CDC FIFO depth: parameter `CDC_FIFO_DEPTH`, default 16 entries.
- Lint exemption: `WIDTH_TRUNC` on AXI awaddr / araddr upper bits where the routing extracts only X_WIDTH+Y_WIDTH bits for dst_id (intentional). No other exemptions expected.

## AR-during-W ordering

When NMU has a W burst in flight on `noc_req_o`, may it inject an AR flit between W beats?

**Decision**: No. AR injection is blocked while a W burst is in progress on the same `noc_req_o` link. The NMU's injection arbiter wormhole-locks to the W-packet slot from the first W flit until the burst's final beat (`wlast=1`, reflected in flit header `last=1`) is accepted by the router; only after the lock releases can the next packet (AW, W, or AR) be granted.

**Rationale**: matches the FlooNoC RTL reference (`hw/floo_axi_chimney.sv` instantiates `floo_wormhole_arbiter` over the AW/W and AR slots; the wormhole arbiter uses `rr_arb_tree` with `LockIn=1` and releases only on `last & ready`). The benefit is W-burst contiguity at the NMU output — W beats arrive at NSU in tight succession with no interleaved foreign flits to filter, simplifying NSU's W-reassembly buffer logic. The cost is potential head-of-line blocking on AR when a slow remote slave back-pressures the W burst; this is acceptable for the target workloads (CPU-driven and DMA-style bulk transfers both tolerate it). Formalised in `protocol_rules.md` `NOC_MST_WORMHOLE_LOCK`.

## ATOPs scope

AXI4 atomic operations (ATOPs) — single-token CAS / SWAP / LOAD-STORE — are **out of scope** for this NI revision. The `awatop` field is sampled and recorded for monitor mode but the BFM and RTL both terminate ATOPs with `bresp=SLVERR` and a single B response (no ATOP read-response generation).

*Reviewer assumption: matches noc-sim §3 parameter list which omits ATOP_SUPPORT. Confirm or upgrade to ATOP_SUPPORT=1 path (would add ~3 weeks of design + DV).*
