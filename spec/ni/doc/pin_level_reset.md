# Pin-Level Reset Behavior

The NI has **two reset signals** corresponding to its two clock domains. Per-wire reset behavior is enumerated separately for each reset assertion. The set of wires must match `signal_interface.md` §Wire table exactly (LINT-BFM-001).

**Reset signals:**
- `arst_ni` (AXI side, sync to `aclk_i`, active LOW); covers all `axi_in_*`, `axi_out_*`, and `csr_*` wires
- `noc_rst_ni` (NoC side, sync to `noc_clk_i`, active LOW); covers all `noc_*` wires plus sideband (`id_i`, `route_table_i`)
- **Minimum assertion duration**: 16 cycles of the corresponding clock
- **Synchronicity**: async assertion / sync deassertion to the corresponding clock

A wire's "during reset" value is determined by its corresponding reset signal. Wires that straddle the AXI ↔ NoC CDC boundary internally are not exposed externally; external wires belong to exactly one reset domain.

## During reset (per relevant reset asserted)

### AXI Manager port — driven by AXI master DUT or by NMU; AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| AW_IN | axi_in_awvalid | as driven by DUT | input from AXI master |
| AW_IN | axi_in_awready | 0 | NMU not ready while reset |
| AW_IN | axi_in_awid | as driven by DUT |  |
| AW_IN | axi_in_awaddr | as driven by DUT |  |
| AW_IN | axi_in_awlen | as driven by DUT |  |
| AW_IN | axi_in_awsize | as driven by DUT |  |
| AW_IN | axi_in_awburst | as driven by DUT |  |
| AW_IN | axi_in_awcache | as driven by DUT |  |
| AW_IN | axi_in_awprot | as driven by DUT |  |
| AW_IN | axi_in_awqos | as driven by DUT |  |
| AW_IN | axi_in_awuser | as driven by DUT |  |
| W_IN | axi_in_wvalid | as driven by DUT |  |
| W_IN | axi_in_wready | 0 |  |
| W_IN | axi_in_wdata | as driven by DUT |  |
| W_IN | axi_in_wstrb | as driven by DUT |  |
| W_IN | axi_in_wlast | as driven by DUT |  |
| W_IN | axi_in_wuser | as driven by DUT |  |
| B_IN | axi_in_bvalid | 0 |  |
| B_IN | axi_in_bready | as driven by DUT |  |
| B_IN | axi_in_bid | 0 |  |
| B_IN | axi_in_bresp | 0 (OKAY) |  |
| B_IN | axi_in_buser | 0 |  |
| AR_IN | axi_in_arvalid | as driven by DUT |  |
| AR_IN | axi_in_arready | 0 |  |
| AR_IN | axi_in_arid | as driven by DUT |  |
| AR_IN | axi_in_araddr | as driven by DUT |  |
| AR_IN | axi_in_arlen | as driven by DUT |  |
| AR_IN | axi_in_arsize | as driven by DUT |  |
| AR_IN | axi_in_arburst | as driven by DUT |  |
| AR_IN | axi_in_arcache | as driven by DUT |  |
| AR_IN | axi_in_arprot | as driven by DUT |  |
| AR_IN | axi_in_arqos | as driven by DUT |  |
| AR_IN | axi_in_aruser | as driven by DUT |  |
| R_IN | axi_in_rvalid | 0 |  |
| R_IN | axi_in_rready | as driven by DUT |  |
| R_IN | axi_in_rid | 0 |  |
| R_IN | axi_in_rdata | 0 |  |
| R_IN | axi_in_rresp | 0 (OKAY) |  |
| R_IN | axi_in_rlast | 0 |  |
| R_IN | axi_in_ruser | 0 |  |

### AXI Subordinate port — driven by NSU or by AXI slave DUT; AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| AW_OUT | axi_out_awvalid | 0 | NSU not driving |
| AW_OUT | axi_out_awready | as driven by AXI slave | input |
| AW_OUT | axi_out_awid | 0 | registered default |
| AW_OUT | axi_out_awaddr | 0 | registered default |
| AW_OUT | axi_out_awlen | 0 | registered default |
| AW_OUT | axi_out_awsize | 0 | registered default |
| AW_OUT | axi_out_awburst | 0 | registered default |
| AW_OUT | axi_out_awcache | 0 | registered default |
| AW_OUT | axi_out_awprot | 0 | registered default |
| AW_OUT | axi_out_awqos | 0 | registered default |
| AW_OUT | axi_out_awuser | 0 | registered default |
| W_OUT | axi_out_wvalid | 0 |  |
| W_OUT | axi_out_wready | as driven by slave |  |
| W_OUT | axi_out_wdata | 0 |  |
| W_OUT | axi_out_wstrb | 0 |  |
| W_OUT | axi_out_wlast | 0 |  |
| W_OUT | axi_out_wuser | 0 |  |
| B_OUT | axi_out_bvalid | as driven by slave |  |
| B_OUT | axi_out_bready | 1 | always-ready while in reset to drain |
| B_OUT | axi_out_bid | as driven by slave |  |
| B_OUT | axi_out_bresp | as driven by slave |  |
| B_OUT | axi_out_buser | as driven by slave |  |
| AR_OUT | axi_out_arvalid | 0 |  |
| AR_OUT | axi_out_arready | as driven by slave |  |
| AR_OUT | axi_out_arid | 0 | registered default |
| AR_OUT | axi_out_araddr | 0 | registered default |
| AR_OUT | axi_out_arlen | 0 | registered default |
| AR_OUT | axi_out_arsize | 0 | registered default |
| AR_OUT | axi_out_arburst | 0 | registered default |
| AR_OUT | axi_out_arcache | 0 | registered default |
| AR_OUT | axi_out_arprot | 0 | registered default |
| AR_OUT | axi_out_arqos | 0 | registered default |
| AR_OUT | axi_out_aruser | 0 | registered default |
| R_OUT | axi_out_rvalid | as driven by slave |  |
| R_OUT | axi_out_rready | 1 | always-ready to drain |
| R_OUT | axi_out_rid | as driven by slave |  |
| R_OUT | axi_out_rdata | as driven by slave |  |
| R_OUT | axi_out_rresp | as driven by slave |  |
| R_OUT | axi_out_rlast | as driven by slave |  |
| R_OUT | axi_out_ruser | as driven by slave |  |

### NoC Request link — NoC domain (noc_rst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| REQ_OUT | noc_req_o_valid | 0 |  |
| REQ_OUT | noc_req_o_ready | as driven by router | input |
| REQ_OUT | noc_req_o_flit | 0 (all-zero) | Held to 0 for waveform readability |
| REQ_IN | noc_req_i_valid | as driven by router |  |
| REQ_IN | noc_req_i_ready | 0 | NSU not accepting |
| REQ_IN | noc_req_i_flit | as driven by router |  |

### NoC Response link — NoC domain (noc_rst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| RSP_OUT | noc_rsp_o_valid | 0 |  |
| RSP_OUT | noc_rsp_o_ready | as driven by router |  |
| RSP_OUT | noc_rsp_o_flit | 0 |  |
| RSP_IN | noc_rsp_i_valid | as driven by router |  |
| RSP_IN | noc_rsp_i_ready | 0 |  |
| RSP_IN | noc_rsp_i_flit | as driven by router |  |

### CSR access port — AXI domain (arst_ni)

| Channel | Signal | Value during reset | Notes |
|---------|--------|--------------------|-------|
| CSR_AW | csr_awvalid | as driven by master | input from CSR master |
| CSR_AW | csr_awready | 0 |  |
| CSR_AW | csr_awaddr | as driven |  |
| CSR_AW | csr_awprot | as driven |  |
| CSR_W | csr_wvalid | as driven |  |
| CSR_W | csr_wready | 0 |  |
| CSR_W | csr_wdata | as driven |  |
| CSR_W | csr_wstrb | as driven |  |
| CSR_B | csr_bvalid | 0 |  |
| CSR_B | csr_bready | as driven |  |
| CSR_B | csr_bresp | 0 (OKAY) |  |
| CSR_AR | csr_arvalid | as driven |  |
| CSR_AR | csr_arready | 0 |  |
| CSR_AR | csr_araddr | as driven |  |
| CSR_AR | csr_arprot | as driven |  |
| CSR_R | csr_rvalid | 0 |  |
| CSR_R | csr_rready | as driven |  |
| CSR_R | csr_rdata | 0 |  |
| CSR_R | csr_rresp | 0 (OKAY) |  |

### Sideband — NoC domain (noc_rst_ni)

| Signal | Value during reset | Notes |
|--------|--------------------|-------|
| id_i | as driven by integrator | strap-style, expected stable |
| route_table_i | as driven by integrator | strap-style, expected stable when USE_ID_TABLE=1 |

## After reset (first clock edge with respective reset deasserted)

For wires driven by the BFM, the first-cycle-after-reset value is generally the same as during-reset (registered outputs hold). The notable differences:

| Wire | First cycle after reset | Notes |
|------|-------------------------|-------|
| axi_in_awready / wready / arready | 0 → returns to 1 when NMU is ready to accept stimulus | Default-on at reset deassertion in active mode (1 cycle latency for tracker reset) |
| axi_out_bready / rready | 1 (still always-ready) | Held |
| All BFM-driven `*valid` outputs (`axi_in_bvalid`, `axi_in_rvalid`, `axi_out_*valid`, `noc_*_o_valid`, `csr_*valid` outputs) | 0 | Asserts only when a transaction is ready to drive |
| `noc_req_i_ready` / `noc_rsp_i_ready` | 0 → returns to 1 when NSU/NMU receive-buffer slot available | Default-on |

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
- Cross-domain partial reset can leave the FIFO in an inconsistent state where one pointer is 0 and the other is not. The FIFO must self-recover when both resets eventually align — `flush_on_full_reset` mechanism. TODO(designer): confirm RTL implements `flush_on_full_reset` identically to BFM (no issue yet — behavioral equivalence will be verified during D2 RTL-vs-BFM cross-check per stage_gates.md D2.bfm.self_check; BFM model assumes yes).
