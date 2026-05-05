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

A rule with role `SLV` referencing `AW` applies at the BFM's slave-side AXI port (`axi_in_*` for NMU's manager port acting as slave to local AXI master, `axi_out_*` for NSU's subordinate port). A rule with role `MST` referencing `AW` applies at the BFM's master-side AXI port. NoC `<PROTO>_MST_*` rules apply to BFM-driven NoC outputs (`REQ_OUT` and `RSP_OUT`); `<PROTO>_SLV_*` rules apply to BFM-observed NoC inputs (`REQ_IN` and `RSP_IN`).

## Reset rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_RST_OUTPUTS_LOW_AXI | `arst_ni` is asserted | All NI-driven AXI outputs (axi_in_rsp_o.*valid, axi_out_req_o.*valid) must be at their during-reset values per pin_level_reset.md. | FAIL | (none) |
| NI_RST_OUTPUTS_LOW_NOC | `noc_rst_ni` is asserted | All NI-driven NoC outputs (noc_req_o.valid, noc_rsp_o.valid) and ready-back signals (noc_req_i.ready, noc_rsp_i.ready) must be 0. | FAIL | (none) |
| NI_RST_DURATION_AXI | `arst_ni` pulse begins | Held LOW for ≥ 16 `aclk_i` cycles. Shorter pulses leave the AXI side in undefined state. | FAIL | (none) |
| NI_RST_DURATION_NOC | `noc_rst_ni` pulse begins | Held LOW for ≥ 16 `noc_clk_i` cycles. | FAIL | (none) |
| NI_RST_PARTIAL | One reset asserted, the other not | NI may operate in partial state; cross-domain in-flight transactions will not complete. Integrator should ensure the two resets reach a consistent state by power-on completion. | RECOMMEND | (none) |

## CDC rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_CDC_AXI_TO_NOC_FIFO | AXI ingress (NMU AW/W/AR or NSU B/R reception) crosses to NoC injection | NI uses an async FIFO (gray-counter pointer + 2FF sync) to bridge `aclk_i` → `noc_clk_i`. FIFO depth ≥ 16 entries (default; sized to absorb 2× max round-trip at slowest aclk:noc_clk ratio plus 2 entries for synchroniser pipeline depth). TODO(designer): replace constant with formula `2 × max_round_trip_cycles × max(aclk_period, noc_clk_period) / min(aclk_period, noc_clk_period) + 2` (no issue yet — current 16-entry default is conservative for ratios in [0.1, 10]; formula deferred until ratio range is locked). | FAIL | (none) |
| NI_CDC_NOC_TO_AXI_FIFO | NoC ingress (NMU response reception or NSU request reception) crosses to AXI egress | Mirror of above; `noc_clk_i` → `aclk_i` async FIFO. | FAIL | (none) |
| NI_CDC_NO_COMBINATIONAL_PATH | Any wire path | No combinational path crosses the AXI ↔ NoC clock boundary inside NI. All cross-domain signals are FIFO'd or 2FF-synchronized. | FAIL | (none) |

## AXI4 host-side rules

(Apply to both `axi_in_*` AXI manager port and `axi_out_*` AXI subordinate port unless noted.)

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
| AXI4_MST_W_WECC_GEN | NMU injects a W flit on `noc_req_o` | SECDED ECC is computed over WDATA per 64-bit granule (8 ECC bits per granule, 4 granules per 256-bit DATA_WIDTH = 32 bits ECC total) and placed in flit `wecc[31:0]`. | FAIL | (none) |

### B channel

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4_SLV_B_BVALID_STABLE | BVALID rises HIGH | BVALID must remain HIGH until BREADY observed HIGH on a rising `aclk_i` edge. | FAIL | AXI4_ERRS_BVALID_STABLE (unverified) |
| AXI4_SLV_B_BID_STABLE | BVALID is HIGH | BID must not change between BVALID rise and BREADY observation. | FAIL | AXI4_ERRS_BID_STABLE (unverified) |
| AXI4_SLV_B_BRESP_STABLE | BVALID is HIGH | BRESP must not change. | FAIL | AXI4_ERRS_BRESP_STABLE (unverified) |
| AXI4_SLV_B_BUSER_STABLE | BVALID is HIGH | BUSER must not change. | FAIL | AXI4_ERRS_BUSER_STABLE (unverified) |
| AXI4_SLV_B_BRESP_VALUES | BVALID + BREADY handshake | BRESP ∈ {OKAY=2'b00, SLVERR=2'b10, DECERR=2'b11}. EXOKAY (2'b01) is reserved for exclusive access and not used by this NI. | FAIL | AXI4_ERRS_BRESP_VALUES (unverified) |
| AXI4_SLV_B_BID_MATCH | B handshake completes | BID must match an outstanding AWID for the same NMU (i.e., a write transaction was issued with this AWID and is still in flight). | FAIL | AXI4_ERRS_BID_MATCH (unverified) |
| AXI4_SLV_B_BRESP_ECC_FAIL | NMU receives a B flit with `ecc_fail=1` set in flit payload | NMU must drive `bresp=SLVERR` to AXI master, propagating the ECC uncorrectable error from the W-flit reception at NSU. | FAIL | (none) |

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
| AXI4_SLV_R_RRESP_ECC_FAIL | NMU detects ECC uncorrectable on a received R flit | NMU drives `rresp=SLVERR` on the affected beat to AXI master. Other beats of the same burst are unaffected (per-beat reporting). | FAIL | (none) |

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

## NoC flit-side rules

### NoC link valid/ready handshake

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_MST_VALID_STABLE | A NoC output (`noc_*_o.valid`) rises HIGH | Must remain HIGH until the receiver's `ready` is observed HIGH. | FAIL | (none) |
| NOC_MST_FLIT_STABLE | `noc_*_o.valid` is HIGH | `noc_*_o.flit_data[FLIT_WIDTH-1:0]` must not change between valid rise and ready observation. | FAIL | (none) |
| NOC_SLV_READY_NO_LATCH | `noc_*_i.ready` is HIGH and `noc_*_i.valid` is LOW | Ready may be HIGH before valid (legal). NI must not interpret a one-cycle ready as completing a transfer. | FAIL | (none) |
| NOC_MST_WORMHOLE_LOCK | First flit of a packet has been injected on a `noc_*_o` link (`noc_req_o` for NMU AW/W/AR; `noc_rsp_o` for NSU B/R) and the packet's `last=1` flit has not yet been accepted | NMU/NSU MUST NOT inject any flit from a different packet on the same `noc_*_o` link until the in-flight packet's `last=1` flit is accepted by the receiving router. The wormhole-lock is per-packet: single-flit packets (AW, AR, B) release the lock immediately on the cycle they are accepted; multi-flit packets (W burst, R burst) hold the lock until their final beat (`wlast`/`rlast`, reflected in flit header `last=1`) is accepted. Implementation reference: FlooNoC `hw/floo_axi_chimney.sv` injection arbiter via `floo_wormhole_arbiter.sv` (`rr_arb_tree` with `LockIn=1`). Consequence for AXI4: AR injection is blocked while a corresponding W burst is in progress on `noc_req_o`. | FAIL | (none) |

### Flit format / header invariants

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_FLIT_HDR_QOS_4BIT | Any flit injected | Header `qos` field is 4 bits ([3:0] of the QOS_WIDTH-bit slot). Generated by QoS Generator (NMU) or copied from request (NSU response). | FAIL | (none) |
| NOC_FLIT_HDR_DST_ID_VALID | Request flit injected | `dst_id` must reference an existing node in the mesh (XY coordinates within mesh bounds, or SAM rule match if `USE_ID_TABLE=1`). | FAIL | (none) |
| NOC_FLIT_HDR_ROB_IDX_UNIQUE | Multiple flits in flight from same NMU | `rob_idx` of all in-flight requests must be unique within the NMU; duplicate is RoB allocator bug. | FAIL | (none) |
| NOC_FLIT_AW_W_ORDER | AW flit and corresponding W flits | NMU injects AW flit before any of its corresponding W flits onto `noc_req_o`. (W burst is independent wormhole; ordering at NMU output port is FIFO-natural.) | FAIL | (none) |

### ECC

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NOC_ECC_W_GEN | NMU injects W flit | SECDED ECC computed over WDATA per 64-bit granule and placed in flit `wecc[31:0]`. | FAIL | (none) |
| NOC_ECC_W_CHECK | NSU receives W flit | SECDED ECC verified per granule. 1-bit error → corrected silently + log CSR. 2-bit uncorrectable → propagate to `bresp = SLVERR`, log CSR. | FAIL | (none) |
| NOC_ECC_R_GEN | NSU injects R flit | SECDED ECC computed over RDATA per 64-bit granule and placed in flit `recc[31:0]`. | FAIL | (none) |
| NOC_ECC_R_CHECK | NMU receives R flit | SECDED ECC verified per granule. Same handling as W. | FAIL | (none) |

## CSR access (AXI4-Lite) rules

The CSR access port is AXI4-Lite subordinate. AXI4-Lite is a subset of AXI4: single-beat transactions (no AWLEN/ARLEN/AWBURST/ARBURST), no AWID/ARID, fixed-size accesses. Standard AXI4-Lite STABLE / VALUES rules apply.

### CSR write channels (AW + W + B)

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_AW_AWVALID_STABLE | csr_awvalid rises HIGH | csr_awvalid must remain HIGH until csr_awready observed HIGH. | FAIL | AXI4LITE_ERRM_AWVALID_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWADDR_STABLE | csr_awvalid is HIGH | csr_awaddr must not change. | FAIL | AXI4LITE_ERRM_AWADDR_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWPROT_STABLE | csr_awvalid is HIGH | csr_awprot must not change. | FAIL | AXI4LITE_ERRM_AWPROT_STABLE (unverified) |
| AXI4LITE_SLV_AW_AWADDR_ALIGNED | csr_awvalid + csr_awready handshake | csr_awaddr must be 4-byte-aligned (lower 2 bits = 0). Misaligned writes cause `csr_bresp=SLVERR`. | FAIL | (none) |
| AXI4LITE_SLV_W_WVALID_STABLE | csr_wvalid rises HIGH | Until csr_wready observed HIGH. | FAIL | AXI4LITE_ERRM_WVALID_STABLE (unverified) |
| AXI4LITE_SLV_W_WDATA_STABLE | csr_wvalid is HIGH | csr_wdata must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_W_WSTRB_STABLE | csr_wvalid is HIGH | csr_wstrb must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_B_BVALID_STABLE | csr_bvalid rises HIGH | Until csr_bready observed HIGH. | FAIL | AXI4LITE_ERRS_BVALID_STABLE (unverified) |
| AXI4LITE_SLV_B_BRESP_VALUES | csr_bvalid + csr_bready handshake | csr_bresp ∈ {OKAY=2'b00, SLVERR=2'b10, DECERR=2'b11}. SLVERR for misaligned address or RW1C-write-to-RO-bit; DECERR for unmapped offset. | FAIL | (none) |
| AXI4LITE_SLV_XCH_W_AFTER_AW | csr_wready asserted | Corresponding csr_awvalid + csr_awready handshake must have completed (or be on the same cycle). | FAIL | (none) |
| AXI4LITE_SLV_XCH_B_AFTER_AW_AND_W | csr_bvalid asserted | Both AW and W phases of the same write must have completed. | FAIL | (none) |

### CSR read channels (AR + R)

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_AR_ARVALID_STABLE | csr_arvalid rises HIGH | Until csr_arready observed HIGH. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARADDR_STABLE | csr_arvalid is HIGH | csr_araddr must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARPROT_STABLE | csr_arvalid is HIGH | csr_arprot must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_AR_ARADDR_ALIGNED | csr_arvalid + csr_arready handshake | csr_araddr must be 4-byte-aligned. Misaligned reads cause `csr_rresp=SLVERR`. | FAIL | (none) |
| AXI4LITE_SLV_R_RVALID_STABLE | csr_rvalid rises HIGH | Until csr_rready observed HIGH. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RDATA_STABLE | csr_rvalid is HIGH | csr_rdata must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RRESP_STABLE | csr_rvalid is HIGH | csr_rresp must not change. | FAIL | (unverified) |
| AXI4LITE_SLV_R_RRESP_VALUES | csr_rvalid + csr_rready handshake | csr_rresp ∈ {OKAY, SLVERR, DECERR}. DECERR for unmapped offset. | FAIL | (none) |
| AXI4LITE_SLV_R_RLAST_NOT_REQUIRED | csr_rvalid + csr_rready handshake | AXI4-Lite reads are single-beat; no RLAST signal. (NI's CSR port omits csr_rlast.) | FAIL | (none) |
| AXI4LITE_SLV_XCH_R_AFTER_AR | csr_rvalid asserted | Corresponding csr_arvalid + csr_arready handshake must have completed. | FAIL | (none) |

### CSR address policy

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| AXI4LITE_SLV_UNMAPPED_DECERR | Read or write to a CSR offset not listed in registers.md §Register map | csr_bresp=DECERR (for write) or csr_rresp=DECERR (for read). | FAIL | (none) |
| AXI4LITE_SLV_RO_WRITE_IGNORED | Write to a Read-Only register (per registers.md Access column) | Write data is silently ignored; csr_bresp=OKAY (write succeeds at the bus level but has no effect). Software contract: don't write to RO. | RECOMMEND | (none) |
| AXI4LITE_SLV_RW1C_WRITE_BIT_LEVEL | Write to a RW1C register | For each bit position: software writes 1 → bit clears + associated counter clears (per registers.md §ERR_STATUS); software writes 0 → no effect (bit retains current state). | FAIL | (none) |

## Configuration-knob rules

| ID | Condition | Required behavior | Severity | ARM SVA equivalent |
|----|-----------|-------------------|----------|--------------------|
| NI_CFG_QOS_MODE_TRANSITION | `QOS_MODE` CSR written | New mode applies to the NEXT AW/AR flit injection; in-flight transactions retain the QoS computed at their injection time. | FAIL | (none) |
| NI_CFG_QOS_FIXED_VALUE | `QOS_MODE = Fixed`; `QOS_FIXED_VALUE` CSR written | Next AW/AR flit's `qos` header field equals `QOS_FIXED_VALUE`, regardless of AXI awqos/arqos input. | FAIL | (none) |
| NI_CFG_BANDWIDTH_LIMIT_BOUND | `BANDWIDTH_LIMIT` CSR written; `QOS_MODE = Limiter` | Limiter counter increments per request bytes, decrements per cycle by `BANDWIDTH_LIMIT`; QoS drops to `LOW_PRIORITY` when counter > `SATURATION_THRESHOLD`. Saturating arithmetic. | FAIL | (none) |
| NI_CFG_BANDWIDTH_BUDGET_BOUND | `BANDWIDTH_BUDGET` CSR written; `QOS_MODE = Regulator` | Per cycle: counter += response_bytes − BANDWIDTH_BUDGET. Urgency adjusts per `BASE_QOS[5:4]` (URGENCY_STEP) per cycle: counter<0 → urgency increases; counter>0 → urgency decreases (saturating to 0..MAX_URGENCY). | FAIL | (none) |
| NI_CFG_REGULATOR_FINAL_QOS | `QOS_MODE = Regulator`; AW/AR flit being injected | flit.hdr.qos = max(min(BASE_QOS[3:0] + urgency_level, 15), `SOCKET_QOS_EN ? SOCKET_QOS : 0`). Saturation arithmetic; clamps to 4-bit range. | FAIL | (none) |
| NI_CFG_PROBE_EN_TRANSITION | `PKT_PROBE_EN` or `TXN_PROBE_EN` CSR transitions 0→1 | Probe counters start counting from the next cycle; previous count state is preserved (not auto-cleared). To clear, software must explicitly write 0 to the count register or rely on saturating wrap-around. | FAIL | (none) |
| NI_CFG_PROBE_PKT_BYTE_COUNT | `PKT_PROBE_EN=1`; AW or AR flit injected (depends on PKT_PROBE_MODE) | `PKT_BYTE_COUNT` increments by `(awlen+1) × (1 << awsize)` for writes (PKT_PROBE_MODE=0 or 2) or `(arlen+1) × (1 << arsize)` for reads (PKT_PROBE_MODE=0 or 1). Saturating. | FAIL | (none) |
| NI_CFG_PROBE_TXN_LATENCY | `TXN_PROBE_EN=1`; B response or final R beat received | Latency = (response cycle) − (request injection cycle). Increment `TXN_BIN_<i>_COUNT` where bin `i` is the smallest index with `latency < TXN_THRESHOLD_<i>` (or final bin if larger than all thresholds). Update `TXN_MIN_LATENCY` / `TXN_MAX_LATENCY` / `TXN_TOTAL_COUNT`. | FAIL | (none) |
| NI_CFG_ERR_STATUS_RW1C | Software writes 1 to `ERR_STATUS[i]` (i ∈ {0=ecc_uncorr_err, 1=timeout_err}) | Bit `[i]` and the associated saturating counter (`ECC_UNCORR_ERR_CNT` for i=0, `ERR_COUNT` for i=1) are cleared atomically on the cycle the AXI4-Lite write handshake completes. | FAIL | (none) |
| NI_CFG_LAST_ERR_INFO_CAPTURE | NMU detects ECC uncorrectable on a received B/R flit OR NSU times out waiting for response | `LAST_ERR_INFO` register captures the offending transaction's `err_axi_id`, `err_src_id`, `err_dst_id`. **Sticky semantics: first error since last clear is captured; subsequent errors do not overwrite. Clear via `ERR_STATUS` RW1C.** *Reviewer assumption: please confirm sticky vs overwrite semantics.* | FAIL | (none) |
| NI_CFG_MODE_SWITCH | `set_bfm_mode(mode)` called (per `transaction_api.md`); `bfm_mode` transitions ACTIVE→PASSIVE or PASSIVE→ACTIVE | On ACTIVE→PASSIVE, all BFM-driven outputs (per `active_passive_mode.md` §Capability table) transition to their during-reset values within 1 cycle of the corresponding clock; in-flight Transaction API calls unblock with `MODE_SWITCHED_TO_PASSIVE`. On PASSIVE→ACTIVE, BFM-driven outputs return to reset-deassertion values; configuration knobs become effective on the next transaction. | FAIL | (none) |
| NI_CFG_RESPONSE_DELAY_AXI | `set_response_delay_axi(min, max)` called; next AXI response handshake on manager port pending | BFM holds AXI B/R response output by random K ∈ [min, max] `aclk_i` cycles before asserting `bvalid`/`rvalid`. Persists across transactions until reconfigured or `reset_state()`. Test-only knob; RTL counterpart has fixed pipeline timing (`CUT_AX`/`CUT_RSP` synthesis params). | RECOMMEND | (none) |
| NI_CFG_RESPONSE_DELAY_NOC | `set_response_delay_noc(min, max)` called; next NoC injection pending | BFM holds NoC `noc_*_o.valid` HIGH assertion by random K ∈ [min, max] `noc_clk_i` cycles after the flit is ready-to-inject. Persists across transactions until reconfigured or `reset_state()`. Test-only knob; no RTL counterpart. | RECOMMEND | (none) |
| NI_CFG_INJECT_ECC_ERROR | `set_inject_ecc_error(channel, kind)` called; `kind ∈ {SINGLE_BIT, DOUBLE_BIT}`; next flit injection on the specified channel | Next flit's ECC field is corrupted: SINGLE_BIT flips one ECC bit (correctable by Hsiao SECDED at receiver); DOUBLE_BIT flips two ECC bits (uncorrectable). One-shot — flag clears after the next flit injection on the specified channel. `kind=NONE` clears any pending injection. Test-only knob. | RECOMMEND | (none) |
| NI_CFG_RESPONSE_FAULT | `set_response_fault(channel, kind)` called; `channel ∈ {B, R}`; `kind ∈ {SLVERR, DECERR}`; next response handshake on the specified channel | Next B/R response handshake drives the corresponding `bresp`/`rresp` value (`SLVERR=0b10` or `DECERR=0b11`) instead of the would-be `OKAY`. One-shot — flag clears after the response is consumed. `kind=NONE` clears any pending fault. Test-only knob. | RECOMMEND | (none) |
