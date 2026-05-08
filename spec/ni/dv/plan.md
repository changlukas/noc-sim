# DV Plan

## Verification scope

Verify the NI against:
1. AXI4 protocol compliance (host side, both manager and subordinate ports + AXI4-Lite for CSR access).
2. NoC flit protocol compliance (per protocol_rules.md `NOC_*` rules).
3. Cross-protocol transformation correctness (AXI ↔ flit packing/unpacking, ECC end-to-end).
4. RoB ordering invariants (per-AXI-ID).
5. QoS Generator modes (Bypass / Fixed / Limiter / Regulator) functional behavior.
6. Performance Probe accuracy (Packet bandwidth statistics, Transaction latency histogram).
7. Error monitoring CSRs (saturating counters, ERR_STATUS write-1-to-clear across 3 event classes, LAST_ERR_INFO sticky capture).
8. Interrupt mechanism (`irq_o` level-sensitive, IRQ_ENABLE masking, RW1C deassertion, per `protocol_rules.md NI_IRQ_LEVEL`).
9. AXI host-side parity (data + address) check, log-only behaviour (no AXI rresp synthesis).
10. Dual-clock-domain CDC correctness (no metastability, no data loss across aclk ↔ noc_clk).
11. Reset behavior (per pin_level_reset.md, including partial-reset edge cases).
12. Mode switch (ACTIVE / PASSIVE).
13. NMU-side software quiesce flow (`QUIESCE_CTRL` / `QUIESCE_STATUS`; drain correctness; NMU-only scope; best-effort liveness — no NI-side guaranteed bound).
14. Outstanding-transaction count CSR observability (`PENDING_R_COUNT` / `PENDING_W_COUNT`) and NSU Exclusive Monitor CSR clear / observability (`EXCLUSIVE_MONITOR_CTRL` / `EXCLUSIVE_MONITOR_STATUS`; race semantics with concurrent NSU events).

DV strategy:
- **Constrained-random testing** — **UVM 1.2** (industry standard; mature DV ecosystem; matches assumed in-house DV expertise). Master DUT stimulates AXI; NoC router stub provides flit endpoint; scoreboard cross-checks AXI handshakes against observed NoC flits. **Designer-confirmed (A5 wave 2026-05-08): UVM 1.2.**
- **Directed tests for configuration knobs** — verify each CSR write produces the documented wire-level effect.
- **Mode-switch tests** — ACTIVE / PASSIVE transitions with and without in-flight transactions.
- **Reset tests** — assert each reset mid-transaction at every channel state; assert both resets together; assert partial reset.
- **CDC tests** — vary aclk / noc_clk frequency ratios across [0.1, 1, 10] to stress async FIFO depth.
- **ABV (always-on SVA assertions)** — every FAIL-severity row in protocol_rules.md maps to one SVA `assert property`.
- **FPV (formal property verification)** — RoB allocator state machine; ECC SECDED correctness (gen + check round-trip).

## Testpoints

Mapping README Features → testpoints (per stage gate D1.dv.testpoints requirement). Source noc-sim 09_verification.md provides additional testpoints; merged here.

| ID | README Feature | Testpoint description | Protocol rules exercised |
|----|----------------|------------------------|--------------------------|
| TP1 | AXI4 full protocol conversion | Master issues 1000 randomised single AXI writes to randomised addresses; NoC stub captures flits; verify flit content matches AXI request. | All AXI4 AW/W rules; NOC_FLIT_HDR_*; AXI4_SLV_XCH_W_AFTER_AW; AXI4_SLV_XCH_B_AFTER_AW_AND_W |
| TP2 | AXI4 full protocol conversion | Same for AXI reads. | AXI4 AR/R rules; XCH_R_AFTER_AR; XCH_R_LAST_CONSISTENT |
| TP3 | AXI4 burst handling | Burst writes (awlen ∈ {1, 7, 15}); verify N+1 W flits per burst, all carrying same axi_id; verify single B response with correct id. | AXI4_MST_AW_AWLEN_STABLE; NOC_FLIT_AW_W_ORDER |
| TP4 | AXI4 burst handling | Burst reads; verify wormhole-locked R flit sequence; final beat carries RLAST=1. | AR/R rules; XCH_R_LAST_CONSISTENT |
| TP5 | RoB Normal mode (NormalRoB) | Issue 32 outstanding reads with mixed axi_id; randomize NoC response order; verify per-id in-order release at AXI; verify cross-id reordering. | AXI4_MST_RoB_PER_ID_ORDER |
| TP6 | RoB Simple mode (SimpleRoB) | Same with SimpleRoB; verify FIFO ordering across all txnIDs (different IDs serialised). | RoB_PER_ID_ORDER |
| TP7 | RoB NoRoB mode | Single-outstanding; verify next request stalls until previous completes. | RoB_OUTSTANDING_LIMIT |
| TP8 | flit_ecc single-bit corrected on W | Inject 1-bit error in W flit at NMU egress (`set_inject_ecc_error(W, SINGLE_BIT)`); verify NSU sink corrects silently, increments `ECC_CORR_ERR_CNT` (saturating, no clear path), forwards corrected data to local AXI slave with `bresp=OKAY`; verify `ECC_UNCORR_ERR_CNT`, `ERR_STATUS[0]`, `LAST_ERR_INFO`, `irq_o` all unchanged. | NOC_FLIT_HDR_FLIT_ECC_GEN; NOC_FLIT_HDR_FLIT_ECC_CHECK |
| TP9 | flit_ecc double-bit forwarded with logging on W | Inject 2-bit error in W flit at NMU egress (`set_inject_ecc_error(W, DOUBLE_BIT)`); verify NSU sink detects, forwards the corrupted flit to local AXI slave **with `bresp=OKAY`** (NoC fabric does NOT synthesise SLVERR from this check), increments `ECC_UNCORR_ERR_CNT`, sets `ERR_STATUS[0] ecc_uncorr_err`, captures `LAST_ERR_INFO` if first sticky, asserts `irq_o` if `IRQ_ENABLE[0]=1`. RW1C-clear ERR_STATUS[0] then verify counter and bit clear together; verify `irq_o` deasserts when last set+enabled bit clears. | NOC_FLIT_HDR_FLIT_ECC_CHECK; NI_CFG_ERR_STATUS_RW1C; NI_IRQ_LEVEL |
| TP10 | flit_ecc single-bit corrected + double-bit forwarded on R | Mirror of TP8/TP9 on R direction (`set_inject_ecc_error(R, SINGLE_BIT)` and `set_inject_ecc_error(R, DOUBLE_BIT)`). NMU sink corrects single-bit silently + ECC_CORR_ERR_CNT++; double-bit forwarded to AXI master with `rresp=OKAY` + ECC_UNCORR_ERR_CNT++ + ERR_STATUS[0] + LAST_ERR_INFO + irq_o (if enabled). For multi-beat R bursts: only the affected beat carries the corrupted data; other beats are unaffected and rresp=OKAY across the whole burst. | NOC_FLIT_HDR_FLIT_ECC_CHECK; NI_CFG_ERR_STATUS_RW1C; NI_IRQ_LEVEL |
| TP11 | QoS Bypass mode | `QOS_MODE = 0`; verify flit header qos == AXI awqos / arqos directly. | NI_CFG_QOS_MODE_TRANSITION |
| TP12 | QoS Fixed mode | `QOS_MODE = 1`, set QOS_FIXED_VALUE = 7; verify all flits have qos = 7 regardless of AXI awqos. | NI_CFG_QOS_MODE_TRANSITION |
| TP13 | QoS Limiter mode | `QOS_MODE = 2`, configure BANDWIDTH_LIMIT and SATURATION_THRESHOLD; issue traffic at 2× the limit; verify qos drops to LOW_PRIORITY when threshold exceeded. | NI_CFG_BANDWIDTH_LIMIT_BOUND |
| TP14 | QoS Regulator mode | `QOS_MODE = 3`, configure BANDWIDTH_BUDGET; observe response bandwidth slow → urgency rises → qos rises; observe response bandwidth high → urgency drops → qos drops. | NI_CFG_BANDWIDTH_BUDGET_BOUND |
| TP15 | QoS Saturation | Regulator mode with BASE_QOS=12; force urgency to MAX; verify final qos clamped at 15 (not wrap). | (none specific) |
| TP16 | Packet Probe | Configure `PKT_PROBE_EN`, `PKT_PROBE_MODE`, `PKT_WINDOW_SIZE`; issue known-bandwidth traffic; verify `PKT_BYTE_COUNT` and `PKT_BANDWIDTH` match expected. | NI_CFG_PROBE_EN_TRANSITION |
| TP17 | Transaction Probe | Configure thresholds; issue traffic with various round-trip latencies; verify each TXN_BIN_*_COUNT receives expected number of transactions. | (none specific) |
| TP18 | ERR_STATUS RW1C across all 3 bits | For each i ∈ {0..2}: trigger the corresponding event class (ECC double-bit on W → bit 0; inject route_par mismatch on a request flit → bit 1; corrupt `axi_wdata_par_i` on an AW handshake → bit 2). Verify `ERR_STATUS[i]` is set, the paired counter increments (ECC_UNCORR_ERR_CNT, ROUTE_PAR_ERR_CNT, AXI_PARITY_ERR_CNT respectively). Software writes 1 to `ERR_STATUS[i]`; verify bit and counter cleared atomically; verify other ERR_STATUS bits + counters are unaffected. | NI_CFG_ERR_STATUS_RW1C |
| TP19 | LAST_ERR_INFO sticky capture across 3 event classes | Trigger error A from event class X (e.g., ECC uncorr); verify `LAST_ERR_INFO` captures A's err_axi_id/src/dst. Trigger error B from event class Y ≠ X (e.g., AXI parity) without clearing; verify `LAST_ERR_INFO` still shows A (sticky regardless of class). Software writes 1 to the corresponding `ERR_STATUS[X]`; trigger error C from any class; verify `LAST_ERR_INFO` now shows C. Repeat with all (X, Y) pairs from {0..2} × {0..2}. | NI_CFG_LAST_ERR_INFO_CAPTURE; NI_CFG_ERR_STATUS_RW1C |
| TP20 | CDC at fast aclk | Set aclk_freq = 2 × noc_clk_freq; issue burst traffic; verify no flit loss, no order corruption across CDC. | NI_CDC_AXI_TO_NOC_FIFO; NI_CDC_NOC_TO_AXI_FIFO |
| TP21 | CDC at slow aclk | Set aclk_freq = 0.1 × noc_clk_freq; same. | Same |
| TP22 | CDC at equal clocks | aclk_freq = noc_clk_freq; same; verify FIFOs degenerate to direct paths but still function. | Same |
| TP23 | Reset during AXI AW phase | Master raises awvalid; arst_ni asserts before awready; verify NMU returns to IDLE; verify any cross-domain in-flight is cleaned up by NoC-side draining. | NI_RST_OUTPUTS_LOW_AXI |
| TP24 | Reset during NoC injection | Mid-flit injection on `noc_req_o`; noc_rst_ni asserts; verify noc_req_valid_o drops to 0 same cycle. | NI_RST_OUTPUTS_LOW_NOC |
| TP25 | Reset during multi-beat R burst | Master in-flight reading; noc_rst_ni asserts mid-burst; verify R beats stop on AXI side; verify the in-flight RoB entry's pending beats are dropped (RoB entry returned to FREE on noc_rst_ni release). On subsequent reset deassertion, master can re-issue the read. AXI side does NOT see rresp=SLVERR for partial-reset case — instead sees no further R beats and the master's transaction times out per master DUT's logic. | NI_RST_PARTIAL |
| TP26 | Partial reset (only one of two resets) | Assert only `arst_ni`; verify NoC side continues operating but cross-domain transactions stall. | NI_RST_PARTIAL |
| TP27 | Mode switch ACTIVE→PASSIVE | Issue traffic; mid-burst, switch to PASSIVE; verify all BFM-driven outputs transition to during-reset values within 1 cycle; verify in-flight transactions return MODE_SWITCHED_TO_PASSIVE. | NI_CFG_MODE_SWITCH |
| TP28 | Mode switch PASSIVE→ACTIVE | Switch back; verify outputs return to active state; new traffic flows. | Same |
| TP29 | NMU-only configuration | `EN_MGR_PORT=1, EN_SBR_PORT=0`; verify only NMU-related signals operate; NSU-related signals tied to inactive defaults. | (none specific) |
| TP30 | NSU-only configuration | Mirror. | Same |

### Additional integration testpoints (system-level)

| ID | Feature | Testpoint description | Protocol rules exercised |
|----|---------|------------------------|--------------------------|
| TP31 | End-to-end AXI4 over NoC, 2-NI loopback | NMU at node A, NSU at node B; AXI master at A issues writes; verify NSU at B drives correct writes to local AXI slave; reverse path tested with reads. | All AXI4 + NoC rules in combination |
| TP32 | Wormhole-route deadlock prevention | Two simultaneous bursts contending on same router output; verify QoS arbitration prevents starvation; verify no deadlock under all permutation combinations. | NI_CFG_QOS_MODE_TRANSITION + Router-side rules (separate spec) |
| TP33 | Cross-traffic during mode switch | Mid-test ACTIVE→PASSIVE on NI A while NI B still actively transacting; verify NI B's traffic unaffected; verify NI A's wires float. | CFG_MODE_SWITCH |
| TP34 | NMU-only / NSU-only configurations | Build BFM with `EN_MGR_PORT=1, EN_SBR_PORT=0`; verify NSU signals tied to inactive defaults; mirror with NSU-only. Coverage of `D1.bfm.signal_interface` parameter constraints. | (configuration-only, no protocol rule directly) |
| TP35 | Probe accuracy under sustained load | Configure PKT_PROBE_EN with various PKT_WINDOW_SIZE; issue traffic at known bandwidth; verify reported PKT_BANDWIDTH within ±5% of actual (target accuracy). | NI_CFG_PROBE_PKT_BYTE_COUNT |
| TP36 | Long-tail latency capture | Configure TXN_PROBE thresholds (e.g., 10/100/1000/10000 cycles); inject long-tail-latency traffic; verify all 5 bins populated correctly. | NI_CFG_PROBE_TXN_LATENCY |
| TP37 | RoB exhaustion / back-pressure | Issue MAX_TXNS+1 outstanding transactions in rapid succession; verify NMU asserts back-pressure on awready / arready until RoB slot frees; verify no transaction loss. | AXI4_MST_RoB_OUTSTANDING_LIMIT |
| TP38 | RoB FREE entry allocation policy | Issue 5 transactions to RoB entries 0-4; complete entry 2 first; issue new transaction; verify it allocates to entry 2 (lowest-index-FREE-first per ToO §RoB allocator). | (none specific; ToO §RoB) |
| TP39 | RoB tie-breaking on simultaneous READY | Issue 2 transactions same axi_id (back-to-back); arrange responses to arrive simultaneously; verify lower rob_idx releases first (per ToO §RoB tie-breaking). | AXI4_MST_RoB_PER_ID_ORDER |
| TP40 | AR-during-W interleaving | Start a long W burst; mid-burst issue an AR; verify AR flit injected on noc_req_o between W flits; verify NSU correctly dispatches both. | NOC_FLIT_AW_W_ORDER + AR ordering per ToO |
| TP41 | route_par drop (silent AXI hang in v0.4.0) | Inject a single-bit corruption into a request flit's `route_par`-protected fields (`dst_id` / `last`, per AMD pg313 §Parity coverage) on `noc_req_o` egress (e.g., via stub router); verify the receiving router or NSU sink drops the flit, increments `ROUTE_PAR_ERR_CNT`, sets `ERR_STATUS[1]`, captures `LAST_ERR_INFO` (if first sticky). Set `IRQ_ENABLE[1]=1` and verify `irq_o` asserts. The originating AXI master transaction hangs silently — no automatic SLVERR synthesis in v0.4.0. Test framework upper-bounded watchdog detects the hang and validates the test case. | NOC_FLIT_HDR_ROUTE_PAR_GEN; NOC_FLIT_HDR_ROUTE_PAR_CHECK |
| TP42 | IRQ assert/deassert + IRQ_ENABLE mask + RW1C interaction | (a) With `IRQ_ENABLE = 0x0`, trigger each of the 3 ERR_STATUS event classes; verify `irq_o` stays LOW even though ERR_STATUS bits set and counters increment (mask works). (b) With `IRQ_ENABLE = 0x7`, trigger one event class at a time; verify `irq_o` rises on the event cycle (after CSR-CDC sync delay where applicable) and falls on the cycle the matching `ERR_STATUS[i]` is RW1C-cleared. (c) With multiple ERR_STATUS bits set + multiple IRQ_ENABLE bits set, verify `irq_o` stays HIGH until ALL set+enabled bits are cleared; verify partial clear keeps `irq_o` HIGH. (d) Edge case: set ERR_STATUS bit, then set the matching IRQ_ENABLE bit; verify `irq_o` asserts on the IRQ_ENABLE write cycle (level-sensitive, no edge requirement). | NI_IRQ_LEVEL; NI_CFG_ERR_STATUS_RW1C |
| TP43 | AXI host-side parity error logging (data + addr, both directions) | (a) NMU side: with `ENABLE_AXI_PARITY=true` (default), drive `axi_awvalid_i=1` with corrupted `axi_awaddr_par_i` (parity flipped); verify NMU logs `ERR_STATUS[2]`, `AXI_PARITY_ERR_CNT++`, `LAST_ERR_INFO` capture, and the AW transaction proceeds (no SLVERR injected at AXI boundary). Repeat for `axi_araddr_par_i` and `axi_wdata_par_i[byte]`. (b) NSU side: drive `axi_rvalid_i=1` from local slave with corrupted `axi_rdata_par_i[byte]`; verify NSU logs the same way and forwards the R beat to the originating AXI master with `rresp=OKAY` (no SLVERR). (c) Cross-check: set `ENABLE_AXI_PARITY=false` at instantiation; verify the parity wires are absent and TP43 a/b cannot be exercised (parameter sanity test). | AXI4_MST_PARITY_CHECK; AXI4_SLV_PARITY_CHECK |
| TP44 | NMU quiesce flow nominal | Issue 16 outstanding mixed AW/AR transactions on the NMU manager port; while in-flight, software writes `QUIESCE_CTRL.quiesce_req=1`. Verify NMU back-pressures `axi_awready_o = axi_arready_o = 0` for any new AW/AR; verify in-flight transactions complete normally (responses arrive at AXI master); verify `PENDING_R_COUNT` / `PENDING_W_COUNT` decrement to 0 as responses are consumed; verify `QUIESCE_STATUS.quiesce_idle` asserts on the cycle both PENDING counters reach 0. Software writes `quiesce_req=0`; verify NMU resumes accepting AW/AR on the next cycle; verify `quiesce_idle` deasserts the same cycle. | NI_CFG_QUIESCE_FLOW; NI_CFG_PENDING_COUNT_ACCURACY |
| TP46 | Quiesce scope (NMU-only, NSU continues) | Set `quiesce_req=1` on NI A. From a remote NI B, issue AXI traffic that arrives at A's NSU via NoC (NMU at B → router fabric → NSU at A → A's local AXI slave). Verify NI A's NSU continues to drive `axi_*_o` to its local AXI slave normally — quiesce does NOT stop NSU. Verify `quiesce_idle` reflects ONLY NMU-side outstanding (PENDING_R/W_COUNT); NSU-side activity does NOT affect the bit. | NI_CFG_QUIESCE_FLOW |
| TP47 | Exclusive Monitor clear_all (no race) | Allocate 5 Exclusive AR reservations on NSU (different `axi_id`); verify `EXCLUSIVE_MONITOR_STATUS.occupancy = 5`. Software writes `EXCLUSIVE_MONITOR_CTRL.clear_all = 1`; verify on the next aclk edge: occupancy = 0; subsequent Exclusive AW from any of the 5 master IDs misses the monitor → bresp=OKAY (downgraded to normal write); read-back of `EXCLUSIVE_MONITOR_CTRL.clear_all` returns 0 (self-cleared on the next `aclk_i` edge after the CSR-write completed). | NI_CFG_EXCLUSIVE_CLEAR_RACE; NI_CFG_EXCLUSIVE_OCCUPANCY_ACCURACY |
| TP48 | Exclusive clear race semantics | Drive simultaneous events on the same `aclk_i` cycle as the `clear_all` CSR write handshake completes: (a) Exclusive AW match check on NSU → verify match check uses **pre-clear** state (the AW that arrived in the same cycle proceeds against the entry that was alive at start-of-cycle; if matched, EXOKAY); (b) new Exclusive AR allocation → verify the new entry survives clear (post-clear allocation); (c) overlap-invalidate triggered by a normal write → verify idempotent (entry invalidated either way). Cover all three race types in directed cycles. | NI_CFG_EXCLUSIVE_CLEAR_RACE |
| TP49 | PENDING_*_COUNT and Exclusive occupancy accuracy under load | Issue mixed single-beat + burst AW/AR traffic across the manager port concurrently with Exclusive AR/AW activity at NSU. At every aclk cycle, sample `PENDING_R_COUNT`, `PENDING_W_COUNT`, and `EXCLUSIVE_MONITOR_STATUS.occupancy` via CSR readback; cross-check against scoreboard tracking AW/AR/B/R handshake events at `axi_*_i` (for PENDING) and Exclusive AR/AW/clear events at NSU (for occupancy). Tolerance: counters MUST match scoreboard exactly on the cycle the read handshake completes (no CDC slack — all three counters are aclk-domain). | NI_CFG_PENDING_COUNT_ACCURACY; NI_CFG_EXCLUSIVE_OCCUPANCY_ACCURACY |
| TP50 | NMU R-direction parity regeneration (`axi_rdata_par_o`) | (a) Issue a normal AXI read with `ENABLE_AXI_PARITY=true`. Verify NMU drives `axi_rdata_par_o[DATA_WIDTH/8-1:0]` byte-wise correctly: each parity bit equals XOR-reduction of the corresponding `axi_rdata_o` byte (even-parity convention); regeneration occurs **after** the `flit_ecc` check stage. AXI master verifies parity, sees no mismatch. (b) Inject `flit_ecc` 1-bit error on the R flit (`set_inject_ecc_error(R, SINGLE_BIT)`); verify NMU corrects the bit silently and regenerates `axi_rdata_par_o` over the **corrected** data — master sees consistent data + parity. (c) Inject `flit_ecc` 2-bit error (`set_inject_ecc_error(R, DOUBLE_BIT)`); verify NMU forwards the corrupted `axi_rdata_o` with `rresp=OKAY` AND regenerates parity over the **corrupted** wire data — master can independently detect via parity check. Verify simultaneous `ECC_UNCORR_ERR_CNT++` + `ERR_STATUS[0]` + `irq_o` (if `IRQ_ENABLE[0]=1`) triggered by the ECC path. | AXI4_MST_PARITY_GEN_R; NOC_FLIT_HDR_FLIT_ECC_CHECK |
| TP51 | NMU credit-based flit injection contract | (a) Normal: with per-VC credit > 0 on the chosen VC, issue an AXI request; verify NMU drives `noc_req_valid_o=1` on the egress cycle and the per-VC credit counter decrements by 1. (b) Credit starvation: withhold all `noc_req_credit_i` returns to drain the per-VC credit pool to 0; issue a fresh AXI AW/AR; verify NMU does NOT assert `noc_req_valid_o`. The stall is permanent — no automatic escalation to SLVERR in v0.4.0. (c) Recovery: return one credit on `noc_req_credit_i[chosen_vc]`; verify NMU resumes flit injection on the next available cycle and decrements the counter again. (d) Per-VC isolation: starve VC 0 only, leave VC 1 with credits; verify NMU still injects flits whose `vc_id` maps to VC 1 (Hybrid R/W × QoS mapping per `NOC_VC_MAPPING_HYBRID_RW_QOS`). | NOC_MST_FLIT_ON_CREDIT_ONLY; NOC_VC_MAPPING_HYBRID_RW_QOS |

## Coverage model

Covergroups, each binned across the rules / scenarios it exercises:

- **cg_axi_handshake_aw / w / b / ar / r** — bins across (VALID-before-READY, READY-before-VALID, simultaneous), (AWLEN ∈ {0, 1-7, 8-15, 16+}), (id ∈ {0..MAX_UNIQUE_IDS-1}).
- **cg_noc_handshake_req_out / rsp_out / req_in / rsp_in** — bins across (valid-before-ready, ready-before-valid, simultaneous), (consecutive-back-to-back, intermittent).
- **cg_rob_state_machine** — bins per RoB Entry State (FREE / ALLOCATED / RESPONSE_RECEIVED / READY_TO_RELEASE), per RoB type (Normal / Simple / NoRoB).
- **cg_qos_modes** — bins across (Bypass, Fixed, Limiter at <threshold, Limiter ≥threshold, Regulator urgency=0, Regulator urgency mid, Regulator urgency=MAX).
- **cg_qos_clamp** — Regulator BASE_QOS + urgency at boundary (clamp to 15) and SOCKET_QOS lift (≥SOCKET_QOS).
- **cg_ecc** — bins across (no error, 1-bit corrected on W, 1-bit corrected on R, 2-bit forwarded-with-log on W, 2-bit forwarded-with-log on R, route_par drop on request flit, route_par drop on response flit). Note: 2-bit cases verify forward+log behaviour (NoC fabric does NOT synthesise SLVERR per `NOC_FLIT_HDR_FLIT_ECC_CHECK`).
- **cg_axi_parity** — bins across (no error, NMU-side awaddr-byte parity error, NMU-side araddr-byte parity error, NMU-side wdata-byte parity error, NSU-side rdata-byte parity error). All check paths log to `ERR_STATUS[2]` + `AXI_PARITY_ERR_CNT`; AXI rresp/bresp unchanged. Plus `axi_rdata_par_o` regeneration bins (NMU R-direction): no-error / 1-bit-flit_ecc-corrected / 2-bit-flit_ecc-uncorrectable cases — verify parity reflects on-the-wire data, not pre-correction (per `AXI4_MST_PARITY_GEN_R`).
- **cg_probe_packet** — bins across modes (Combined, Read, Write) and window-overlap scenarios.
- **cg_probe_txn** — bins across each latency bin coverage.
- **cg_err_status** — bins across (none, single class set [each of 3], two-class combinations, all-3 set, partial-clear-via-RW1C, full-clear-via-RW1C).
- **cg_irq** — bins across (mask-all-no-irq, single-bit-set-with-enable-asserts-irq [each of 3], multi-bit-set-with-partial-mask, irq-deassert-on-last-clear, irq-rise-on-`IRQ_ENABLE`-write [late mask enable]).
- **cg_cdc_clock_ratio** — bins across aclk:noc_clk ratios (1:10, 1:2, 1:1, 2:1, 10:1).
- **cg_reset_phase** — bins across (reset during AW, W, B, AR, R, idle), partial-reset variants.
- **cg_mode_switch** — bins across (ACTIVE→PASSIVE during AXI traffic, ACTIVE→PASSIVE during NoC traffic, ACTIVE→PASSIVE idle, PASSIVE→ACTIVE).
- **cg_quiesce** — bins across (idle [no quiesce], drain-in-progress [quiesce_req=1, pending>0], drain-complete [quiesce_req=1, pending=0, idle=1], resume [quiesce_req=0 after idle]).
- **cg_exclusive_clear** — bins across (clear-in-isolation, clear+concurrent-AW-match, clear+concurrent-AR-alloc, clear+concurrent-overlap-invalidate); plus pre-clear-occupancy bin (1, EXCLUSIVE_MONITOR_DEPTH/2, EXCLUSIVE_MONITOR_DEPTH).
- **cg_pending_count** — bins across `PENDING_R_COUNT` values (0, 1, MAX_TXNS/2, MAX_TXNS) × `PENDING_W_COUNT` values (same set). Cross-coverage to verify no off-by-one between NMU tracker view and CSR readback.
- **cg_protocol_rule_hits** — one cover-property per rule ID in protocol_rules.md. Final count: **136 rule IDs** post-A5 (across all sections; canonical `grep -c '^| AXI4\\|^| NOC\\|^| NI\\|^| AXI4LITE' protocol_rules.md`). One cover bin per rule. Detailed breakdown: 5 RST + 3 CDC + 11 AW + 7 W + 6 B + 10 AR + 9 R + 4 XCH + 5 RoB + 3 Exclusive + 3 AXI parity + 3 NoC handshake (credit-based) + 13 NoC flit + 4 ECC + 4 VC + 2 Width-conv + 24 AXI4-Lite CSR + 19 CFG + 1 IRQ = 136. (A5 wave: removed `AXI4_MST_TIMEOUT_SLVERR` (Timeout 1→0) and `NI_CFG_QUIESCE_LIVENESS` (CFG 20→19) as part of Outstanding-tx Timeout feature deletion. A4.7 baseline: 138 rules.)

D3 coverage closure goal: 100% bin hits on every covergroup.

## ABV / FPV strategy

**ABV** — every FAIL-severity row in protocol_rules.md gets one SVA `assert property` in the testbench. Post-A5 canonical count: **126 FAIL-severity assertions** + **10 RECOMMEND cover-properties** (RECOMMEND family: `NI_RST_PARTIAL`, `AXI4_MST_AW_AWCACHE_STABLE`, `AXI4_MST_AR_ARCACHE_STABLE`, `NOC_FLIT_HDR_RSVD_IGNORE_RX`, `NOC_VC_PARTITION`, `AXI4LITE_SLV_RO_WRITE_IGNORED`, `NI_CFG_RESPONSE_DELAY_AXI`, `NI_CFG_RESPONSE_DELAY_NOC`, `NI_CFG_INJECT_ECC_ERROR`, `NI_CFG_RESPONSE_FAULT`). Total ABV library size: 136 properties.

**FPV** — formal verification scope:
- RoB allocator state machine (FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE) — verify no deadlock, no entry stuck, per-id ordering.
- flit_ecc SECDED Hamming gen + check round-trip — formally verify single-bit correction for all single-bit error patterns. Verify double-bit detection for representative patterns (full enumeration is infeasible at 406-bit flit, sampled). Verify the (B)-philosophy invariant: when SECDED reports double-bit, the flit is forwarded **without** modification of the AXI rresp/bresp value.
- route_par parity — formally verify XOR-reduction over `{dst_id, last}` (per AMD pg313 §Parity NPP packet DST ID + LAST coverage) and the drop-on-mismatch behaviour.
- IRQ assertion function — formally verify `irq_o = OR_i(ERR_STATUS[i] & IRQ_ENABLE[i])` is purely combinational (no glitches under simultaneous bit transitions, no deadlock between RW1C clear and re-assert).
- CDC async FIFO — verify no data loss / corruption / pointer divergence across all clock-ratio extremes.
- Reset entry sequencing — verify the wire-level reset values and post-reset transitions match pin_level_reset.md formally.

**Security role**: NI does NOT participate in security-critical paths (access control, attestation, key management). Sec_cm FPV is **not required**. AXI awprot/arprot are sampled but not enforced; protection-attribute checking is delegated to downstream slaves or upstream IP. **Designer-confirmed (A5 wave 2026-05-08): NI is not security-critical; no sec_cm FPV required. Revisit if a future revision adds security gating.**

## Out of scope

- AXI4 atomic operations (ATOPs) — explicitly out of scope per ToO §ATOPs scope. ATOP transactions terminate with `bresp=SLVERR`; not exercised by DV beyond a single negative testpoint (TP_NEG_ATOP: send ATOP write, verify SLVERR + counter increment).
- AXI5 features (CHI-derived, cache-coherent).
- Multi-NI integration (cross-node ordering across the mesh) — handled at system-level DV, not at this NI's unit DV.
- Router DV — separate spec.
- System-level deadlock testing — see system DV.
- Power / clock-gating verification — separate concern.
