# Active vs Passive Mode

## Capability table

| Capability | Active | Passive |
|------------|--------|---------|
| Drives AXI manager port outputs (`axi_rsp_o.*`) | yes | no |
| Drives AXI subordinate port outputs (`axi_req_o.*`) | yes | no |
| Drives NoC outputs (`noc_req_valid_o`, `noc_req_flit_o`, `noc_rsp_valid_o`, `noc_rsp_flit_o`) | yes | no |
| Drives ready-back signals (`noc_req_ready_o`, `noc_rsp_ready_o`) | yes | no — NoC links un-drained in passive mode (must be paired with a real receiver) |
| Drives CSR access port outputs (`csr_axi_rsp_o.*`) | yes | no |
| Samples all inbound signals (AXI inputs, NoC inputs, CSR access inputs) | yes | yes |
| Reconstructs AXI transactions from observed activity | yes | yes |
| Reconstructs NoC flits / packets from observed activity | yes | yes |
| Generates response stimulus via Transaction API (`apply_*`, `set_response_*`, etc.) | yes | no — knobs accepted but no driving effect |
| Reports protocol-rule violations per `protocol_rules.md` | yes | yes |
| Contributes to coverage hooks per `dv/plan.md` | yes | yes |
| `expect_*` methods work for monitoring | yes | yes |
| ECC validation on inbound flits | yes | yes |

## Mode switch

- **Knob name**: `bfm_mode`
- **Type / values**: enum `{ACTIVE, PASSIVE}`
- **Default**: `ACTIVE`
- **Granularity**: per-NI (NMU and NSU switch together). A single `bfm_mode` knob is sufficient for all currently planned testbench setups, including the 4-file mix-and-match co-sim arrangements (NMU.rtl / NMU.cpp / NSU.rtl / NSU.cpp per `docs/design/08_simulation.md` §9 hot-swap). In those arrangements each NI BFM instance houses only one half — instantiated as NMU-only (`EN_MGR_PORT=1, EN_SBR_PORT=0`) or NSU-only (`EN_MGR_PORT=0, EN_SBR_PORT=1`) — so the single mode knob applies cleanly to whichever half is built. Future monitor-style use cases — e.g., a single instance hosting both halves where NMU passively observes the RTL master while NSU actively drives the slave — would require splitting the knob into per-half modes; that extension is intentionally out of scope until such a testbench is introduced.
- **API to switch**: `set_bfm_mode(mode)` per `transaction_api.md`.
- **Effect of ACTIVE → PASSIVE**: All BFM-driven outputs (AXI side: `axi_rsp_o.*`, `axi_req_o.*`; NoC side: `noc_req_o.*`, `noc_rsp_o.*`, ready-back signals; CSR side: `csr_axi_rsp_o.*`) transition to their during-reset values per `pin_level_reset.md` within one cycle of the corresponding clock. In-flight Transaction API calls unblock with `MODE_SWITCHED_TO_PASSIVE`. The BFM continues to monitor and log violations. Corresponds to `protocol_rules.md` `NI_CFG_MODE_SWITCH`.
- **Effect of PASSIVE → ACTIVE**: BFM-driven outputs return to reset-deassertion values; configuration knobs (set during PASSIVE) become effective on the first transaction after the switch.
- **Switching mid-transaction**: Permitted; BFM logs warning. Test author should call `reset_state()` before switching back to ACTIVE if mid-transaction state was non-trivial.

## Common testbench setups

- **Single-NI testbench**: one active NI bridging an AXI master DUT to a stub NoC (router model). Most common for unit-test of an attached IP that uses the NI.
- **NI-pair testbench**: two active NIs facing each other through a router fabric, used for end-to-end transaction testing (e.g., IP at node A reads from memory at node B).
- **Mixed (mesh integration regression)**: real RTL NIs at all nodes; one passive NI BFM attached to a chosen node for protocol violation detection without altering DUT behavior.
- **Forbidden**: two active NIs driving the same AXI manager port or the same NoC link. Only one active driver per link.

## Reset interaction

`bfm_mode` is preserved across both `arst_ni` and `noc_rst_ni`. Mode survives wire-level reset; only `reset_state()` API or test-author intervention restores defaults. Default `bfm_mode = ACTIVE` on first instantiation.
