# Protocol Rules

**Protocols:**
- AXI4 (ARM IHI 0022) on the host side
- Custom flit-based packet protocol on the NoC side
- AXI4-Lite on the CSR access port

**Roles:** Both MST (NMU initiates AXI on remote-side egress; NSU initiates AXI to local memory) and SLV (NMU receives AXI from local master; NSU receives via NoC). MON (passive monitoring) supported.

**ID format:** Two legal variants — pick based on whether the rule's protocol has channels.

  - **Channel-based protocols** (AXI4): `<PROTO>_<ROLE>_<CHANNEL>_<SHORT_NAME>`
  - **Channel-less protocols** (NoC flit, CSR-via-AXI4-Lite): `<PROTO>_<ROLE>_<SHORT_NAME>`

Component definitions:
- **PROTO**: `AXI4` (host AXI4), `NOC` (NoC packet protocol), `AXI4LITE` (CSR access)
- **ROLE**: `MST` (master / manager), `SLV` (slave / subordinate), `MON` (monitor-only)
- **CHANNEL**: AXI4 channel (`AW`/`W`/`B`/`AR`/`R`); or `RST`, `XCH` (cross-channel), `CFG` (configuration knob)
- **SHORT_NAME**: snake_case

**Severity legend (2 levels, matching ARM Protocol Checker):**
- **FAIL** — protocol violation; BFM signals an error.
- **RECOMMEND** — quality-of-implementation; coverage / observability only.

**ARM SVA equivalent column convention:**
- Verified ARM Protocol Checker IDs listed verbatim
- IDs suffixed `(unverified)` follow ARM naming pattern but await cross-check against ARM DUI 0534B / DUI 0576A
- `(none)` for rules without ARM equivalent (NoC custom protocol, CFG, RST-duration, XCH cross-protocol, etc.)

## Channel naming convention

Rule IDs and sub-section headings in this document use **abstract channel tokens** (`AW`, `W`, `B`, `AR`, `R` for AXI4; wildcard `noc_*_o`/`noc_*_i` patterns for NoC) that alias the per-port / per-direction channels declared in `signal_interface.md` §Channel grouping:

| Token in this document | Aliases (signal_interface.md §Channel grouping) |
|------------------------|--------------------------------------------------|
| `AW` (in rule ID or §AW channel heading) | `AW_IN` (manager port) and `AW_OUT` (subordinate port) — rule applies to whichever port the rule's `<ROLE>` (MST / SLV) selects |
| `W` | `W_IN` and `W_OUT` |
| `B` | `B_IN` and `B_OUT` |
| `AR` | `AR_IN` and `AR_OUT` |
| `R` | `R_IN` and `R_OUT` |
| `noc_*_o.valid` / `noc_*_o.ready` patterns | `REQ_OUT` and `RSP_OUT` (BFM-driven NoC outputs) |
| `noc_*_i.valid` / `noc_*_i.ready` patterns | `REQ_IN` and `RSP_IN` (BFM-observed NoC inputs) |
| `CSR_AW` / `CSR_W` / `CSR_B` / `CSR_AR` / `CSR_R` (in §CSR sub-section headings) | one-to-one with signal_interface tokens; no aliasing needed |

A rule with role `SLV` referencing `AW` applies at the BFM's slave-side AXI port (`axi_*_i` for NMU's manager port acting as slave to local AXI master, `axi_*_o` for NSU's subordinate port). A rule with role `MST` referencing `AW` applies at the BFM's master-side AXI port. NoC `<PROTO>_MST_*` rules apply to BFM-driven NoC outputs (`REQ_OUT` and `RSP_OUT`); `<PROTO>_SLV_*` rules apply to BFM-observed NoC inputs (`REQ_IN` and `RSP_IN`).

## Reset rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_RST_OUTPUTS_LOW_AXI | `arst_ni` is asserted | All NI-driven AXI outputs (axi_rsp_o.*valid, axi_req_o.*valid) must be at their during-reset values per pin_level_reset.md. | FAIL | (none) |
| NI_RST_OUTPUTS_LOW_NOC | `noc_rst_ni` is asserted | All NI-driven NoC outputs (`noc_req_valid_o`, `noc_rsp_valid_o`, `noc_req_credit_o[NUM_VC-1:0]`, `noc_rsp_credit_o[NUM_VC-1:0]`, `noc_req_credit_init_ready_o`, `noc_rsp_credit_init_ready_o`) must be 0. | FAIL | (none) |
| NI_RST_DURATION_AXI | `arst_ni` pulse begins | Held LOW for ≥ 16 `aclk_i` cycles. Shorter pulses leave the AXI side in undefined state. | FAIL | (none) |
| NI_RST_DURATION_NOC | `noc_rst_ni` pulse begins | Held LOW for ≥ 16 `noc_clk_i` cycles. | FAIL | (none) |
| NI_RST_PARTIAL | One reset asserted, the other not | NI may operate in partial state; cross-domain in-flight transactions will not complete. Integrator should ensure the two resets reach a consistent state by power-on completion. | RECOMMEND | (none) |

## CDC rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_CDC_AXI_TO_NOC_FIFO | AXI ingress (NMU AW/W/AR or NSU B/R reception) crosses to NoC injection | NI uses an async FIFO (gray-counter pointer + 2FF sync) to bridge `aclk_i` → `noc_clk_i`. FIFO depth = `CDC_FIFO_DEPTH` parameter (default 16). The default is conservative for the supported clock-ratio range `aclk_period : noc_clk_period ∈ [0.1, 10]`; outside that range, integrator must size per the formula `2 × max_round_trip_cycles × max(aclk_period, noc_clk_period) / min(aclk_period, noc_clk_period) + 2`. The formula is documented in `signal_interface.md` §Parameters under `CDC_FIFO_DEPTH`. | FAIL | (none) |
| NI_CDC_NOC_TO_AXI_FIFO | NoC ingress (NMU response reception or NSU request reception) crosses to AXI egress | Mirror of above; `noc_clk_i` → `aclk_i` async FIFO. | FAIL | (none) |
| NI_CDC_NO_COMBINATIONAL_PATH | Any wire path | No combinational path crosses the AXI ↔ NoC clock boundary inside NI. All cross-domain signals are FIFO'd or 2FF-synchronized. | FAIL | (none) |

## AXI4 host-side rules

(Apply to both `axi_*_i` AXI manager port and `axi_*_o` AXI subordinate port unless noted.)

### AW channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_AW_AWVALID_STABLE | AWVALID rises HIGH | AWVALID must remain HIGH until AWREADY is observed HIGH on a rising `aclk_i` edge. | FAIL | AXI4_ERRM_AWVALID_STABLE |
| AXI4_MST_AW_AWADDR_STABLE | AWVALID is HIGH | AWADDR must not change between AWVALID rise and AWREADY observation. | FAIL | AXI4_ERRM_AWADDR_STABLE (unverified) |
| AXI4_MST_AW_AWLEN_STABLE | AWVALID is HIGH | AWLEN must not change. | FAIL | AXI4_ERRM_AWLEN_STABLE (unverified) |
| AXI4_MST_AW_AWSIZE_STABLE | AWVALID is HIGH | AWSIZE must not change. | FAIL | AXI4_ERRM_AWSIZE_STABLE (unverified) |
| AXI4_MST_AW_AWBURST_STABLE | AWVALID is HIGH | AWBURST must not change. | FAIL | AXI4_ERRM_AWBURST_STABLE (unverified) |
| AXI4_MST_AW_AWID_STABLE | AWVALID is HIGH | AWID must not change. | FAIL | AXI4_ERRM_AWID_STABLE (unverified) |
| AXI4_MST_AW_AWQOS_USED | AW handshake completes | AWQOS is sampled and either passed through (Bypass mode) or replaced by `QoSGen` (Fixed/Limiter/Regulator). See FR-07 in source. | FAIL | (none) |
| AXI4_SLV_AW_BURST_4KB_BOUNDARY | AW handshake with `awlen > 0` and `awburst == INCR` | The full burst (`awaddr` + `(awlen+1) * (1 << awsize)`) must not cross a 4KB address boundary. NSU rejects with `bresp=SLVERR` and does not propagate to memory. WRAP bursts are bounded within their wrap boundary by AXI4 construction (always within 4KB); FIXED bursts hold address constant — neither needs explicit 4KB enforcement. | FAIL | AXI4_ERRM_AWADDR_BOUNDARY (unverified) |
| AXI4_MST_AW_AWUSER_STABLE | AWVALID is HIGH | AWUSER must not change between AWVALID rise and AWREADY observation. | FAIL | AXI4_ERRM_AWUSER_STABLE (unverified) |
| AXI4_MST_AW_AWCACHE_STABLE | AWVALID is HIGH | AWCACHE must not change between AWVALID rise and AWREADY observation. | RECOMMEND | AXI4_RECM_AWCACHE_STABLE (unverified) |
| AXI4_MST_AW_AWPROT_STABLE | AWVALID is HIGH | AWPROT must not change between AWVALID rise and AWREADY observation. | FAIL | AXI4_ERRM_AWPROT_STABLE (unverified) |

### W channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_W_WVALID_STABLE | WVALID rises HIGH | WVALID must remain HIGH until WREADY observed HIGH on a rising `aclk_i` edge. | FAIL | AXI4_ERRM_WVALID_STABLE (unverified) |
| AXI4_MST_W_WDATA_STABLE | WVALID is HIGH | WDATA must not change between WVALID rise and WREADY observation. | FAIL | AXI4_ERRM_WDATA_STABLE (unverified) |
| AXI4_MST_W_WSTRB_STABLE | WVALID is HIGH | WSTRB must not change between WVALID rise and WREADY observation. | FAIL | AXI4_ERRM_WSTRB_STABLE (unverified) |
| AXI4_MST_W_WUSER_STABLE | WVALID is HIGH | WUSER must not change. | FAIL | AXI4_ERRM_WUSER_STABLE (unverified) |
| AXI4_MST_W_WLAST_STABLE | WVALID is HIGH | WLAST must not change. | FAIL | AXI4_ERRM_WLAST_STABLE (unverified) |
| AXI4_MST_W_WLAST_LEN_CONSISTENT | WLAST asserted on a W beat | The total beat count for the corresponding AW transaction equals AWLEN+1 (i.e., WLAST asserts on beat AWLEN+1, not earlier or later). | FAIL | AXI4_ERRM_WLAST_LEN (unverified) |
| AXI4_MST_W_WSTRB_NARROW | WVALID is HIGH and AWSIZE < log2(DATA_WIDTH/8) | WSTRB bits outside the addressed lanes (per AWSIZE) must be 0. | FAIL | AXI4_ERRM_WSTRB_NARROW (unverified) |

### B channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_SLV_B_BVALID_STABLE | BVALID rises HIGH | BVALID must remain HIGH until BREADY observed HIGH on a rising `aclk_i` edge. | FAIL | AXI4_ERRS_BVALID_STABLE (unverified) |
| AXI4_SLV_B_BID_STABLE | BVALID is HIGH | BID must not change between BVALID rise and BREADY observation. | FAIL | AXI4_ERRS_BID_STABLE (unverified) |
| AXI4_SLV_B_BRESP_STABLE | BVALID is HIGH | BRESP must not change. | FAIL | AXI4_ERRS_BRESP_STABLE (unverified) |
| AXI4_SLV_B_BUSER_STABLE | BVALID is HIGH | BUSER must not change. | FAIL | AXI4_ERRS_BUSER_STABLE (unverified) |
| AXI4_SLV_B_BRESP_VALUES | BVALID + BREADY handshake | BRESP ∈ {OKAY=2'b00, SLVERR=2'b10, DECERR=2'b11}. EXOKAY (2'b01) is reserved for exclusive access and not used by this NI. | FAIL | AXI4_ERRS_BRESP_VALUES (unverified) |
| AXI4_SLV_B_BID_MATCH | B handshake completes | BID must match an outstanding AWID for the same NMU (i.e., a write transaction was issued with this AWID and is still in flight). | FAIL | AXI4_ERRS_BID_MATCH (unverified) |

### AR channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_AR_ARVALID_STABLE | ARVALID rises HIGH | ARVALID must remain HIGH until ARREADY observed HIGH. | FAIL | AXI4_ERRM_ARVALID_STABLE (unverified) |
| AXI4_MST_AR_ARADDR_STABLE | ARVALID is HIGH | ARADDR must not change. | FAIL | AXI4_ERRM_ARADDR_STABLE (unverified) |
| AXI4_MST_AR_ARLEN_STABLE | ARVALID is HIGH | ARLEN must not change. | FAIL | AXI4_ERRM_ARLEN_STABLE (unverified) |
| AXI4_MST_AR_ARSIZE_STABLE | ARVALID is HIGH | ARSIZE must not change. | FAIL | AXI4_ERRM_ARSIZE_STABLE (unverified) |
| AXI4_MST_AR_ARBURST_STABLE | ARVALID is HIGH | ARBURST must not change. | FAIL | AXI4_ERRM_ARBURST_STABLE (unverified) |
| AXI4_MST_AR_ARID_STABLE | ARVALID is HIGH | ARID must not change. | FAIL | AXI4_ERRM_ARID_STABLE (unverified) |
| AXI4_MST_AR_ARCACHE_STABLE | ARVALID is HIGH | ARCACHE must not change. | RECOMMEND | AXI4_RECM_ARCACHE_STABLE (unverified) |
| AXI4_MST_AR_ARPROT_STABLE | ARVALID is HIGH | ARPROT must not change. | FAIL | AXI4_ERRM_ARPROT_STABLE (unverified) |
| AXI4_MST_AR_ARQOS_USED | AR handshake completes | ARQOS is sampled and either passed through (Bypass mode) or replaced by `QoSGen`. | FAIL | (none) |
| AXI4_SLV_AR_BURST_4KB_BOUNDARY | AR handshake with `arlen > 0` and `arburst == INCR` | Full burst must not cross 4KB boundary. NSU rejects with `rresp=SLVERR` on the affected beat. WRAP bursts are bounded within their wrap boundary by AXI4 construction (always within 4KB); FIXED bursts hold address constant — neither needs explicit 4KB enforcement. | FAIL | AXI4_ERRM_ARADDR_BOUNDARY (unverified) |

### R channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_SLV_R_RVALID_STABLE | RVALID rises HIGH | RVALID must remain HIGH until RREADY observed HIGH. | FAIL | AXI4_ERRS_RVALID_STABLE (unverified) |
| AXI4_SLV_R_RID_STABLE | RVALID is HIGH | RID must not change between RVALID rise and RREADY observation. | FAIL | AXI4_ERRS_RID_STABLE (unverified) |
| AXI4_SLV_R_RDATA_STABLE | RVALID is HIGH | RDATA must not change. | FAIL | AXI4_ERRS_RDATA_STABLE |
| AXI4_SLV_R_RRESP_STABLE | RVALID is HIGH | RRESP must not change. | FAIL | AXI4_ERRS_RRESP_STABLE (unverified) |
| AXI4_SLV_R_RLAST_STABLE | RVALID is HIGH | RLAST must not change. | FAIL | AXI4_ERRS_RLAST_STABLE (unverified) |
| AXI4_SLV_R_RUSER_STABLE | RVALID is HIGH | RUSER must not change. | FAIL | AXI4_ERRS_RUSER_STABLE (unverified) |
| AXI4_SLV_R_RRESP_VALUES | RVALID + RREADY handshake | RRESP ∈ {OKAY=2'b00, SLVERR=2'b10, DECERR=2'b11}. EXOKAY unused. | FAIL | AXI4_ERRS_RRESP_VALUES (unverified) |
| AXI4_SLV_R_RLAST_LEN_CONSISTENT | RLAST asserted on a R beat | Total beat count for the corresponding AR transaction equals ARLEN+1. | FAIL | AXI4_ERRS_RLAST_LEN (unverified) |
| AXI4_SLV_R_RID_MATCH | R handshake completes | RID must match an outstanding ARID for the same NMU. | FAIL | AXI4_ERRS_RID_MATCH (unverified) |

### Cross-channel (XCH) rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_SLV_XCH_W_AFTER_AW | WREADY asserted | Corresponding AW handshake completed in the same or earlier cycle. NSU does not accept W before AW for the same write. | FAIL | (none) |
| AXI4_SLV_XCH_B_AFTER_AW_AND_W | BVALID asserted | Both AW and W phases must have completed in same or earlier cycle. | FAIL | (none) |
| AXI4_SLV_XCH_R_AFTER_AR | RVALID asserted | AR phase must have completed. | FAIL | (none) |
| AXI4_SLV_XCH_R_LAST_CONSISTENT | RVALID + RREADY handshake with RLAST=1 | Total R beats observed for the corresponding AR == ARLEN+1. | FAIL | (none) |

### Per-AXI-ID ordering (RoB-managed)

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_RoB_PER_ID_ORDER | Multiple outstanding transactions with same AWID/ARID | Responses for transactions with the same ID must be released in issue order. NMU RoB enforces. Different IDs may complete out of order. | FAIL | (none) |
| AXI4_MST_RoB_OUTSTANDING_LIMIT | New transaction issued while NMU RoB is full | NMU back-pressures by deasserting `awready` / `arready` until a slot frees. | FAIL | (none) |
| AXI4_SLV_R_INTERLEAVE_CROSS_ID | Multiple outstanding R bursts with different `arid` simultaneously in flight at the NMU | NMU MAY interleave R-flit packets from different `arid`s on `axi_r*_o` at packet boundary granularity (one full burst at a time per packet). Within the same `arid`, beats MUST be contiguous and in burst order (per AXI4 §A5.3); inter-burst interleaving on the same `arid` is forbidden. NMU implementation note: header `last` from `noc_rsp_i` flit demarcates packet boundaries for the wormhole-locked path. | FAIL | (none) |
| AXI4_SLV_NSU_AW_BURST_WRAP_REPLAY | NSU receives AW + W flits with `awburst == WRAP` | NSU MUST replay per-beat addresses on `axi_awaddr_o` per AXI4 §A3.4.1 wrap formula: each beat's address wraps within the burst's wrap boundary (`(awaddr & ~((awlen+1) << awsize)) | ((awaddr + i × (1 << awsize)) & ((awlen+1) << awsize))`). Wrap boundary by AXI4 construction stays within 4KB. | FAIL | (none) |
| AXI4_SLV_NSU_AW_BURST_FIXED_REPLAY | NSU receives AW + W flits with `awburst == FIXED` | NSU MUST replay the same `awaddr` on every beat of `axi_awaddr_o`; address does NOT increment between beats. | FAIL | (none) |

### AXI4 Exclusive Access

These rules govern AXI4 Exclusive (`AxLOCK = Exclusive`) transactions and the NSU-side Exclusive Monitor (per ToO §"NSU Exclusive Monitor (NSU sub-block)"). The NI implements a per-NSU monitor with `EXCLUSIVE_MONITOR_DEPTH` reservation slots; coherency across multiple NIs is out of scope per ToO §"NSU Exclusive Monitor".

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_EXCLUSIVE_SINGLE_BEAT | AW or AR handshake completes with `awlock` / `arlock = Exclusive` (`AxLOCK=01`) | The accompanying burst MUST be single-beat (`awlen = 0` / `arlen = 0`) per AXI4 §A7. NSU rejects multi-beat Exclusive bursts with `bresp = SLVERR` (writes) or `rresp = SLVERR` on every beat (reads). | FAIL | AXI4_ERRS_EXCLUSIVE_BURST_LEN (unverified) |
| AXI4_EXCLUSIVE_MONITOR_OVERFLOW | NSU receives an Exclusive AR while its Exclusive Monitor is at capacity (`EXCLUSIVE_MONITOR_DEPTH` reservations already pending) | NSU MUST reject the new Exclusive AR by responding with `rresp = SLVERR` (cannot guarantee exclusivity tracking). Software is expected to retry the access as non-Exclusive or back off. The reservation table is NOT silently evicted. | FAIL | (none) |
| AXI4_EXCLUSIVE_OVERLAP_INVALIDATE | NSU receives any normal (non-Exclusive) write whose `(awaddr, awsize, awlen)` overlaps a pending Exclusive read reservation in the Exclusive Monitor | NSU MUST invalidate that reservation entry. A subsequent matching Exclusive write from the original master then misses the reservation and is downgraded to a normal write with `bresp = OKAY` (not `EXOKAY`); the reservation slot is freed. Per AXI4 §A7. | FAIL | (none) |

### Outstanding-transaction timeout

This sub-section formalises the NMU-side timeout mechanism that converts "missing response" events (slave never responds, flit dropped by `route_par` check, fabric loss) into observable AXI SLVERR. The timeout is the sole AXI-rresp-generating mechanism on the NoC-fabric error path — fabric ECC checks themselves never synthesise rresp values (per §"ECC and routing-parity"). Default timeout 10 000 `aclk_i` cycles.

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_TIMEOUT_SLVERR | An NMU outstanding-transaction tracker entry (allocated on AW or AR injection) has been waiting for its corresponding response (B for writes, R for reads) for ≥ `TXN_TIMEOUT` `aclk_i` cycles (default 10 000) | NMU MUST drive `bresp = SLVERR` (write) or `rresp = SLVERR` on the affected beat (read) at the AXI master port; increment `ERR_COUNT` (saturating, per `registers.md`); set `ERR_STATUS[1] timeout_err`; capture `LAST_ERR_INFO` if no prior un-cleared error is sticky; release the tracker entry. The triggering flit may have been lost in fabric, dropped by `NOC_FLIT_HDR_ROUTE_PAR_CHECK`, or stalled at an unresponsive slave — software disambiguates via `LAST_ERR_INFO` plus other counters. | FAIL | (none) |

### AXI host-side parity

These rules govern parity check at the AXI host boundary when `ENABLE_AXI_PARITY = true` (default per `signal_interface.md`). Parity covers data (1 bit per byte) and address (1 bit per AxAddress), per AMD pg313 §Data Integrity. Mismatch is logged but does NOT propagate as AXI rresp, consistent with the (B)-philosophy fabric-error treatment (rresp channel reserved for end-to-end / timeout-driven SLVERR).

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_MST_PARITY_CHECK | NMU samples `axi_awaddr_par_i[ADDR_WIDTH/8-1:0]`, `axi_araddr_par_i[ADDR_WIDTH/8-1:0]`, or `axi_wdata_par_i[DATA_WIDTH/8-1:0]` on its corresponding handshake (AW / AR / W) | NMU MUST recompute per-byte even parity over each protected byte and compare to the corresponding sampled `_par_i` bit. On any byte mismatch: increment `AXI_PARITY_ERR_CNT` (saturating, per `registers.md`); set `ERR_STATUS[3] axi_parity_err`; capture `LAST_ERR_INFO` if no prior un-cleared error is sticky. The transaction proceeds — NMU does NOT inject an AXI rresp value. The corrupted field is forwarded into the NoC layer where `flit_ecc` provides the next layer of protection. Aligned with AMD pg313 §Parity standard configuration ("1 bit per byte for Data" / "1 bit per byte for AxAddress"). | FAIL | (none) |
| AXI4_SLV_PARITY_CHECK | NSU samples `axi_rdata_par_i[DATA_WIDTH/8-1:0]` on R handshake from the local AXI slave | NSU MUST recompute per-byte even parity over each `axi_rdata_i` byte and compare to the corresponding sampled `_par_i` bit. On any byte mismatch: increment `AXI_PARITY_ERR_CNT` (same counter as `AXI4_MST_PARITY_CHECK` — both directions accumulate together); set `ERR_STATUS[3] axi_parity_err`; capture `LAST_ERR_INFO` if no prior un-cleared error is sticky. The R beat is forwarded into the NoC with `rresp = OKAY` (the NI does NOT synthesise SLVERR from its own parity check). Aligned with AMD pg313 §Parity per-byte standard configuration. | FAIL | (none) |
| AXI4_MST_PARITY_GEN_R | NMU is forwarding an R beat to the AXI master after passing the `flit_ecc` check stage (per `NOC_FLIT_HDR_FLIT_ECC_CHECK`) | NMU MUST regenerate per-byte even parity over `axi_rdata_o[DATA_WIDTH-1:0]` and drive `axi_rdata_par_o[DATA_WIDTH/8-1:0]` on the same R handshake. Generation point: **after** the `flit_ecc` check stage, when the NoC packet is converted to AXI protocol — aligned with AMD pg313 §Parity verbatim: "Data parity for read responses is generated as 1 bit per byte after the ECC check stage, when the data is converted from NPP to AXI protocol." If `flit_ecc` reports an uncorrectable double-bit error on this R flit (per `NOC_FLIT_HDR_FLIT_ECC_CHECK`), parity is computed over the corrupted (forwarded-as-is) `axi_rdata_o` — parity reflects what is actually on the wire, not what *should* have been. AXI master can then independently detect corruption via parity check (in addition to the CSR / IRQ surface). | FAIL | (none) |

## NoC flit-side rules

### NoC link credit-based handshake

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_MST_FLIT_ON_CREDIT_ONLY | NMU or NSU drives `noc_*_valid_o = 1` on a NoC link | The source MUST hold ≥ 1 credit on the chosen VC before asserting valid. The credit counter for that VC decrements by 1 on the cycle `noc_*_valid_o = 1`. Per AMD pg313 §Credit-Based Flow Control verbatim: "Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver." | FAIL | (none) |
| NOC_MST_WORMHOLE_LOCK | First flit of a packet has been injected on a `noc_*_o` link (`noc_req_o` for NMU AW/W/AR; `noc_rsp_o` for NSU B/R) and the packet's `last=1` flit has not yet been accepted | NMU/NSU MUST NOT inject any flit from a different packet on the same `noc_*_o` link until the in-flight packet's `last=1` flit is accepted by the receiving router. The wormhole-lock is per-packet: single-flit packets (AW, AR, B) release the lock immediately on the cycle they are accepted; multi-flit packets (W burst, R burst) hold the lock until their final beat (`wlast`/`rlast`, reflected in flit header `last=1`) is accepted. Implementation reference: FlooNoC `hw/floo_axi_chimney.sv` injection arbiter via `floo_wormhole_arbiter.sv` (`rr_arb_tree` with `LockIn=1`). Consequence for AXI4: AR injection is blocked while a corresponding W burst is in progress on `noc_req_o`. | FAIL | (none) |
| NOC_CREDIT_STARTUP_HANDSHAKE | `noc_rst_ni` has just deasserted | Before exchanging any flit, NMU/NSU and the attached router MUST complete a bi-directional credit-init handshake on `noc_*_credit_init_ready_o` / `noc_*_credit_init_ready_i`. Each side asserts its `_o` line when ready. The cycle both sides observe each other's `_i` HIGH establishes the credit-exchange epoch. Source-credit counters seed at `INPUT_BUFFER_DEPTH / NUM_VC` per VC on that cycle. Until both `_o` and `_i` are HIGH simultaneously, no flit may be injected. Per AMD pg313 §Credit-Based Flow Control verbatim: "The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange." Rule applies independently per direction (request link, response link). | FAIL | (none) |

### Flit format / header invariants

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_FLIT_HDR_QOS_4BIT | Any flit injected | Header `qos` field is 4 bits ([3:0] of the QOS_WIDTH-bit slot). Generated by QoS Generator (NMU) or copied from request (NSU response). | FAIL | (none) |
| NOC_FLIT_HDR_AWAR_PAYLOAD_NO_QOS | AW or AR flit injected | AW/AR flit payload does NOT carry a separate `qos` copy; the flit header `qos` field is the unique source. NSU MUST NOT look for qos in payload; consumers (router arbiter, NSU) read header `qos` only. | FAIL | (none) |
| NOC_FLIT_HDR_DST_ID_VALID | Request flit injected | `dst_id` must reference an existing node in the mesh — either XY coordinates within `MESH_COLS × MESH_ROWS` bounds, or a `dst_id` produced by SAM table lookup when `USE_ID_TABLE=1`. Out-of-bounds `dst_id` is an NMU bug. | FAIL | (none) |
| NOC_FLIT_HDR_ROB_IDX_UNIQUE | Multiple flits in flight from same NMU | `rob_idx` of all in-flight requests must be unique within the NMU; duplicate is RoB allocator bug. | FAIL | (none) |
| NOC_FLIT_HDR_SRC_ID_VALID | Any flit injected on `noc_req_o` (NMU) or `noc_rsp_o` (NSU) | `src_id` field equals the injecting NI's own `id_i` strap value (XY coordinate of this NI's node). The router fabric uses `src_id` for response-path routing back from NSU to originating NMU. | FAIL | (none) |
| NOC_FLIT_HDR_AXI_CH_VALUES | Any flit injected | Header `axi_ch` field carries the AXI channel enum: `AW=0`, `W=1`, `AR=2`, `B=3`, `R=4`. Request flits (`AW`/`W`/`AR`) MUST be injected on `noc_req_o`; response flits (`B`/`R`) MUST be injected on `noc_rsp_o`. Routers use `axi_ch` to route flits to the matching physical NoC channel. | FAIL | (none) |
| NOC_FLIT_HDR_LAST_CONSISTENT | Any flit injected | Header `last` bit indicates packet boundary: single-flit packets (AW, AR, B) MUST have `last=1` always; multi-flit packets (W burst, R burst) MUST have `last=0` on every flit except the final beat, on which `last=1`. For W flits, header `last` MUST equal AXI `wlast` of the corresponding W beat; for R flits, header `last` MUST equal AXI `rlast` of the corresponding R beat. Wormhole arbiter behaviour (per `NOC_MST_WORMHOLE_LOCK`) depends on this bit. | FAIL | (none) |
| NOC_FLIT_HDR_ROB_REQ_GEN | NMU injects request flit (AW or AR) | Header `rob_req` bit indicates whether the NMU expects per-AXI-ID-ordered response release: `rob_req=1` allocates a RoB entry and waits for in-order release; `rob_req=0` is fast-path (NSU response is delivered to AXI as soon as received, bypassing RoB ordering). When `rob_req=0`, header `rob_idx` is don't-care. | FAIL | (none) |
| NOC_FLIT_HDR_RSVD_ZERO_TX | NMU/NSU injects any flit on `noc_*_o` | Reserved header fields (`rsvd_commtype`, `multicast`, `rsvd_mc_status`) MUST be zero on transmission. Reserved fields are placeholders for future extensions. | FAIL | (none) |
| NOC_FLIT_HDR_RSVD_IGNORE_RX | NMU/NSU receives any flit on `noc_*_i` | Reserved header fields received from a future-revision peer MUST be ignored — non-zero values in reserved bit positions MUST NOT be interpreted as protocol violations or trigger error counters. This preserves forward compatibility with future header extensions. | RECOMMEND | (none) |
| NOC_FLIT_RSP_QOS_INHERIT | NSU injects B or R response flit | Response flit header `qos` field equals the corresponding original request flit's header `qos` (preserved in NSU's `MetaBuffer` per request). NSU MUST NOT regenerate qos via QoS Generator; QoS computation occurs only on AW/AR injection at NMU. | FAIL | (none) |
| NOC_FLIT_RSP_ROB_IDX_INHERIT | NSU injects B or R response flit | Response flit header `rob_idx` equals the corresponding original request flit's `rob_idx` (preserved in NSU's `MetaBuffer`). NMU uses this to look up the originating RoB entry on response reception (per ToO §RoB allocator). When the originating request had `rob_req=0`, the inherited `rob_idx` is don't-care at NMU. | FAIL | (none) |
| NOC_FLIT_AW_W_ORDER | AW flit and corresponding W flits | NMU injects AW flit before any of its corresponding W flits onto `noc_req_o`. (W burst is independent wormhole; ordering at NMU output port is FIFO-natural.) | FAIL | (none) |

### ECC and routing-parity

This sub-section formalises the v0.4.0 two-layer integrity scheme (per ToO §ECC), aligned with AMD pg313 §Data Integrity. Layer 1 is `route_par`: a 1-bit even parity over `{dst_id, last}` (aligned with AMD pg313 §Parity verbatim — "The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity"), generated at NMU/NSU egress and checked at every router output port and at every NI sink. Layer 2 is `flit_ecc`: whole-flit SECDED Hamming over the entire flit excluding the `flit_ecc` field, generated at NMU/NSU egress and checked **only at the destination NI sink** (routers do not check or regenerate, per AMD pg313 §Data Integrity verbatim — "No ECC checking is performed in the switch fabric"). Single-bit errors are corrected silently. Double-bit errors are forwarded to AXI with logging — the NI does not synthesise AXI rresp values from fabric ECC checks (per ToO §ECC §"Double-bit (uncorrectable) errors" rationale).

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_FLIT_HDR_ROUTE_PAR_GEN | NMU or NSU injecting a flit on `noc_req_o` / `noc_rsp_o` | Compute `route_par = ^{dst_id, last}` (XOR-reduction over 9 bits at default; even-parity convention) and place in the flit-header `route_par` field. Width = `ROUTE_PAR_WIDTH` (fixed 1). Aligned with AMD pg313 §Parity (NPP packet DST ID + LAST coverage). | FAIL | (none) |
| NOC_FLIT_HDR_ROUTE_PAR_CHECK | A router output port or an NI sink receives a flit | Recompute `route_par` over the received `{dst_id, last}` and compare to header `route_par` field. Mismatch → drop the flit (do NOT forward; misrouting or wormhole-arbiter-disruption risk), increment `ROUTE_PAR_ERR_CNT` (saturating, per `registers.md`), set `ERR_STATUS[2] route_par_err`, capture `LAST_ERR_INFO` if no prior un-cleared error is sticky. The originating NMU's outstanding-transaction tracker eventually times out on the missing response (handled by `AXI4_MST_TIMEOUT_SLVERR`). | FAIL | (none) |
| NOC_FLIT_HDR_FLIT_ECC_GEN | NMU or NSU injecting a flit on `noc_req_o` / `noc_rsp_o` | Compute SECDED Hamming code over the entire flit (`flit[FLIT_WIDTH-1:0]`) excluding the `flit_ecc` field bits, and place the resulting `FLIT_ECC_WIDTH`-bit syndrome (default 10) in the flit-header `flit_ecc` field. Width parameter constraint per `signal_interface.md` §Parameters under `FLIT_ECC_WIDTH`. | FAIL | (none) |
| NOC_FLIT_HDR_FLIT_ECC_CHECK | An NI sink (NMU on R-flit reception, NSU on W-flit / AW-flit / AR-flit reception) accepts a flit | Recompute SECDED syndrome and compare. Single-bit error: correct the bit silently, increment `ECC_CORR_ERR_CNT` (pure saturating, no clear path; informational only), forward the corrected flit downstream. Double-bit error: forward the corrupted flit downstream **as-is** to AXI with `bresp=OKAY` / `rresp=OKAY` (the NI does NOT synthesise SLVERR from this check), increment `ECC_UNCORR_ERR_CNT`, set `ERR_STATUS[0] ecc_uncorr_err`, capture `LAST_ERR_INFO` if no prior un-cleared error is sticky. Routers do NOT execute this check; intermediate-hop integrity relies on `route_par` only. | FAIL | (none) |

### NoC VC management

These rules govern per-VC injection and reception when `NUM_VC > 1` (default 1). Per ToO §"VC Mapping" and `signal_interface.md` §"NoC credit signals" (per-VC credit return). With `NUM_VC = 1`, the partition rule degenerates trivially. The hard-lock and in-order rules still apply (single-VC is a 1-element partition).

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_FLIT_VC_HARDLOCK | A wormhole-arbiter at NMU (or any router along the path) has granted a packet's first flit on a particular VC | All subsequent flits of that packet (W burst beats, R burst beats, single-flit AW/AR/B alike) MUST traverse the same VC at every NMU/router/NSU hop along the route. Mid-packet VC switching is forbidden. The lock releases when the packet's `last=1` flit is accepted. QoS-aware arbitration operates only at packet boundaries — a higher-QoS packet arriving on the same VC during an in-flight low-QoS packet MUST wait for the lock to release. | FAIL | (none) |
| NOC_VC_PARTITION | NMU / NSU instantiated with `NUM_VC ∈ {1, 2, 4, 8}` (validated values per ToO §"VC partition policy") | The VCs SHOULD be partitioned into request and response subsets per the ToO recommended table (e.g., `NUM_VC=2` → VC[0]=request, VC[1]=response). Exact partition is integrator-configurable but the default partition is what the BFM models. Non-recommended partitions (e.g., `NUM_VC=3`) are not validated; integrator's responsibility to verify deadlock-freedom. | RECOMMEND | (none) |
| NOC_VC_MAPPING_HYBRID_RW_QOS | NMU / NSU constructs an outbound flit when `NUM_VC > 1` | The flit's `vc_id` is assigned by Hybrid R/W × QoS mapping. VCs are partitioned into request and response subsets (per `NOC_VC_PARTITION`). The R/W bit (request flits → request-subset, response flits → response-subset) selects the subset. Within the subset, the QoS tier selects which VC of that subset. Mapping is a function of `(R/W, qos)` only. Policy is fixed at design time, no runtime alternative. Cycle-level VC arbitration in the network switch is a separate function (NPS-side, per AMD pg313 §Virtual Channel Arbitration), out of NI scope. | FAIL | (none) |
| NOC_FLIT_INORDER_PER_VC | Multiple flits in flight along the same `(src_id, dst_id, vc_id)` tuple | Routers MUST preserve flit order along that tuple. Flits from different `(src_id, dst_id, vc_id)` tuples MAY interleave at router output ports per QoS arbitration. Same-tuple in-order delivery is the assumption underlying NormalRoB's `prev_dest` adaptive bypass (per ToO §RoB allocator). | FAIL | (none) |

### Width-conversion (Upsize / Downsize)

These rules govern NMU-side Upsize and NSU-side Downsize behaviour at the AXI ↔ flit boundary when `DATA_WIDTH` differs from `FLIT_PAYLOAD_WIDTH`. Per ToO §"NMU Upsize / NSU Downsize (data-width conversion)" and §"Over-fetch and WSTRB regeneration".

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_WSTRB_REGEN_NMU | NMU is performing Upsize on a W burst (`DATA_WIDTH < FLIT_PAYLOAD_WIDTH`) and accumulates AXI W beats into a wide flit | NMU MUST regenerate the wide-flit `wstrb` field per AXI byte-lane mapping: each AXI input `wstrb` byte at AXI byte position `b` maps to the wide-flit byte position `b'` derived from `awaddr` + per-beat offset + `awsize`. Bytes in the wide flit not driven by any AXI W beat (over-fetch lanes) MUST carry `wstrb=0`. Bytes outside the addressed lanes for narrow transfers (per `awsize` < `log2(DATA_WIDTH/8)`) also carry `wstrb=0`. The NSU on the receiving end relies on this mask to gate which bytes commit. | FAIL | (none) |
| AXI4_OVERFETCH_NSU_FILTER | NSU receives a wide W flit and is performing Downsize (`DATA_WIDTH > FLIT_PAYLOAD_WIDTH`) or pass-through to a slave whose `DATA_WIDTH` matches the flit | NSU MUST filter the W flit's byte-lanes by the regenerated `wstrb` field: only bytes with `wstrb=1` are committed to the local AXI slave's W beat(s). Over-fetched bytes (those with `wstrb=0` from the NMU side) MUST NOT be visible to the slave. AXI4 §A3.4.4 `wstrb`-as-canonical-validity contract is preserved end-to-end. | FAIL | (none) |

## CSR access (AXI4-Lite) rules

The CSR access port is AXI4-Lite subordinate. AXI4-Lite is a subset of AXI4: single-beat transactions (no AWLEN/ARLEN/AWBURST/ARBURST), no AWID/ARID, fixed-size accesses. Standard AXI4-Lite STABLE / VALUES rules apply.

### CSR write channels (AW + W + B)

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_AW_AWVALID_STABLE | csr_awvalid_i rises HIGH | csr_awvalid_i must remain HIGH until csr_awready_o observed HIGH. | FAIL | AXI4LITE_ERRM_AWVALID_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWADDR_STABLE | csr_awvalid_i is HIGH | csr_awaddr_i must not change. | FAIL | AXI4LITE_ERRM_AWADDR_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWPROT_STABLE | csr_awvalid_i is HIGH | csr_awprot_i must not change. | FAIL | AXI4LITE_ERRM_AWPROT_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWADDR_ALIGNED | csr_awvalid_i + csr_awready_o handshake | csr_awaddr_i must be 4-byte-aligned (lower 2 bits = 0). Misaligned writes cause `csr_bresp_o=SLVERR`. | FAIL | (none) |
| AXI4LITE_SLV_W_WVALID_STABLE | csr_wvalid_i rises HIGH | Until csr_wready_o observed HIGH. | FAIL | AXI4LITE_ERRM_WVALID_STABLE (unverified) |
| AXI4LITE_SLV_W_WDATA_STABLE | csr_wvalid_i is HIGH | csr_wdata_i must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_W_WSTRB_STABLE | csr_wvalid_i is HIGH | csr_wstrb_i must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_B_BVALID_STABLE | csr_bvalid_o rises HIGH | Until csr_bready_i observed HIGH. | FAIL | AXI4LITE_ERRS_BVALID_STABLE (unverified) |
| AXI4LITE_SLV_B_BRESP_VALUES | csr_bvalid_o + csr_bready_i handshake | csr_bresp_o ∈ {OKAY=2'b00, SLVERR=2'b10, DECERR=2'b11}. SLVERR for misaligned address or RW1C-write-to-RO-bit; DECERR for unmapped offset. | FAIL | (none) |
| AXI4LITE_SLV_XCH_W_AFTER_AW | csr_wready_o asserted | Corresponding csr_awvalid_i + csr_awready_o handshake must have completed (or be on the same cycle). | FAIL | (none) |
| AXI4LITE_SLV_XCH_B_AFTER_AW_AND_W | csr_bvalid_o asserted | Both AW and W phases of the same write must have completed. | FAIL | (none) |

### CSR read channels (AR + R)

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_AR_ARVALID_STABLE | csr_arvalid_i rises HIGH | Until csr_arready_o observed HIGH. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARADDR_STABLE | csr_arvalid_i is HIGH | csr_araddr_i must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARPROT_STABLE | csr_arvalid_i is HIGH | csr_arprot_i must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARADDR_ALIGNED | csr_arvalid_i + csr_arready_o handshake | csr_araddr_i must be 4-byte-aligned. Misaligned reads cause `csr_rresp_o=SLVERR`. | FAIL | (none) |
| AXI4LITE_SLV_R_RVALID_STABLE | csr_rvalid_o rises HIGH | Until csr_rready_i observed HIGH. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RDATA_STABLE | csr_rvalid_o is HIGH | csr_rdata_o must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RRESP_STABLE | csr_rvalid_o is HIGH | csr_rresp_o must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RRESP_VALUES | csr_rvalid_o + csr_rready_i handshake | csr_rresp_o ∈ {OKAY, SLVERR, DECERR}. DECERR for unmapped offset. | FAIL | (none) |
| AXI4LITE_SLV_R_RLAST_NOT_REQUIRED | csr_rvalid_o + csr_rready_i handshake | AXI4-Lite reads are single-beat; no RLAST signal. (NI's CSR port omits csr_rlast.) | FAIL | (none) |
| AXI4LITE_SLV_XCH_R_AFTER_AR | csr_rvalid_o asserted | Corresponding csr_arvalid_i + csr_arready_o handshake must have completed. | FAIL | (none) |

### CSR address policy

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_UNMAPPED_DECERR | Read or write to a CSR offset not listed in registers.md §Register map | csr_bresp_o=DECERR (for write) or csr_rresp_o=DECERR (for read). | FAIL | (none) |
| AXI4LITE_SLV_RO_WRITE_IGNORED | Write to a Read-Only register (per registers.md Access column) | Write data is silently ignored; csr_bresp_o=OKAY (write succeeds at the bus level but has no effect). Software contract: don't write to RO. | RECOMMEND | (none) |
| AXI4LITE_SLV_RW1C_WRITE_BIT_LEVEL | Write to a RW1C register | For each bit position: software writes 1 → bit clears + associated counter clears (per registers.md §ERR_STATUS); software writes 0 → no effect (bit retains current state). | FAIL | (none) |

## Configuration-knob rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_CFG_QOS_MODE_TRANSITION | `QOS_MODE` CSR written | New mode applies to the NEXT AW/AR flit injection; in-flight transactions retain the QoS computed at their injection time. | FAIL | (none) |
| NI_CFG_QOS_FIXED_VALUE | `QOS_MODE = Fixed`; `QOS_FIXED_VALUE` CSR written | Next AW/AR flit's `qos` header field equals `QOS_FIXED_VALUE`, regardless of AXI awqos/arqos input. | FAIL | (none) |
| NI_CFG_BANDWIDTH_LIMIT_BOUND | `BANDWIDTH_LIMIT` CSR written; `QOS_MODE = Limiter` | Limiter counter increments per request bytes, decrements per cycle by `BANDWIDTH_LIMIT`; QoS drops to `LOW_PRIORITY` when counter > `SATURATION_THRESHOLD`. Saturating arithmetic. | FAIL | (none) |
| NI_CFG_BANDWIDTH_BUDGET_BOUND | `BANDWIDTH_BUDGET` CSR written; `QOS_MODE = Regulator` | Per cycle: counter += response_bytes − BANDWIDTH_BUDGET. Urgency adjusts per `BASE_QOS[5:4]` (URGENCY_STEP) per cycle: counter<0 → urgency increases; counter>0 → urgency decreases (saturating to 0..MAX_URGENCY). When software writes URGENCY_STEP=0 to `BASE_QOS[5:4]`, hardware treats the field as if it were 1 (effective minimum step is 1; legal SW-visible values are 1..3). | FAIL | (none) |
| NI_CFG_REGULATOR_FINAL_QOS | `QOS_MODE = Regulator`; AW/AR flit being injected | flit.hdr.qos = max(min(BASE_QOS[3:0] + urgency_level, 15), `SOCKET_QOS_EN ? SOCKET_QOS : 0`). Saturation arithmetic; clamps to 4-bit range. | FAIL | (none) |
| NI_CFG_PROBE_EN_TRANSITION | `PKT_PROBE_EN` or `TXN_PROBE_EN` CSR transitions 0→1 | Probe counters start counting from the next cycle; previous count state is preserved (not auto-cleared). To clear, software must explicitly write 0 to the count register or rely on saturating wrap-around. | FAIL | (none) |
| NI_CFG_PROBE_PKT_BYTE_COUNT | `PKT_PROBE_EN=1`; AW or AR flit injected (depends on PKT_PROBE_MODE) | `PKT_BYTE_COUNT` increments by `(awlen+1) × (1 << awsize)` for writes (PKT_PROBE_MODE=0 or 2) or `(arlen+1) × (1 << arsize)` for reads (PKT_PROBE_MODE=0 or 1). Saturating. | FAIL | (none) |
| NI_CFG_PROBE_TXN_LATENCY | `TXN_PROBE_EN=1`; B response or final R beat received | Latency = (response cycle) − (request injection cycle). Increment `TXN_BIN_<i>_COUNT` where bin `i` is the smallest index with `latency < TXN_THRESHOLD_<i>` (or final bin if larger than all thresholds). Update `TXN_MIN_LATENCY` / `TXN_MAX_LATENCY` / `TXN_TOTAL_COUNT`. | FAIL | (none) |
| NI_CFG_ERR_STATUS_RW1C | Software writes 1 to `ERR_STATUS[i]` (i ∈ {0..3} = {ecc_uncorr_err, timeout_err, route_par_err, axi_parity_err}) | Bit `[i]` and the associated saturating counter are cleared atomically on the cycle the AXI4-Lite write handshake completes. Counter map: `ECC_UNCORR_ERR_CNT` (i=0), `ERR_COUNT` (i=1), `ROUTE_PAR_ERR_CNT` (i=2), `AXI_PARITY_ERR_CNT` (i=3). The clear also deasserts the corresponding IRQ source if `IRQ_ENABLE[i]` was set. | FAIL | (none) |
| NI_CFG_LAST_ERR_INFO_CAPTURE | Any of the four `ERR_STATUS` event classes fires while no prior un-cleared error is sticky: ECC double-bit at NI sink (per `NOC_FLIT_HDR_FLIT_ECC_CHECK`), NMU outstanding-transaction timeout (per `AXI4_MST_TIMEOUT_SLVERR`), route_par drop (per `NOC_FLIT_HDR_ROUTE_PAR_CHECK`), AXI parity mismatch (per `AXI4_MST_PARITY_CHECK` / `AXI4_SLV_PARITY_CHECK`) | `LAST_ERR_INFO` register captures the offending transaction's `err_axi_id`, `err_src_id`, `err_dst_id`. Sticky semantics: first qualifying error wins; subsequent errors do NOT overwrite until software clears the corresponding `ERR_STATUS[i]` via RW1C, at which point the next qualifying event re-arms capture. Rationale: prevents losing the original triggering error during cascaded-error storms. | FAIL | (none) |
| NI_CFG_MODE_SWITCH | `set_bfm_mode(mode)` called (per `transaction_api.md`); `bfm_mode` transitions ACTIVE→PASSIVE or PASSIVE→ACTIVE | On ACTIVE→PASSIVE, all BFM-driven outputs (per `active_passive_mode.md` §Capability table) transition to their during-reset values within 1 cycle of the corresponding clock; in-flight Transaction API calls unblock with `MODE_SWITCHED_TO_PASSIVE`. On PASSIVE→ACTIVE, BFM-driven outputs return to reset-deassertion values; configuration knobs become effective on the next transaction. | FAIL | (none) |
| NI_CFG_RESPONSE_DELAY_AXI | `set_response_delay_axi(min, max)` called; next AXI response handshake on manager port pending | BFM holds AXI B/R response output by random K ∈ [min, max] `aclk_i` cycles before asserting `bvalid`/`rvalid`. Persists across transactions until reconfigured or `reset_state()`. Test-only knob; RTL counterpart has fixed pipeline timing (`CUT_AX`/`CUT_RSP` synthesis params). | RECOMMEND | (none) |
| NI_CFG_RESPONSE_DELAY_NOC | `set_response_delay_noc(min, max)` called; next NoC injection pending | BFM holds NoC `noc_*_o.valid` HIGH assertion by random K ∈ [min, max] `noc_clk_i` cycles after the flit is ready-to-inject. Persists across transactions until reconfigured or `reset_state()`. Test-only knob; no RTL counterpart. | RECOMMEND | (none) |
| NI_CFG_INJECT_ECC_ERROR | `set_inject_ecc_error(channel, kind)` called; `kind ∈ {SINGLE_BIT, DOUBLE_BIT}`; next flit injection on the specified channel | Next flit's ECC field is corrupted: SINGLE_BIT flips one ECC bit (correctable by Hsiao SECDED at receiver); DOUBLE_BIT flips two ECC bits (uncorrectable). One-shot — flag clears after the next flit injection on the specified channel. `kind=NONE` clears any pending injection. Test-only knob. | RECOMMEND | (none) |
| NI_CFG_RESPONSE_FAULT | `set_response_fault(channel, kind)` called; `channel ∈ {B, R}`; `kind ∈ {SLVERR, DECERR}`; next response handshake on the specified channel | Next B/R response handshake drives the corresponding `bresp`/`rresp` value (`SLVERR=0b10` or `DECERR=0b11`) instead of the would-be `OKAY`. One-shot — flag clears after the response is consumed. `kind=NONE` clears any pending fault. Test-only knob. | RECOMMEND | (none) |
| NI_CFG_PENDING_COUNT_ACCURACY | Software reads `PENDING_R_COUNT` (0x130) or `PENDING_W_COUNT` (0x134) | Returned value MUST equal the AXI-edge-defined outstanding count for that direction. `PENDING_R_COUNT` increments on AR handshake completion at `axi_*_i`; decrements on R-with-`rlast` handshake completion at `axi_*_i`. `PENDING_W_COUNT` increments on AW handshake completion at `axi_*_i`; decrements on B handshake completion at `axi_*_i`. Both counters are `aclk_i`-native (no CDC sync delay). Counter width per direction = `ceil(log2(MAX_TXNS+1))`; saturation at `MAX_TXNS` is impossible by construction (NMU back-pressures `awready`/`arready` before exceed). On `arst_ni` assertion: counters reset to 0 (tracker dropped per ToO §Reset entry sequencing). The CSR readback path may pipeline by ≥1 aclk cycle; the value returned reflects the cycle the read handshake samples. | FAIL | (none) |
| NI_CFG_QUIESCE_FLOW | Software writes `QUIESCE_CTRL.quiesce_req` to 1 | NMU MUST stop accepting new AW/AR handshakes by holding `axi_awready_o = axi_arready_o = 0` while `quiesce_req=1`. In-flight outstanding transactions continue to drain through normal response paths (no forced cancellation). `QUIESCE_STATUS.quiesce_idle` asserts on the cycle `(quiesce_req=1) AND (PENDING_R_COUNT=0) AND (PENDING_W_COUNT=0)` becomes true — combinational over latched aclk-domain values. NSU is **NOT** quiesced: NSU continues to service inbound `noc_req_i` requests and drive `axi_*_o` to the local AXI subordinate. Software clears `quiesce_req=0` to resume; on the same cycle, `quiesce_idle` deasserts (because the AND-term `quiesce_req=1` becomes false) and NMU resumes accepting AW/AR on the next cycle. Per ToO §"Software quiesce flow". | FAIL | (none) |
| NI_CFG_QUIESCE_LIVENESS | `quiesce_req=1` held continuously for ≥ `MAX_TXNS × TXN_TIMEOUT` `aclk_i` cycles | `quiesce_idle` MUST eventually assert. Worst-case drain bound: in the pathological case where every outstanding transaction's response is permanently absent, each tracker entry resolves at `TXN_TIMEOUT` cycles via the timeout-induced SLVERR path (per `AXI4_MST_TIMEOUT_SLVERR`); total bound = `MAX_TXNS × TXN_TIMEOUT` aclk cycles. Software polling timeout SHOULD be set ≥ this bound to avoid spurious hang declarations. Default `MAX_TXNS=32`, `TXN_TIMEOUT=10 000` → 320 000 aclk cycles ≈ 320 µs @ 1 GHz. FPV obligation: per `dv/plan.md` outstanding-timeout liveness item — the same proof obligation that covers `AXI4_MST_TIMEOUT_SLVERR` extends to cover this drain bound. | FAIL | (none) |
| NI_CFG_EXCLUSIVE_CLEAR_RACE | Software writes `1` to `EXCLUSIVE_MONITOR_CTRL.clear_all` (0x144) in the same `aclk_i` cycle as one of: (a) NSU performing Exclusive AW match check; (b) NSU allocating new entry on Exclusive AR; (c) NSU invalidating an entry due to overlapping normal write per `AXI4_EXCLUSIVE_OVERLAP_INVALIDATE` | Race resolution: the `aclk_i` edge that completes the CSR-write handshake defines the "clear epoch boundary". On that edge: (a) AW match check uses **pre-clear** monitor state (the Exclusive AW that arrived in the same cycle proceeds against the entry that was alive at start-of-cycle; if matched, EXOKAY; entry then cleared); (b) Exclusive AR allocation occurs **post-clear** (the new entry is allocated from a cleared table and survives the clear); (c) overlap-invalidate is idempotent — both events independently invalidate the same entry. After the clear epoch, the `clear_all` bit self-clears on the next `aclk_i` edge (latency = 1 cycle); subsequent reads return 0. | FAIL | (none) |
| NI_CFG_EXCLUSIVE_OCCUPANCY_ACCURACY | Software reads `EXCLUSIVE_MONITOR_STATUS.occupancy` (0x148) | Returned value MUST equal the live count of NSU Exclusive Monitor entries currently in `ALLOCATED` state. Field width = `ceil(log2(EXCLUSIVE_MONITOR_DEPTH+1))`; max value = `EXCLUSIVE_MONITOR_DEPTH`. `aclk_i`-native; same domain as the monitor itself (NSU). On `arst_ni` assertion: monitor cleared → occupancy=0. Pipelining same as `NI_CFG_PENDING_COUNT_ACCURACY` — value reflects the cycle the read handshake samples. | FAIL | (none) |

## Interrupt assertion

This sub-section formalises `irq_o` behaviour. The interrupt is the sole sideband mechanism by which the NI surfaces error events to the system; AXI-rresp-based propagation is reserved for end-to-end / timeout-driven SLVERR per `AXI4_MST_TIMEOUT_SLVERR` (per ToO §ECC rationale).

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_IRQ_LEVEL | At any cycle of `aclk_i` while `arst_ni` is deasserted | `irq_o` MUST equal the bitwise-OR of `(ERR_STATUS[i] AND IRQ_ENABLE[i])` over `i ∈ {0..3}`. Level-sensitive: stays HIGH while any unmasked `ERR_STATUS` bit is set; deasserts on the cycle software RW1C clears the last set+enabled bit (per `NI_CFG_ERR_STATUS_RW1C`). NoC-domain error sources (route_par drop, flit_ecc uncorrectable detected at NoC-side sink) reach `ERR_STATUS` via the existing CSR-file CDC sync path; no separate interrupt CDC is introduced. During reset (`arst_ni` LOW), `ERR_STATUS` and `IRQ_ENABLE` reset to 0 by construction, so `irq_o = 0`. | FAIL | (none) |
