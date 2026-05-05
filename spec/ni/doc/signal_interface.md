# Signal Interface

**Protocols:**
- AXI side: AXI4 (ARM IHI 0022)
- NoC side: custom flit-based packet protocol; flit width `FLIT_WIDTH` bits (default 400), header `HEADER_WIDTH` bits (default 48). See `02_flit.md` in noc-sim source repo for flit format details.
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

| Signal | Direction | Width | Active | Sample edge | Reset value | Optional | BFM supports | Notes |
|--------|-----------|-------|--------|-------------|-------------|----------|--------------|-------|
| `noc_req_o_valid` | output | 1 | H | pos noc_clk | §NoC req out row 1 | no | yes | NMU asserts to inject a flit |
| `noc_req_o_ready` | input | 1 | H | pos noc_clk | §NoC req out row 2 | no | yes | Router asserts when ready to accept |
| `noc_req_o_flit` | output | FLIT_WIDTH | — | pos noc_clk | §NoC req out row 3 | no | yes | Flit payload; valid when noc_req_o_valid=1 |
| `noc_req_i_valid` | input | 1 | H | pos noc_clk | §NoC req in row 1 | no | yes | Router signals incoming flit |
| `noc_req_i_ready` | output | 1 | H | pos noc_clk | §NoC req in row 2 | no | yes | NSU asserts when ready to accept |
| `noc_req_i_flit` | input | FLIT_WIDTH | — | pos noc_clk | §NoC req in row 3 | no | yes | Inbound flit payload |

### NoC Response link

Same shape as Request link, with `noc_rsp_*` prefix. NSU injects via `noc_rsp_o_*`; NMU receives via `noc_rsp_i_*`.

| Signal | Direction | Width | Reset | Notes |
|--------|-----------|-------|-------|-------|
| `noc_rsp_o_valid` | output | 1 | §NoC rsp out row 1 | NSU asserts to inject response flit |
| `noc_rsp_o_ready` | input | 1 | §NoC rsp out row 2 |  |
| `noc_rsp_o_flit` | output | FLIT_WIDTH | §NoC rsp out row 3 |  |
| `noc_rsp_i_valid` | input | 1 | §NoC rsp in row 1 |  |
| `noc_rsp_i_ready` | output | 1 | §NoC rsp in row 2 | NMU asserts when RoB has slot |
| `noc_rsp_i_flit` | input | FLIT_WIDTH | §NoC rsp in row 3 |  |

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
| `id_i` | input | X_WIDTH+Y_WIDTH (default 8) | §Sideband row 1 | This NI's Node ID; strap-style; sampled in noc_clk domain |
| `route_table_i` | input | NUM_SAM_RULES × sizeof(sam_rule_t) | §Sideband row 2 | Routing table; only valid when `USE_ID_TABLE=1`; strap-style |

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
| `DATA_WIDTH` | int | 256 | 32 / 64 / 128 / 256 / 512 | AXI data width |
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
| `FLIT_WIDTH` | int | 400 | 256 ≤ x ≤ 512 | Flit total width (header + payload + ECC) |
| `HEADER_WIDTH` | int | 48 | 32 ≤ x ≤ 64 | Flit header width |
| `X_WIDTH` | int | 4 | 2 ≤ x ≤ 8 | Mesh X coordinate width |
| `Y_WIDTH` | int | 4 | 2 ≤ x ≤ 8 | Mesh Y coordinate width |
| `BW_COUNTER_WIDTH` | int | 24 | 16 ≤ x ≤ 32 | QoS bandwidth counter width |
| `URGENCY_WIDTH` | int | 3 | 2 ≤ x ≤ 4 | Regulator urgency level width |
| `ERR_COUNTER_WIDTH` | int | 16 | 8 ≤ x ≤ 32 | Error counter width |

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
| REQ_OUT | noc_req_o_* |
| REQ_IN | noc_req_i_* |
| RSP_OUT | noc_rsp_o_* |
| RSP_IN | noc_rsp_i_* |
| CSR_AW / CSR_W / CSR_B / CSR_AR / CSR_R | csr_aw* / csr_w* / csr_b* / csr_ar* / csr_r* |

This separates manager-side and subordinate-side channels (e.g., `AW_IN` vs `AW_OUT`) so rules can be specific to which port a given AXI4 STABLE rule applies to.
