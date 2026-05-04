# Design Verification Plan

## Verification scope

In scope:
- Functional correctness of NMU and NSU datapaths (AXI ↔ flit conversion in both directions).
- Address translation: XY-direct decoding and SAM-table decoding.
- Reorder Buffer per-ID ordering, all three RoB types (`NormalRoB`, `SimpleRoB`, `NoRoB`).
- AXI burst handling, all burst types (FIXED, INCR, WRAP) and all burst lengths (`awlen ∈ 0..255`, but tests focus on representative subset).
- ECC generate/check round-trip, single-bit correction, double-bit detection and SLVERR propagation.
- QoS Generator behavior in all four modes (Bypass / Fixed / Limiter / Regulator).
- Performance Probes: bandwidth and latency histogram correctness.
- Backpressure: NMU InjectionBuffer-full, RoB-full, NSU local-AXI-not-ready propagation back to source AXI master.

Out of scope:
- Full mesh routing correctness (verified at NoC system level).
- Router internals and arbitration (separate spec).
- Post-place-and-route timing closure.
- Power-aware simulation (not enabled in current model).
- Cross-NI ECC error correlation (each NI is verified standalone for ECC).
- Multicast / reduction (rsvd_commtype + multicast header fields are reserved; behavior TBD).
<!-- source: 09_verification.md §1, §3.1; 02_flit.md §7 (multicast OOS) -->

## Testbench architecture

`TODO(designer):` Source describes the **C++ behavior model** verification (`09_verification.md`) and an **RTL co-simulation** flow via DPI-C, but does not commit to a single testbench methodology (UVM, cocotb, plain SV) for RTL-level standalone DV of `ni`. Decide and document here. The choices visible in source:

- C++ model unit tests use **GoogleTest**.
- RTL co-simulation is described abstractly: SystemVerilog testbench + RTL Router/NI vs. C++ NocSystem via DPI-C, comparing per-transaction results.

The expected RTL DV environment for `ni` standalone:

- Methodology: `TODO(designer): UVM | cocotb | plain SV`. Recommend UVM 1.2 for industry alignment.
- Top-level: `tb/tb_ni.sv`, instantiating `ni` DUT.
- Agents: `axi_in_agent` (drives the manager port from a UVM AXI master agent), `axi_out_agent` (passive monitor + responder on the subordinate port to model local memory), `noc_req_agent` and `noc_rsp_agent` (drive/monitor flit links).
- Reference model: a SystemVerilog wrapper around the C++ model (via DPI-C), or a re-implementation of the FlitPack/Unpack/RoB behavior in SVA-friendly form.
- Scoreboard: per-transaction comparison (data + response code) per `09_verification.md §6.2` "match basis: per-transaction".
<!-- source: 09_verification.md §2, §6.1, §6.2 -->

## Testpoints

| ID | Feature reference | Testpoint description |
|---|---|---|
| TP-01 | Single-flit write (1 hop) | Drive a single 32-byte AXI write through NMU; verify it arrives at the destination NSU's local AXI port intact. (Mirrors IT-01 in 09_verification.md.) |
| TP-02 | Multi-hop single-flit write | Drive a 32-byte write to a destination 3 hops away; verify integrity. (Mirrors IT-02.) |
| TP-03 | Burst write (`awlen=7`, 256 B) | NMU packs 1×AW + 8×W flits with correct `last` bit on the final W; NSU reassembles into a single AXI burst. (Mirrors IT-03.) |
| TP-04 | Single read (AR → R) | Drive AR; verify R returns with correct data and rresp. (Mirrors IT-04.) |
| TP-05 | Burst read (`arlen=15`, 512 B) | Verify multi-flit R reassembly at NMU. (Mirrors IT-05.) |
| TP-06 | Read-after-write to same address | Verify ordering through `ni` is preserved when same AXI ID is used. (Mirrors IT-08.) |
| TP-07 | Maximum outstanding | Issue 32 simultaneous outstanding transactions; verify RoB does not drop or reorder beyond AXI per-ID rules. (Mirrors IT-09.) |
| TP-08 | Multi-ID response reordering | Issue transactions with different `axi_id` returning out-of-order; verify per-ID release order via `NormalRoB`. (Mirrors IT-10.) |
| TP-09 | ECC single-bit correction | Inject 1-bit error in a W flit at the link layer between NMU and NSU; verify NSU's ECC Check corrects silently and does **not** raise SLVERR. (Mirrors IT-12.) |
| TP-10 | ECC double-bit detection | Inject 2-bit error in a W flit; verify NSU forwards the write but the resulting B flit carries `ecc_fail = 1`, AXI `bresp = SLVERR`, and `ECC_UNCORR_ERR_CNT` increments. (Mirrors IT-13.) |
| TP-11 | RoB full backpressure | Saturate NMU with outstanding transactions; verify `awready`/`arready` deassert. |
| TP-12 | InjectionBuffer full | Stall the connected Router on `noc_req_o`; verify NMU eventually backpressures the AXI side. |
| TP-13 | NSU local-slave backpressure | Hold `axi_out_rsp_i.awready = 0`; verify NSU eventually backpressures NoC link via `noc_req_i.ready = 0`. |
| TP-14 | XY address decoding | Issue writes whose addresses encode every legal `(x, y)` combination; verify `dst_id` in the produced flit. |
| TP-15 | SAM table decoding | Configure `ROUTE_CFG.USE_ID_TABLE = 1` with `NUM_SAM_RULES > 0`; verify lookup correctness. |
| TP-16 | QoS Bypass | `QOS_MODE = 0`; verify flit `qos = awqos / arqos` directly. |
| TP-17 | QoS Fixed | `QOS_MODE = 1`; verify flit `qos = QOS_FIXED_VALUE`. |
| TP-18 | QoS Limiter | `QOS_MODE = 2`; saturate the limiter, verify flit `qos` drops to `LOW_PRIORITY`. |
| TP-19 | QoS Regulator urgency | `QOS_MODE = 3`; starve responses, verify urgency_level rises and flit `qos` increases up to saturation. |
| TP-20 | Packet Probe correctness | Enable Packet Probe with a known traffic pattern; read `PKT_BYTE_COUNT` and `PKT_BANDWIDTH` and compare to expected. |
| TP-21 | Transaction Probe histogram | Inject transactions with controlled latency; verify each falls into the correct latency bin per `TXN_THRESHOLD_*`. |
| TP-22 | Reset asserts mid-AXI-transaction | `TODO(designer):` covers the gap noted in theory_of_operation.md §Resets. Verify slave-port outputs go to reset values within 1 `clk_i` cycle. |
| TP-23 | Reset asserts mid-flit-injection | `TODO(designer):` likewise. Verify `noc_req_o.valid` deasserts; no in-flight flit is partially driven post-reset. |
| TP-24 | Saturating error counters | Force `ECC_UNCORR_ERR_CNT` to maximum and beyond; verify saturation, no wrap. |
| TP-25 | `LAST_ERR_INFO` population | After ECC error, verify `LAST_ERR_INFO` is populated with correct `err_axi_id` / `err_src_id` / `err_dst_id`. |
| TP-26 | NMU-only / NSU-only configurations | Build with `EN_MGR_PORT = false` and `EN_SBR_PORT = true` (and vice versa); verify the disabled side's ports tie off cleanly and the enabled side functions. |
<!-- source: 09_verification.md §3.1; 04_network_interface.md §3.2, §3.4, §5 -->

`TODO(designer):` Testpoint coverage of `noc_req_t`/`noc_rsp_t` flow control modes (Valid/Ready vs. Credit-Based) is **not** enumerated; if `ni` is built with Credit-Based config, an additional set of credit-tracking testpoints is required.

## Functional coverage model

| Covergroup | Bins / crosses |
|---|---|
| `axi_channel_cov` | All five AXI channels (AW/W/AR/B/R) at least one transaction each. |
| `routing_cov` | All five Router output directions (N/S/E/W/LOCAL) at least one routing decision. |
| `rob_state_cov` | Each RoB entry visits FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE. |
| `rob_type_cov` | Each `B_ROB_TYPE` and `R_ROB_TYPE` value exercised. |
| `qos_mode_cov` | Each `QOS_MODE` value exercised; cross with traffic load. |
| `qos_value_cov` | All 16 `qos` values present in transmitted flits. |
| `burst_len_cov` | `awlen / arlen ∈ {0, 1, 7, 15, 255}`. |
| `burst_type_cov` | Each `awburst / arburst` ∈ {FIXED, INCR, WRAP}. |
| `ecc_cov` | No-error / 1-bit-error / 2-bit-error per channel; cross with W and R. |
| `error_path_cov` | Each `ERR_STATUS` bit set at least once; saturation of `ERR_COUNT` and `ECC_UNCORR_ERR_CNT`. |
| `flow_control_cov` | NMU InjectionBuffer fill-level; RoB occupancy; per-port credit (Credit-Based mode). |
<!-- source: 09_verification.md §7.1, §7.2 -->

## Assertion-based verification (ABV)

| Assertion | Property |
|---|---|
| `a_axi_handshake_in` | AXI in / out compliant: `valid` held until `ready` asserts; no `valid` retraction without handshake. |
| `a_noc_handshake` | `noc_req_*` / `noc_rsp_*` handshake compliance: `valid` held until `ready`. |
| `a_no_x_after_reset` | All output ports are non-X within 1 cycle after `rst_ni` deassertion. |
| `a_rob_state_legal` | `rob_state_cov` only takes legal transitions per the per-entry FSM. |
| `a_rob_no_double_alloc` | An `ALLOCATED` entry is not re-allocated until it transitions back to `FREE`. |
| `a_per_id_order` | For any AXI ID, response release order matches request issue order in `NormalRoB`/`SimpleRoB` modes. |
| `a_ecc_propagation` | A 2-bit error in a W flit always results in `bresp = SLVERR` on the downstream B response. |
| `a_qos_inheritance_w` | All W flits in a burst carry the same `qos` as the heading AW flit. |
| `a_qos_inheritance_rsp` | A response flit's `qos` matches the corresponding request's `qos`. |
| `a_last_bit_correctness` | `last = 1` exactly on the final flit of every multi-flit packet (W with last beat; R with last beat); `last = 1` on every single-flit packet (AW, AR, B). |
| `a_err_counter_saturating` | `ECC_UNCORR_ERR_CNT` does not wrap when at maximum. |
<!-- source: 09_verification.md §7.1 (FSM coverage); 04_network_interface.md §5 FR-05, FR-06; 02_flit.md §2.2.5 -->

`TODO(designer):` Mark each ABV assertion as spec-level vs. RTL-implementation. Spec-level assertions belong here; RTL-implementation assertions (e.g., a specific FIFO pointer never wraps in this implementation) belong in the RTL repository.

## Formal property verification (FPV)

`TODO(designer):` Source does not describe FPV for `ni`. Recommend FPV for the per-RoB-entry FSM (small state space; high payoff from proving "no entry stuck in ALLOCATED forever") and for the AXI-NoC channel mapping (`a_axi_ch_correct`: AW packs to `axi_ch=0`, W to 1, etc.). Decide and document.

## Security countermeasure testpoints

Not applicable. `ni` has no security countermeasures (see `theory_of_operation.md` §Security countermeasures). SECDED ECC is data-integrity only.

## Stages and exit criteria

| Stage | Status | Exit criterion |
|---|---|---|
| V0 | reached | This DV plan drafted (this document, post-import). |
| V1 | not started | TP-01..TP-26 all pass on a smoke build; coverage model coded; ABV all passing. |
| V2 | not started | 100% testpoint pass; ≥ 90% functional coverage; ≥ 95% code coverage; all ABV passing. |
| V3 | not started | 100% functional coverage; ≥ 99% code coverage; FPV decisions implemented; signoff review. |
<!-- source: 09_verification.md §7.1 thresholds (≥80% line, ≥70% branch); per-stage thresholds adapted from wctmr example -->
