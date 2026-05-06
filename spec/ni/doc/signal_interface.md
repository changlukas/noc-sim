# Signal Interface

**Protocols:**
- AXI side: AXI4 (ARM IHI 0022)
- NoC side: custom flit-based packet protocol. Flit width `FLIT_WIDTH` bits (default 408 in v0.4.0). Header `HEADER_WIDTH` bits (default 56 in v0.4.0). See `02_flit.md` in noc-sim source repo for flit format details.
- CSR side: AXI4-Lite (subset of AXI4) for software-visible configuration and monitoring registers.

**Role:** Both manager and subordinate. NMU (Network Manager Unit) acts as AXI subordinate on the host side (receives AXI requests from local master) and initiates flits on the NoC. NSU (Network Subordinate Unit) receives flits from the NoC and acts as AXI manager on the host side (drives AXI requests to local slave).

**BFM perspective:** Direction in the tables below is from the BFM out to the connected fabric / IP. The BFM has four external interfaces: AXI4 manager port (NMU side; receives AXI from local master), AXI4 subordinate port (NSU side; drives AXI to local slave), NoC link pair (req + rsp, bidirectional), AXI4-Lite CSR access port (software configuration access).

## Naming convention

Lowercase signals with `_i` / `_o` direction suffixes and `_ni` for active-low resets. AXI4 channel signals use the standard `awvalid` / `awready` / `awaddr` lowercase form. Flit-level signals use `noc_*` prefix. Width parameters in brackets denote signal vector width.

## Wire table

The Wire table excludes the protocol clocks and resets (listed in §Protocol clock and reset). LINT-BFM-001 (wire-set parity vs `pin_level_reset.md`) applies to this table only.

### AXI4 Manager port (NMU side; receives AXI from local AXI master)

#### AW channel (write address)

| Signal | Direction | Width | Active | Sample edge | Reset value (see pin_level_reset.md) | Optional in protocol | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|--------------------------------------|----------------------|--------------|-------|
| `axi_in_awvalid` | input | 1 | H | pos aclk | §AXI in AW row 1 | no | yes | Master signals AW phase request |
| `axi_in_awready` | output | 1 | H | pos aclk | §AXI in AW row 2 | no | yes | NMU signals AW phase accept |
| `axi_in_awid` | input | IN_ID_WIDTH | — | pos aclk | §AXI in AW row 3 | no | yes | Transaction ID |
| `axi_in_awaddr` | input | ADDR_WIDTH | — | pos aclk | §AXI in AW row 4 | no | yes | Target address |
| `axi_in_awlen` | input | 8 | — | pos aclk | §AXI in AW row 5 | no | yes | Burst length minus 1 (0..255) |
| `axi_in_awsize` | input | 3 | — | pos aclk | §AXI in AW row 6 | no | yes | Beat size (log2 bytes per beat) |
| `axi_in_awburst` | input | 2 | — | pos aclk | §AXI in AW row 7 | no | yes | Burst type: 00=FIXED, 01=INCR, 10=WRAP, 11=reserved |
| `axi_in_awcache` | input | 4 | — | pos aclk | §AXI in AW row 8 | yes | yes | Cache attributes; recorded but not enforced by NMU |
| `axi_in_awprot` | input | 3 | — | pos aclk | §AXI in AW row 9 | no | yes | Protection attributes; recorded but not enforced |
| `axi_in_awqos` | input | 4 | — | pos aclk | §AXI in AW row 10 | yes | yes | QoS hint; consumed by QoSGen in Bypass mode |
| `axi_in_awuser` | input | USER_WIDTH | — | pos aclk | §AXI in AW row 11 | yes | yes | User signal; passed through to flit user payload |
| `axi_in_awatop` | input | 6 | — | pos aclk | §AXI in AW row 12 | yes | yes (sample-only) | AXI5 atomic operation code; sampled and recorded by monitor only — BFM terminates ATOPs with `bresp=SLVERR` per ToO §ATOPs scope (out-of-scope for stimulus generation in this revision) |

#### W channel (write data)

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `axi_in_wvalid` | input | 1 | H | pos aclk | §AXI in W row 1 | no | yes |  |
| `axi_in_wready` | output | 1 | H | pos aclk | §AXI in W row 2 | no | yes |  |
| `axi_in_wdata` | input | DATA_WIDTH | — | pos aclk | §AXI in W row 3 | no | yes |  |
| `axi_in_wstrb` | input | DATA_WIDTH/8 | — | pos aclk | §AXI in W row 4 | no | yes | Byte strobes |
| `axi_in_wlast` | input | 1 | H | pos aclk | §AXI in W row 5 | no | yes | Asserted on the last beat of the burst |
| `axi_in_wuser` | input | USER_WIDTH | — | pos aclk | §AXI in W row 6 | yes | yes |  |

#### B channel (write response)

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `axi_in_bvalid` | output | 1 | H | pos aclk | §AXI in B row 1 | no | yes |  |
| `axi_in_bready` | input | 1 | H | pos aclk | §AXI in B row 2 | no | yes |  |
| `axi_in_bid` | output | IN_ID_WIDTH | — | pos aclk | §AXI in B row 3 | no | yes | Matches awid of completed transaction |
| `axi_in_bresp` | output | 2 | — | pos aclk | §AXI in B row 4 | no | yes | 00=OKAY, 01=EXOKAY (unused), 10=SLVERR, 11=DECERR |
| `axi_in_buser` | output | USER_WIDTH | — | pos aclk | §AXI in B row 5 | yes | yes |  |

#### AR channel (read address)

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `axi_in_arvalid` | input | 1 | H | pos aclk | §AXI in AR row 1 | no | yes |  |
| `axi_in_arready` | output | 1 | H | pos aclk | §AXI in AR row 2 | no | yes |  |
| `axi_in_arid` | input | IN_ID_WIDTH | — | pos aclk | §AXI in AR row 3 | no | yes |  |
| `axi_in_araddr` | input | ADDR_WIDTH | — | pos aclk | §AXI in AR row 4 | no | yes |  |
| `axi_in_arlen` | input | 8 | — | pos aclk | §AXI in AR row 5 | no | yes |  |
| `axi_in_arsize` | input | 3 | — | pos aclk | §AXI in AR row 6 | no | yes |  |
| `axi_in_arburst` | input | 2 | — | pos aclk | §AXI in AR row 7 | no | yes |  |
| `axi_in_arcache` | input | 4 | — | pos aclk | §AXI in AR row 8 | yes | yes |  |
| `axi_in_arprot` | input | 3 | — | pos aclk | §AXI in AR row 9 | no | yes |  |
| `axi_in_arqos` | input | 4 | — | pos aclk | §AXI in AR row 10 | yes | yes |  |
| `axi_in_aruser` | input | USER_WIDTH | — | pos aclk | §AXI in AR row 11 | yes | yes |  |

#### R channel (read data)

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `axi_in_rvalid` | output | 1 | H | pos aclk | §AXI in R row 1 | no | yes |  |
| `axi_in_rready` | input | 1 | H | pos aclk | §AXI in R row 2 | no | yes |  |
| `axi_in_rid` | output | IN_ID_WIDTH | — | pos aclk | §AXI in R row 3 | no | yes |  |
| `axi_in_rdata` | output | DATA_WIDTH | — | pos aclk | §AXI in R row 4 | no | yes |  |
| `axi_in_rresp` | output | 2 | — | pos aclk | §AXI in R row 5 | no | yes |  |
| `axi_in_rlast` | output | 1 | H | pos aclk | §AXI in R row 6 | no | yes |  |
| `axi_in_ruser` | output | USER_WIDTH | — | pos aclk | §AXI in R row 7 | yes | yes |  |

### AXI4 Subordinate port (NSU side; drives AXI to local AXI slave)

The subordinate port mirrors the manager port but with all directions reversed. Field semantics are identical to AXI4 spec; only the BFM-perspective direction differs.

#### AW channel

| Signal | Direction | Width | Reset | Notes |
|--------|-----------|-------|-------|-------|
| `axi_out_awvalid` | output | 1 | §AXI out AW row 1 | NSU drives AW phase to local slave |
| `axi_out_awready` | input | 1 | §AXI out AW row 2 | Local slave signals AW accept |
| `axi_out_awid` | output | OUT_ID_WIDTH | §AXI out AW row 3 |  |
| `axi_out_awaddr` | output | ADDR_WIDTH | §AXI out AW row 4 |  |
| `axi_out_awlen` | output | 8 | §AXI out AW row 5 |  |
| `axi_out_awsize` | output | 3 | §AXI out AW row 6 |  |
| `axi_out_awburst` | output | 2 | §AXI out AW row 7 |  |
| `axi_out_awcache` | output | 4 | §AXI out AW row 8 |  |
| `axi_out_awprot` | output | 3 | §AXI out AW row 9 |  |
| `axi_out_awqos` | output | 4 | §AXI out AW row 10 | Forwarded from inbound flit qos field |
| `axi_out_awuser` | output | USER_WIDTH | §AXI out AW row 11 |  |

#### W / B / AR / R channels

Same shape as the manager port; all `_o` ↔ `_i` direction flipped (W: outputs from BFM driving wdata/wstrb/wlast; B: BFM samples bvalid/bid/bresp; AR: BFM drives ARVALID with full address; R: BFM samples rvalid + payload).

For brevity the wire-level expansion follows the manager port exactly (same fields, same widths). See pin_level_reset.md §AXI out for per-wire reset values.

### NoC Request link

All `valid` / `ready` / `flit` signals are **per-VC arrays** of width `NUM_VC` (default 1).

When `NUM_VC=1` the array degenerates to a single line per signal (synthesis flattens).

When `NUM_VC>1` each VC has independent valid/ready/flit. NMU at injection side selects the VC for each flit per `theory_of_operation.md` §"NMU VC selection policy" (Hybrid R/W × QoS mapping).

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `noc_req_o_valid[NUM_VC-1:0]` | output | NUM_VC | H | pos noc_clk | §NoC req out row 1 | no | yes | Per-VC valid. NMU asserts on the chosen VC to inject a flit. |
| `noc_req_o_ready[NUM_VC-1:0]` | input | NUM_VC | H | pos noc_clk | §NoC req out row 2 | no | yes | Per-VC ready. Router asserts per-VC when its corresponding input FIFO has space. |
| `noc_req_o_flit[NUM_VC-1:0][FLIT_WIDTH-1:0]` | output | NUM_VC × FLIT_WIDTH | — | pos noc_clk | §NoC req out row 3 | no | yes | Per-VC flit data. Valid when `noc_req_o_valid[v]=1`. |
| `noc_req_i_valid[NUM_VC-1:0]` | input | NUM_VC | H | pos noc_clk | §NoC req in row 1 | no | yes | Per-VC valid. Router signals incoming flit on a specific VC. |
| `noc_req_i_ready[NUM_VC-1:0]` | output | NUM_VC | H | pos noc_clk | §NoC req in row 2 | no | yes | Per-VC ready. NSU asserts per-VC when its corresponding input FIFO has space. |
| `noc_req_i_flit[NUM_VC-1:0][FLIT_WIDTH-1:0]` | input | NUM_VC × FLIT_WIDTH | — | pos noc_clk | §NoC req in row 3 | no | yes | Per-VC inbound flit payload. |

### NoC Response link

Same shape as Request link, with `noc_rsp_*` prefix.

- NSU injects via `noc_rsp_o_*`
- NMU receives via `noc_rsp_i_*`

| Signal | Direction | Width | Reset | Notes |
|--------|-----------|-------|-------|-------|
| `noc_rsp_o_valid[NUM_VC-1:0]` | output | NUM_VC | §NoC rsp out row 1 | Per-VC valid. NSU asserts to inject response flit on chosen VC. |
| `noc_rsp_o_ready[NUM_VC-1:0]` | input | NUM_VC | §NoC rsp out row 2 | Per-VC ready from router. |
| `noc_rsp_o_flit[NUM_VC-1:0][FLIT_WIDTH-1:0]` | output | NUM_VC × FLIT_WIDTH | §NoC rsp out row 3 | Per-VC flit data. |
| `noc_rsp_i_valid[NUM_VC-1:0]` | input | NUM_VC | §NoC rsp in row 1 | Per-VC valid from router. |
| `noc_rsp_i_ready[NUM_VC-1:0]` | output | NUM_VC | §NoC rsp in row 2 | Per-VC ready. NMU asserts when RoB has slot for the corresponding VC. |
| `noc_rsp_i_flit[NUM_VC-1:0][FLIT_WIDTH-1:0]` | input | NUM_VC × FLIT_WIDTH | §NoC rsp in row 3 | Per-VC inbound flit payload. |

### NoC credit signals — Optional, present only when `FLOW_CONTROL == CREDIT_BASED`

When `FLOW_CONTROL = CREDIT_BASED`, the NoC link uses per-VC credit accounting in lieu of `valid`/`ready` handshake. The following signals are added per NoC link direction. All credit signals have width `NUM_VC` (one credit bit per VC). When `FLOW_CONTROL = VALID_READY`, these signals are absent.

**Per-VC credit return signals:**

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `noc_req_o_credit_i[NUM_VC-1:0]` | input | NUM_VC | H | pos noc_clk | §NoC credit row 1 | conditional (FLOW_CONTROL=CREDIT_BASED) | yes | Per-VC credit return from downstream router. One bit asserts → upstream NMU credit counter for that VC increments by 1. |
| `noc_req_i_credit_o[NUM_VC-1:0]` | output | NUM_VC | H | pos noc_clk | §NoC credit row 2 | conditional | yes | Per-VC credit return generated by NSU input buffer pop. One bit assert → downstream router increments its credit. |
| `noc_rsp_o_credit_i[NUM_VC-1:0]` | input | NUM_VC | H | pos noc_clk | §NoC credit row 3 | conditional | yes | Per-VC credit return for response link. |
| `noc_rsp_i_credit_o[NUM_VC-1:0]` | output | NUM_VC | H | pos noc_clk | §NoC credit row 4 | conditional | yes | Per-VC credit return for NMU response input buffer. |

**Credit startup handshake signals (per AMD §Credit-Based Flow Control):**

After reset, source-credit counters initialise to 0 per VC. Both ends must complete a bidirectional ready handshake before credit exchange begins. These signals carry that handshake.

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `noc_req_o_credit_init_ready_o` | output | 1 | H | pos noc_clk | §NoC credit-init row 1 | conditional (FLOW_CONTROL=CREDIT_BASED) | yes | NMU asserts after `noc_rst_ni` deassertion when ready to start credit exchange on `noc_req_o`. |
| `noc_req_o_credit_init_ready_i` | input | 1 | H | pos noc_clk | §NoC credit-init row 2 | conditional | yes | Router asserts when ready to receive on `noc_req` link. Credit exchange begins on the cycle both `*_o` and `*_i` are HIGH. |
| `noc_rsp_o_credit_init_ready_o` | output | 1 | H | pos noc_clk | §NoC credit-init row 3 | conditional | yes | NSU asserts after `noc_rst_ni` deassertion when ready to start credit exchange on `noc_rsp_o`. |
| `noc_rsp_o_credit_init_ready_i` | input | 1 | H | pos noc_clk | §NoC credit-init row 4 | conditional | yes | Router asserts when ready to receive on `noc_rsp` link. |

**Behaviour summary in `CREDIT_BASED` mode:**

- `noc_*_o.ready` / `noc_*_i.ready` remain part of the link signal but back-pressure is governed by credits, not by per-cycle ready handshake.
- Source-credit counters initialise to 0 per VC at reset deassertion.
- Credit exchange begins only after both ends raise their respective `*_credit_init_ready_*` signals (bidirectional handshake).
- Once exchange begins, credit counters are seeded with `INPUT_BUFFER_DEPTH / NUM_VC` per VC (where `INPUT_BUFFER_DEPTH` is the receiver's per-link buffer depth, a router-side parameter the integrator must communicate).
- Credit return latency is `CREDIT_DELAY` cycles (router-side parameter, default 1).
- Credit starvation (no credit return for `CREDIT_TIMEOUT` cycles, default 10000) triggers `ERR_STATUS[1]` `timeout_err` per `protocol_rules.md` `NI_CFG_ERR_STATUS_RW1C`.

**Behaviour in `VALID_READY` mode (default):**

- Standard per-VC `valid` / `ready` handshake per the NoC Request / Response link tables applies.
- Credit signals and credit-init-ready signals are absent (do not appear in the wire list).

### CSR access port (AXI4-Lite subordinate)

A dedicated AXI4-Lite subordinate port for software access to NMU/NSU CSR file. Width assumptions: 32-bit CSR_ADDR_WIDTH=12 (4KB region accommodates ~32 registers per `registers.md`); 32-bit CSR_DATA_WIDTH=32.

| Signal | Direction | Width | Active | Reset value | Notes |
|--------|-----------|-------|--------|-------------|-------|
| `csr_awvalid` | input | 1 | H | §CSR row 1 |  |
| `csr_awready` | output | 1 | H | §CSR row 2 |  |
| `csr_awaddr` | input | 12 | — | §CSR row 3 | 4KB CSR window |
| `csr_awprot` | input | 3 | — | §CSR row 4 |  |
| `csr_wvalid` | input | 1 | H | §CSR row 5 |  |
| `csr_wready` | output | 1 | H | §CSR row 6 |  |
| `csr_wdata` | input | 32 | — | §CSR row 7 |  |
| `csr_wstrb` | input | 4 | — | §CSR row 8 |  |
| `csr_bvalid` | output | 1 | H | §CSR row 9 |  |
| `csr_bready` | input | 1 | H | §CSR row 10 |  |
| `csr_bresp` | output | 2 | — | §CSR row 11 |  |
| `csr_arvalid` | input | 1 | H | §CSR row 12 |  |
| `csr_arready` | output | 1 | H | §CSR row 13 |  |
| `csr_araddr` | input | 12 | — | §CSR row 14 |  |
| `csr_arprot` | input | 3 | — | §CSR row 15 |  |
| `csr_rvalid` | output | 1 | H | §CSR row 16 |  |
| `csr_rready` | input | 1 | H | §CSR row 17 |  |
| `csr_rdata` | output | 32 | — | §CSR row 18 |  |
| `csr_rresp` | output | 2 | — | §CSR row 19 |  |

### Sideband / configuration

| Signal | Direction | Width | Reset value | Notes |
|--------|-----------|-------|-------------|-------|
| `id_i` | input | X_WIDTH+Y_WIDTH (default 8) | §Sideband row 1 | This NI's Node ID. Strap-style. Sampled in noc_clk domain. |
| `port_id_i` | input | PORT_ID_WIDTH (default 2) | §Sideband row 2 | This NI's local-port index at its attached router (0..3 for default 4-LOCAL-port router). Strap-style. Sampled in noc_clk domain. Used by NMU to populate request flit `port_id` (per `protocol_rules.md` `NOC_FLIT_HDR_PORT_ID_VALID`). Used by NSU to identify response routing back to originating NMU's port. Modifying after `noc_rst_ni` deassertion is undefined. |
| `route_table_i` | input | NUM_SAM_RULES × sizeof(sam_rule_t) | §Sideband row 3 | Routing table. Only valid when `USE_ID_TABLE=1`. Strap-style. Modifying after `noc_rst_ni` deassertion is undefined. For runtime route reconfiguration use the CSR path with quiesce-then-modify discipline. |

### Optional AXI parity sideband — present only when `ENABLE_AXI_PARITY = true`

Per AMD §Data Integrity, AXI-side data and address parity is an **optional integrator-enabled** integrity layer at the host/slave AXI boundaries. Independent of the always-on whole-flit `flit_ecc` and `route_par` inside the NoC fabric. When `ENABLE_AXI_PARITY = false` (default), these signals are absent.

Coverage:
- 1-bit even parity per byte of data (data parity)
- 1-bit even parity per address word (address parity)

**AXI Manager port (axi_in_*) parity inputs (when `EN_MGR_PORT=1` and `ENABLE_AXI_PARITY=1`):**

| Signal | Direction | Width | Active | Sample edge | Reset value | Notes |
|--------|-----------|-------|--------|-------------|-------------|-------|
| `axi_in_awaddr_par_i` | input | 1 | H | pos aclk | §AXI parity row 1 | Even parity over `axi_in_awaddr`. Sampled when `axi_in_awvalid=1`. |
| `axi_in_araddr_par_i` | input | 1 | H | pos aclk | §AXI parity row 2 | Even parity over `axi_in_araddr`. Sampled when `axi_in_arvalid=1`. |
| `axi_in_wdata_par_i[DATA_WIDTH/8-1:0]` | input | DATA_WIDTH/8 | H | pos aclk | §AXI parity row 3 | Per-byte even parity over `axi_in_wdata`. Sampled when `axi_in_wvalid=1`. |

**AXI Subordinate port (axi_out_*) parity outputs (when `EN_SBR_PORT=1` and `ENABLE_AXI_PARITY=1`):**

| Signal | Direction | Width | Active | Sample edge | Reset value | Notes |
|--------|-----------|-------|--------|-------------|-------------|-------|
| `axi_out_awaddr_par_o` | output | 1 | H | pos aclk | §AXI parity row 4 | Even parity over `axi_out_awaddr` (NSU-generated). |
| `axi_out_araddr_par_o` | output | 1 | H | pos aclk | §AXI parity row 5 | Even parity over `axi_out_araddr` (NSU-generated). |
| `axi_out_wdata_par_o[DATA_WIDTH/8-1:0]` | output | DATA_WIDTH/8 | H | pos aclk | §AXI parity row 6 | Per-byte even parity over `axi_out_wdata` (NSU-generated). |
| `axi_out_rdata_par_i[DATA_WIDTH/8-1:0]` | input | DATA_WIDTH/8 | H | pos aclk | §AXI parity row 7 | Per-byte even parity over `axi_out_rdata` (from local slave). NSU verifies on R reception. |

**Behaviour:**

- NMU verifies `axi_in_*_par_i` on each AW/AR/W handshake. Mismatch triggers fatal error logged to `ERR_STATUS` parity-error bits (per D13 grouped error logging).
- NSU generates `axi_out_*_par_o` from regenerated WSTRB-aligned data after upsize/downsize.
- NSU verifies `axi_out_rdata_par_i` from local slave. Mismatch triggers `rresp=SLVERR` on the affected R beat plus error log entry.
- Parity is verified at AXI boundary only. The protected AXI signal is propagated through NMU/NSU intermediates without re-checking. Once data is on the NoC, `flit_ecc` (whole-flit SECDED) takes over.

## Protocol clock and reset

| Signal | Direction | Description |
|--------|-----------|-------------|
| `aclk_i` | input | AXI side clock; samples all `axi_in_*`, `axi_out_*`, `csr_*` on rising edge |
| `arst_ni` | input | AXI side active-low reset; async assertion / sync deassertion to `aclk_i`; hold ≥ 16 `aclk_i` cycles |
| `noc_clk_i` | input | NoC fabric clock; samples all `noc_*`, `id_i`, `route_table_i` on rising edge |
| `noc_rst_ni` | input | NoC side active-low reset; async assertion / sync deassertion to `noc_clk_i`; hold ≥ 16 `noc_clk_i` cycles |

NI internal async FIFOs (gray-counter pointer + 2FF synchronizer) bridge AXI ↔ NoC at the boundary inside both NMU and NSU. Cross-domain signals never propagate combinationally.

## Parameters

| Name | Type | Default | Constraint | Description |
|------|------|---------|------------|-------------|
| `ADDR_WIDTH` | int | 64 | 32 ≤ x ≤ 64 | AXI address width on host side |
| `DATA_WIDTH` | int | 256 | 64 / 128 / 256 / 512 | AXI data width. **32-bit not supported** at NMU/NSU per AMD pg313 §AXI Support and Restrictions. master width < NoC width → narrow transfer (per AxCache[1]). master width > NoC width → downsize at NMU. |
| `USER_WIDTH` | int | 8 | 1 ≤ x ≤ 32 | AXI user signal width |
| `IN_ID_WIDTH` | int | 8 | 1 ≤ x ≤ 16 | AXI manager (incoming) txnID width |
| `OUT_ID_WIDTH` | int | 8 | 1 ≤ x ≤ 16 | AXI subordinate (outgoing) txnID width |
| `EN_SBR_PORT` | bool | true | EN_SBR_PORT \|\| EN_MGR_PORT | Enable NSU |
| `EN_MGR_PORT` | bool | true | EN_SBR_PORT \|\| EN_MGR_PORT | Enable NMU |
| `MAX_TXNS` | int | 32 | power-of-2 | HW ceiling on outstanding transactions |
| `MAX_UNIQUE_IDS` | int | 1 | 1 ≤ x ≤ MAX_TXNS | Number of unique downstream txnIDs |
| `MAX_TXNS_PER_ID` | int | 32 | 1 ≤ x ≤ MAX_TXNS | Outstanding count per unique ID |
| `B_ROB_TYPE` | enum {NormalRoB, SimpleRoB, NoRoB} | NoRoB | — | B response RoB mode |
| `B_ROB_SIZE` | int | 0 | 0 if NoRoB, else 1 ≤ x ≤ MAX_TXNS | B RoB depth |
| `R_ROB_TYPE` | enum | NoRoB | — | R response RoB mode |
| `R_ROB_SIZE` | int | 0 | same as B_ROB_SIZE | R RoB depth |
| `CUT_AX` | bool | false | — | AW/AR spill register |
| `CUT_RSP` | bool | false | — | Response spill register |
| `ROUTE_ALGO` | enum {XYRouting, SourceRouting, IDRouting} | XYRouting | — | Routing algorithm |
| `USE_ID_TABLE` | bool | false | — | Use SAM table for dst_id derivation |
| `XY_ADDR_OFFSET_X` | int | 32 | 0 ≤ x ≤ ADDR_WIDTH-X_WIDTH | X coordinate bit offset in AXI address |
| `XY_ADDR_OFFSET_Y` | int | 36 | 0 ≤ x ≤ ADDR_WIDTH-Y_WIDTH | Y coordinate bit offset |
| `NUM_SAM_RULES` | int | 0 | 0 ≤ x ≤ 64 | SAM rules count when USE_ID_TABLE=1 |
| `FLIT_WIDTH` | derived | 408 | derived = HEADER_WIDTH + PAYLOAD_WIDTH | Flit total width (header + payload). v0.4.0 default 408. v0.3.0 was 400. |
| `HEADER_WIDTH` | derived | 56 | derived = Σ(all header field params) | Flit header width. v0.4.0 default 56. v0.3.0 was 48. Header grew net +8 bits in v0.4.0: added `route_par` (1 bit) + `flit_ecc` (10 bit), recovered 3 bits from v0.3.0 MSB `rsvd` (which was always 0 in CB mode after `vc_id`). |
| `PAYLOAD_WIDTH` | derived | 352 | derived = max(per-channel payload widths) | Per-channel payload max (W/R = 352, AW/AR = 108, B = 64). v0.3.0 `wecc/recc` removed. Equivalent bits become `*_rsvd` future-extension. |
| `FLIT_ECC_WIDTH` | int | 10 | satisfy `2^(x-1) ≥ FLIT_DATA_WIDTH + x + 1` SECDED Hamming bound | Whole-flit SECDED syndrome width. Default 10 covers FLIT_DATA_WIDTH=398. Integrator must bump to 11 if header+payload growth makes FLIT_DATA_WIDTH > 502. |
| `ROUTE_PAR_WIDTH` | int | 1 | fixed = 1 | Routing parity width. Always 1-bit even parity over `src_id`+`dst_id`+`port_id`. |
| `X_WIDTH` | int | 4 | 2 ≤ x ≤ 8 | Mesh X coordinate width |
| `Y_WIDTH` | int | 4 | 2 ≤ x ≤ 8 | Mesh Y coordinate width |
| `BW_COUNTER_WIDTH` | int | 24 | 16 ≤ x ≤ 32 | QoS bandwidth counter width |
| `URGENCY_WIDTH` | int | 3 | 2 ≤ x ≤ 4 | Regulator urgency level width |
| `ERR_COUNTER_WIDTH` | int | 16 | 8 ≤ x ≤ 32 | Error counter width |
| `PORT_ID_WIDTH` | int | 2 | 1 ≤ x ≤ 4 | Width of `port_id` strap and flit-header field (router has up to 2^x LOCAL ports per node) |
| `MESH_COLS` | int | 4 | 1 ≤ x ≤ 2^X_WIDTH | Mesh column count; bounds `dst_id.x` for `NOC_FLIT_HDR_DST_ID_VALID` |
| `MESH_ROWS` | int | 4 | 1 ≤ x ≤ 2^Y_WIDTH | Mesh row count; bounds `dst_id.y` |
| `FLOW_CONTROL` | enum {`VALID_READY`, `CREDIT_BASED`} | `VALID_READY` | — | NoC link flow-control mode. `VALID_READY`: standard valid/ready handshake (per `NOC_MST_VALID_STABLE` rule). `CREDIT_BASED`: per-VC credit accounting; sender holds a credit counter per VC, decrements on flit injection, replenished by `noc_*_credit_*` return signals (see Wire table additions). Mode is compile-time fixed; cannot be switched at runtime. |
| `NUM_VC` | int | 1 | 1 ≤ x ≤ 8 | Number of virtual channels per NoC link. Upper bound 8 matches `VC_ID_WIDTH = 3` in flit header (see `02_flit.md` §1.2 Group 2). `NUM_VC=1` (default) degenerates to single-VC behaviour with no VC arbitration. `NUM_VC > 1` requires per-VC valid/ready/credit signal arrays and a VC arbitration policy (router-side per `06_qos.md §5`, NMU-side per `theory_of_operation.md §VC selection`). Deadlock-free routing across VCs is the integrator's responsibility. |
| `CDC_FIFO_DEPTH` | int | 16 | 4 ≤ x ≤ 64 (power-of-2 recommended) | Internal AXI ↔ NoC async-FIFO depth (gray-counter pointer + 2FF synchroniser). Sized to absorb `2 × max_round_trip_cycles × max(aclk_period, noc_clk_period) / min(aclk_period, noc_clk_period) + 2`; default 16 is conservative for ratio range [0.1, 10]. |
| `MAX_OUTSTANDING` | int | 8 | 1 ≤ x ≤ MAX_TXNS | Software-configurable cap on concurrent outstanding transactions (≤ `MAX_TXNS` hardware ceiling). The BFM allows the test author to throttle below the hardware ceiling for stress-testing scenarios. Implementation may expose this through `transaction_api.md` knob or compile-time parameter; if compile-time, default 8. |
| `MAX_BURST_LEN` | int | 16 | 1 ≤ x ≤ 256 | Maximum AXI burst length the NMU/NSU supports per transaction. Bounds the NSU W-reassembly buffer depth. Tests issuing `len + 1 > MAX_BURST_LEN` violate `apply_burst_write` precondition (per `transaction_api.md`); BFM returns `BURST_LEN_EXCEEDS_MAX`. |
| `ECC_GRANULE_WIDTH` | retired | — | — | (v0.3.0) Per-granule SECDED scheme retired in v0.4.0 in favour of whole-flit SECDED via `FLIT_ECC_WIDTH`. |
| `ECC_PER_GRANULE_WIDTH` | retired | — | — | (v0.3.0) Per-granule ECC width parameter retired with the per-granule scheme. |
| `ECC_FAIL_WIDTH` | retired | — | — | (v0.3.0) `ecc_fail` B-payload field dropped in v0.4.0. NSU now signals uncorrectable ECC via `bresp=SLVERR` in-band plus `ERR_STATUS` CSR ECC-class bit. |
| `ECC_WIDTH` | retired | — | — | (v0.3.0) Total per-granule ECC width replaced by parameter `FLIT_ECC_WIDTH` (whole-flit SECDED syndrome). |
| `MAX_RO_TXNS_PER_ID` | int | 32 | 1 ≤ x ≤ MAX_TXNS_PER_ID | NormalRoB status-table FIFO depth per AXI ID (FlooNoC `MaxRoTxnsPerId`). Bounds simultaneous outstanding transactions per ID that **require reordering** (i.e., go to different destinations). Default 32 matches FlooNoC default. |
| `ONLY_METADATA_B` | bool | true | — | B-RoB skips data-SRAM (B response is metadata-only: bid + bresp + buser). Saves significant area. R-RoB is always SRAM-backed (rdata is bulk data). FlooNoC `OnlyMetaData` parameter. |
| `NSU_R_BUFFER_DEPTH` | int | 4 | 1 ≤ x ≤ 16 | NSU read response buffer depth (entries × R flit). Smooths AXI-slave-to-NoC R injection back-pressure. Per AMD §NSU "Read responses are buffered before forwarding to minimize bubbles". Independent of MetaBuffer (which stores request headers). |
| `EXCLUSIVE_MONITOR_DEPTH` | int | 16 | 1 ≤ x ≤ MAX_TXNS | NSU Exclusive Monitor capacity (per-axi_id reservation slots). Per AMD §NSU AXI exclusive access handling. Limits concurrent exclusive-access reservations. |
| `ENABLE_AXI_PARITY` | bool | false | — | Enable optional AXI-side byte parity (data: 1 bit/byte) and address parity (1 bit). Per AMD §Data Integrity. When true: `axi_*_par_*` sideband signals are present. Independent of NoC-fabric flit-level ECC (`flit_ecc`/`route_par`). This is end-of-pipe AXI integrity only. |

## Optional features in / out of scope

- **Supported**: AXI4 full (AW/W/AR/B/R with bursts up to AWLEN=255 / ARLEN=255; multiple outstanding via RoB), end-to-end SECDED ECC on W and R data, RoB modes (Normal / Simple / NoRoB) per-channel, QoS Generator with 4 modes, Performance Probes (Packet / Transaction), runtime CSR-driven configuration, dual-clock-domain operation with internal CDC.
- **Not supported**: AXI4 atomic operations (ATOPs / AXI5) — out of scope. Cache-coherent / CHI-derived features. Low-power AXI handshake (CACTIVE/CSYSREQ).

## Channel grouping

Multiple logical channels across two AXI ports + NoC + CSR. Protocol-rule IDs in `protocol_rules.md` use these channel tokens:

| Channel | Wires |
|---------|-------|
| AW_IN | axi_in_aw* (manager port AW) |
| W_IN | axi_in_w* |
| B_IN | axi_in_b* |
| AR_IN | axi_in_ar* |
| R_IN | axi_in_r* |
| AW_OUT | axi_out_aw* (subordinate port AW) |
| W_OUT | axi_out_w* |
| B_OUT | axi_out_b* |
| AR_OUT | axi_out_ar* |
| R_OUT | axi_out_r* |
| REQ_OUT | noc_req_o_* (per-VC; incl. `noc_req_o_credit_i` and `noc_req_o_credit_init_ready_o`/`_i` when FLOW_CONTROL=CREDIT_BASED) |
| REQ_IN | noc_req_i_* (per-VC; incl. `noc_req_i_credit_o` when FLOW_CONTROL=CREDIT_BASED) |
| RSP_OUT | noc_rsp_o_* (per-VC; incl. `noc_rsp_o_credit_i` and `noc_rsp_o_credit_init_ready_o`/`_i` when FLOW_CONTROL=CREDIT_BASED) |
| RSP_IN | noc_rsp_i_* (per-VC; incl. `noc_rsp_i_credit_o` when FLOW_CONTROL=CREDIT_BASED) |
| CSR_AW / CSR_W / CSR_B / CSR_AR / CSR_R | csr_aw* / csr_w* / csr_b* / csr_ar* / csr_r* |

This separates manager-side and subordinate-side channels (e.g., `AW_IN` vs `AW_OUT`) so rules can be specific to which port a given AXI4 STABLE rule applies to.
