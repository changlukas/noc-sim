# Pin-Level Reset Behavior

The NI has **two reset signals** corresponding to its two clock domains. Per-wire reset behavior is enumerated separately for each reset assertion. The set of wires must match `signal_interface.md` §Wire table exactly (LINT-BFM-001).

**Reset signals:**
- `arst_ni` (AXI side, sync to `aclk_i`, active LOW); covers all `axi_*_i`, `axi_*_o`, and `csr_*` wires
- `noc_rst_ni` (NoC side, sync to `noc_clk_i`, active LOW); covers all `noc_*` wires plus sideband (`id_i`, `route_table_i`)
- **Minimum assertion duration**: 16 cycles of the corresponding clock
- **Synchronicity**: async assertion / sync deassertion to the corresponding clock

A wire's "during reset" value is determined by its corresponding reset signal. Wires that straddle the AXI ↔ NoC CDC boundary internally are not exposed externally; external wires belong to exactly one reset domain.

## During reset (per relevant reset asserted)

### AXI Manager port — driven by AXI master DUT or by NMU; AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| AW_IN | axi_awvalid_i | as driven by DUT | input from AXI master |
| AW_IN | axi_awready_o | 0 | NMU not ready while reset |
| AW_IN | axi_awid_i | as driven by DUT |  |
| AW_IN | axi_awaddr_i | as driven by DUT |  |
| AW_IN | axi_awlen_i | as driven by DUT |  |
| AW_IN | axi_awsize_i | as driven by DUT |  |
| AW_IN | axi_awburst_i | as driven by DUT |  |
| AW_IN | axi_awlock_i | as driven by DUT | Exclusive access indicator |
| AW_IN | axi_awcache_i | as driven by DUT |  |
| AW_IN | axi_awprot_i | as driven by DUT |  |
| AW_IN | axi_awqos_i | as driven by DUT |  |
| AW_IN | axi_awregion_i | as driven by DUT | Region identifier |
| AW_IN | axi_awuser_i | as driven by DUT |  |
| AW_IN | axi_awatop_i | as driven by DUT | sample-only; out-of-scope per ToO §ATOPs |
| W_IN | axi_wvalid_i | as driven by DUT |  |
| W_IN | axi_wready_o | 0 |  |
| W_IN | axi_wdata_i | as driven by DUT |  |
| W_IN | axi_wstrb_i | as driven by DUT |  |
| W_IN | axi_wlast_i | as driven by DUT |  |
| W_IN | axi_wuser_i | as driven by DUT |  |
| B_IN | axi_bvalid_o | 0 |  |
| B_IN | axi_bready_i | as driven by DUT |  |
| B_IN | axi_bid_o | 0 |  |
| B_IN | axi_bresp_o | 0 (OKAY) |  |
| B_IN | axi_buser_o | 0 |  |
| AR_IN | axi_arvalid_i | as driven by DUT |  |
| AR_IN | axi_arready_o | 0 |  |
| AR_IN | axi_arid_i | as driven by DUT |  |
| AR_IN | axi_araddr_i | as driven by DUT |  |
| AR_IN | axi_arlen_i | as driven by DUT |  |
| AR_IN | axi_arsize_i | as driven by DUT |  |
| AR_IN | axi_arburst_i | as driven by DUT |  |
| AR_IN | axi_arlock_i | as driven by DUT | Exclusive access indicator |
| AR_IN | axi_arcache_i | as driven by DUT |  |
| AR_IN | axi_arprot_i | as driven by DUT |  |
| AR_IN | axi_arqos_i | as driven by DUT |  |
| AR_IN | axi_arregion_i | as driven by DUT | Region identifier |
| AR_IN | axi_aruser_i | as driven by DUT |  |
| R_IN | axi_rvalid_o | 0 |  |
| R_IN | axi_rready_i | as driven by DUT |  |
| R_IN | axi_rid_o | 0 |  |
| R_IN | axi_rdata_o | 0 |  |
| R_IN | axi_rresp_o | 0 (OKAY) |  |
| R_IN | axi_rlast_o | 0 |  |
| R_IN | axi_ruser_o | 0 |  |

### AXI Subordinate port — driven by NSU or by AXI slave DUT; AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| AW_OUT | axi_awvalid_o | 0 | NSU not driving |
| AW_OUT | axi_awready_i | as driven by AXI slave | input |
| AW_OUT | axi_awid_o | 0 | registered default |
| AW_OUT | axi_awaddr_o | 0 | registered default |
| AW_OUT | axi_awlen_o | 0 | registered default |
| AW_OUT | axi_awsize_o | 0 | registered default |
| AW_OUT | axi_awburst_o | 0 | registered default |
| AW_OUT | axi_awlock_o | 0 | registered default |
| AW_OUT | axi_awcache_o | 0 | registered default |
| AW_OUT | axi_awprot_o | 0 | registered default |
| AW_OUT | axi_awqos_o | 0 | registered default |
| AW_OUT | axi_awregion_o | 0 | registered default |
| AW_OUT | axi_awuser_o | 0 | registered default |
| W_OUT | axi_wvalid_o | 0 |  |
| W_OUT | axi_wready_i | as driven by slave |  |
| W_OUT | axi_wdata_o | 0 |  |
| W_OUT | axi_wstrb_o | 0 |  |
| W_OUT | axi_wlast_o | 0 |  |
| W_OUT | axi_wuser_o | 0 |  |
| B_OUT | axi_bvalid_i | as driven by slave |  |
| B_OUT | axi_bready_o | 1 | always-ready while in reset to drain |
| B_OUT | axi_bid_i | as driven by slave |  |
| B_OUT | axi_bresp_i | as driven by slave |  |
| B_OUT | axi_buser_i | as driven by slave |  |
| AR_OUT | axi_arvalid_o | 0 |  |
| AR_OUT | axi_arready_i | as driven by slave |  |
| AR_OUT | axi_arid_o | 0 | registered default |
| AR_OUT | axi_araddr_o | 0 | registered default |
| AR_OUT | axi_arlen_o | 0 | registered default |
| AR_OUT | axi_arsize_o | 0 | registered default |
| AR_OUT | axi_arburst_o | 0 | registered default |
| AR_OUT | axi_arlock_o | 0 | registered default |
| AR_OUT | axi_arcache_o | 0 | registered default |
| AR_OUT | axi_arprot_o | 0 | registered default |
| AR_OUT | axi_arqos_o | 0 | registered default |
| AR_OUT | axi_arregion_o | 0 | registered default |
| AR_OUT | axi_aruser_o | 0 | registered default |
| R_OUT | axi_rvalid_i | as driven by slave |  |
| R_OUT | axi_rready_o | 1 | always-ready to drain |
| R_OUT | axi_rid_i | as driven by slave |  |
| R_OUT | axi_rdata_i | as driven by slave |  |
| R_OUT | axi_rresp_i | as driven by slave |  |
| R_OUT | axi_rlast_i | as driven by slave |  |
| R_OUT | axi_ruser_i | as driven by slave |  |

### NoC Request link — NoC domain (noc_rst_ni)

All `valid`/`ready`/`flit` are per-VC arrays of width `NUM_VC` (default 1). Reset values apply to every VC slot.

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| REQ_OUT | noc_req_valid_o[NUM_VC-1:0] | 0 (all VCs) |  |
| REQ_OUT | noc_req_ready_i[NUM_VC-1:0] | as driven by router | Input. Per-VC. |
| REQ_OUT | noc_req_flit_o[NUM_VC-1:0][FLIT_WIDTH-1:0] | 0 (all VCs, all bits) | Held to 0 for waveform readability. |
| REQ_IN | noc_req_valid_i[NUM_VC-1:0] | as driven by router |  |
| REQ_IN | noc_req_ready_o[NUM_VC-1:0] | 0 (all VCs) | NSU not accepting. |
| REQ_IN | noc_req_flit_i[NUM_VC-1:0][FLIT_WIDTH-1:0] | as driven by router |  |

### NoC Response link — NoC domain (noc_rst_ni)

All `valid`/`ready`/`flit` are per-VC arrays of width `NUM_VC` (default 1).

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| RSP_OUT | noc_rsp_valid_o[NUM_VC-1:0] | 0 (all VCs) |  |
| RSP_OUT | noc_rsp_ready_i[NUM_VC-1:0] | as driven by router |  |
| RSP_OUT | noc_rsp_flit_o[NUM_VC-1:0][FLIT_WIDTH-1:0] | 0 (all VCs, all bits) |  |
| RSP_IN | noc_rsp_valid_i[NUM_VC-1:0] | as driven by router |  |
| RSP_IN | noc_rsp_ready_o[NUM_VC-1:0] | 0 (all VCs) |  |
| RSP_IN | noc_rsp_flit_i[NUM_VC-1:0][FLIT_WIDTH-1:0] | as driven by router |  |

### CSR access port — AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| CSR_AW | csr_awvalid_i | as driven by master | input from CSR master |
| CSR_AW | csr_awready_o | 0 |  |
| CSR_AW | csr_awaddr_i | as driven |  |
| CSR_AW | csr_awprot_i | as driven |  |
| CSR_W | csr_wvalid_i | as driven |  |
| CSR_W | csr_wready_o | 0 |  |
| CSR_W | csr_wdata_i | as driven |  |
| CSR_W | csr_wstrb_i | as driven |  |
| CSR_B | csr_bvalid_o | 0 |  |
| CSR_B | csr_bready_i | as driven |  |
| CSR_B | csr_bresp_o | 0 (OKAY) |  |
| CSR_AR | csr_arvalid_i | as driven |  |
| CSR_AR | csr_arready_o | 0 |  |
| CSR_AR | csr_araddr_i | as driven |  |
| CSR_AR | csr_arprot_i | as driven |  |
| CSR_R | csr_rvalid_o | 0 |  |
| CSR_R | csr_rready_i | as driven |  |
| CSR_R | csr_rdata_o | 0 |  |
| CSR_R | csr_rresp_o | 0 (OKAY) |  |

### Sideband — NoC domain (noc_rst_ni)

| Signal | Value during reset | Notes |
|--------|--------------------|-------|
| id_i | as driven by integrator | strap-style, expected stable |
| port_id_i | as driven by integrator | strap-style, expected stable; selects router LOCAL port index |
| route_table_i | as driven by integrator | strap-style, expected stable when USE_ID_TABLE=1 |

### Interrupt output — AXI domain (arst_ni)

| Signal | Value during reset | Notes |
|--------|--------------------|-------|
| irq_o | 0 | Held LOW while `arst_ni` is asserted. Internal `ERR_STATUS` and `IRQ_ENABLE` registers are reset to 0x0 at the same time, so the function `irq_o = OR(ERR_STATUS[i] & IRQ_ENABLE[i])` evaluates to 0 by construction. |

### NoC credit signals — NoC domain (noc_rst_ni); present only when `FLOW_CONTROL = CREDIT_BASED`

Per-VC credit return signals:

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| REQ_OUT | noc_req_credit_i[NUM_VC-1:0] | as driven by router | Input. Per-VC credit return from router. |
| REQ_IN | noc_req_credit_o[NUM_VC-1:0] | 0 (all VCs) | NSU not returning credits while reset. |
| RSP_OUT | noc_rsp_credit_i[NUM_VC-1:0] | as driven by router | Input. |
| RSP_IN | noc_rsp_credit_o[NUM_VC-1:0] | 0 (all VCs) | NMU not returning credits while reset. |

Credit startup handshake signals:

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| REQ_OUT | noc_req_credit_init_ready_o | 0 | NMU asserts AFTER reset deassertion when ready to start credit exchange. |
| REQ_OUT | noc_req_credit_init_ready_i | as driven by router | Router asserts after its reset deassertion. |
| RSP_OUT | noc_rsp_credit_init_ready_o | 0 | NSU asserts AFTER reset deassertion. |
| RSP_OUT | noc_rsp_credit_init_ready_i | as driven by router | Router asserts after its reset deassertion. |

When `FLOW_CONTROL = VALID_READY` (default), all credit signals and credit-init-ready signals are absent. See `signal_interface.md` §"NoC credit signals" for the full conditional contract.

### Optional AXI parity signals — AXI domain (arst_ni)

Conditional presence:
- `axi_*_i_par_*` signals present only when `ENABLE_AXI_PARITY = true` AND `EN_MGR_PORT = 1`
- `axi_*_o_par_*` signals present only when `ENABLE_AXI_PARITY = true` AND `EN_SBR_PORT = 1`

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| AW_IN | axi_awaddr_par_i | as driven by DUT | Input. Requires `EN_MGR_PORT=1`. |
| AR_IN | axi_araddr_par_i | as driven by DUT | Input. Requires `EN_MGR_PORT=1`. |
| W_IN | axi_wdata_par_i[DATA_WIDTH/8-1:0] | as driven by DUT | Input. Per-byte parity. Requires `EN_MGR_PORT=1`. |
| AW_OUT | axi_awaddr_par_o | 0 | NSU-driven. Held 0 while reset. Requires `EN_SBR_PORT=1`. |
| AR_OUT | axi_araddr_par_o | 0 | NSU-driven. Requires `EN_SBR_PORT=1`. |
| W_OUT | axi_wdata_par_o[DATA_WIDTH/8-1:0] | 0 | NSU-driven per-byte parity. Requires `EN_SBR_PORT=1`. |
| R_OUT | axi_rdata_par_i[DATA_WIDTH/8-1:0] | as driven by slave | Input from local slave. Requires `EN_SBR_PORT=1`. |

Default `ENABLE_AXI_PARITY = true` — these parity signals are present on the wire list (matches AMD pg313 §Data Integrity default-on stance). Integrators MAY set `false` at instantiation to omit the entire parity sideband, in which case all `axi_*_par_*` signals are absent regardless of `EN_MGR_PORT` / `EN_SBR_PORT`.

## After reset (first clock edge with respective reset deasserted)

For wires driven by the BFM, the first-cycle-after-reset value is generally the same as during-reset (registered outputs hold). The notable differences:

| Wire | First cycle after reset | Notes |
|------|-------------------------|-------|
| axi_awready_o / axi_wready_o / axi_arready_o | 0 → returns to 1 when NMU is ready to accept stimulus | Default-on at reset deassertion in active mode (1 cycle latency for tracker reset) |
| axi_bready_o / axi_rready_o | 1 (still always-ready) | Held |
| All BFM-driven `*valid_o` outputs (manager-port responses `axi_bvalid_o`, `axi_rvalid_o`; subordinate-port requests `axi_awvalid_o`, `axi_wvalid_o`, `axi_arvalid_o`; NoC `noc_req_valid_o`, `noc_rsp_valid_o`; CSR `csr_bvalid_o`, `csr_rvalid_o`) | 0 | Asserts only when a transaction is ready to drive |
| `noc_req_ready_o` / `noc_rsp_ready_o` | 0 → returns to 1 when NSU/NMU receive-buffer slot available | Default-on |
| `irq_o` | 0 (held) | Stays LOW until any unmasked `ERR_STATUS` bit asserts. Asserts on the same `aclk_i` edge the OR-function `OR(ERR_STATUS[i] & IRQ_ENABLE[i])` evaluates to 1; deasserts on the cycle software RW1C clears the last set+enabled bit. |

For wires driven by external DUTs ("as driven by DUT" entries above), values during the after-reset window are externally controlled.

## Reset entry sequencing

1. **Either reset asserts asynchronously**. All BFM outputs in the corresponding domain assert their "during reset" values combinationally — no clock cycle of delay.
2. **While the reset is low**:
   - All in-flight transaction trackers in the affected domain are dropped.
   - RoB entries cleared (B and R RoBs both reset on `arst_ni`; cross-domain in-flight transactions on CDC FIFOs partially clear — write-side ptr resets if writer's domain is in reset, read-side ptr resets if reader's domain is in reset).
   - Pending response_delay countdowns cancelled.
   - One-shot fault flags cleared.
   - Observation lists are NOT cleared (cleared only by `reset_state()` API).
   - Configuration store survives wire-level reset; only `reset_state()` restores defaults.
3. **Reset deasserts on the corresponding clock's rising edge**. BFM outputs transition to their "after reset" values; ready signals become assertable when the BFM enables stimulus acceptance (default: enabled on cycle 1 post-reset deassertion in active mode).
4. **Cross-domain partial reset** (one reset asserted, the other deasserted):
   - The asserted side's wires hold during-reset values; the asserted side does not service traffic.
   - The deasserted side services its own intra-domain traffic but cross-domain transactions stall.
   - CDC FIFOs hold their reset values on the asserted side; the deasserted side sees its FIFO read port as empty (or write port as not-ready).
   - Integrator should ensure both resets reach a consistent state by power-on completion. Typical pattern: assert both resets together at power-on, hold for at least max(16 aclk cycles, 16 noc_clk cycles), then deassert both with proper sync.

## CDC FIFO reset

Internal async FIFOs at the AXI ↔ NoC boundary use the following reset semantics (not externally visible but documented for RTL reference):

- Write-side pointer resets on the writer's domain reset.
- Read-side pointer resets on the reader's domain reset.
- Empty signal asserts on read side when both pointers are at 0 (post-reset baseline).
- Cross-domain partial reset can leave the FIFO in an inconsistent state where one pointer is 0 and the other is not. The FIFO must self-recover when both resets eventually align — `flush_on_full_reset` mechanism. RTL implementation MUST be behaviourally equivalent to the BFM here (same flush trigger, same post-flush pointer state); equivalence is enforced at D2 cross-check per `stage_gates.md` `D2.bfm.self_check`.
