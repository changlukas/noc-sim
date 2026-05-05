# DV Plan

## Verification scope

Verify the NI against:
1. AXI4 protocol compliance (host side, both manager and subordinate ports + AXI4-Lite for CSR access).
2. NoC flit protocol compliance (per protocol_rules.md `NOC_*` rules).
3. Cross-protocol transformation correctness (AXI ↔ flit packing/unpacking, ECC end-to-end).
4. RoB ordering invariants (per-AXI-ID).
5. QoS Generator modes (Bypass / Fixed / Limiter / Regulator) functional behavior.
6. Performance Probe accuracy (Packet bandwidth statistics, Transaction latency histogram).
7. Error monitoring CSRs (saturating counters, ERR_STATUS write-1-to-clear, LAST_ERR_INFO atomic capture).
8. Dual-clock-domain CDC correctness (no metastability, no data loss across aclk ↔ noc_clk).
9. Reset behavior (per pin_level_reset.md, including partial-reset edge cases).
10. Mode switch (ACTIVE / PASSIVE).

DV strategy:
- **Constrained-random testing** — **UVM 1.2** (industry standard; mature DV ecosystem; matches assumed in-house DV expertise). Master DUT stimulates AXI; NoC router stub provides flit endpoint; scoreboard cross-checks AXI handshakes against observed NoC flits. *Reviewer assumption: confirm or override (cocotb if Python-driven flow preferred; plain SV if UVM overhead unwanted).*
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
| TP8 | SECDED ECC end-to-end | Inject 1-bit error in W flit at NMU output (`set_inject_ecc_error(W, SINGLE_BIT)`); verify NSU corrects silently; verify `ECC_UNCORR_ERR_CNT` does NOT increment; verify `ECC_CORR_ERR_CNT` (new register at 0x110 per ToO §ECC implementation) DOES increment. | NOC_ECC_W_GEN; NOC_ECC_W_CHECK |
| TP9 | SECDED ECC end-to-end | Inject 2-bit error in W flit; verify NSU detects + propagates to AXI `bresp = SLVERR` + increments `ECC_UNCORR_ERR_CNT`. | NOC_ECC_W_CHECK; AXI4_SLV_B_BRESP_VALUES |
| TP10 | SECDED ECC end-to-end | Inject 1-bit and 2-bit errors in R flit; verify NMU corrects/detects and propagates correctly. | NOC_ECC_R_CHECK |
| TP11 | QoS Bypass mode | `QOS_MODE = 0`; verify flit header qos == AXI awqos / arqos directly. | NI_CFG_QOS_MODE_TRANSITION |
| TP12 | QoS Fixed mode | `QOS_MODE = 1`, set QOS_FIXED_VALUE = 7; verify all flits have qos = 7 regardless of AXI awqos. | NI_CFG_QOS_MODE_TRANSITION |
| TP13 | QoS Limiter mode | `QOS_MODE = 2`, configure BANDWIDTH_LIMIT and SATURATION_THRESHOLD; issue traffic at 2× the limit; verify qos drops to LOW_PRIORITY when threshold exceeded. | NI_CFG_BANDWIDTH_LIMIT_BOUND |
| TP14 | QoS Regulator mode | `QOS_MODE = 3`, configure BANDWIDTH_BUDGET; observe response bandwidth slow → urgency rises → qos rises; observe response bandwidth high → urgency drops → qos drops. | NI_CFG_BANDWIDTH_BUDGET_BOUND |
| TP15 | QoS Saturation | Regulator mode with BASE_QOS=12; force urgency to MAX; verify final qos clamped at 15 (not wrap). | (none specific) |
| TP16 | Packet Probe | Configure `PKT_PROBE_EN`, `PKT_PROBE_MODE`, `PKT_WINDOW_SIZE`; issue known-bandwidth traffic; verify `PKT_BYTE_COUNT` and `PKT_BANDWIDTH` match expected. | NI_CFG_PROBE_EN_TRANSITION |
| TP17 | Transaction Probe | Configure thresholds; issue traffic with various round-trip latencies; verify each TXN_BIN_*_COUNT receives expected number of transactions. | (none specific) |
| TP18 | ERR_STATUS RW1C | Trigger ECC uncorrectable; verify `ERR_STATUS[0]` set; verify `ECC_UNCORR_ERR_CNT` increments; software writes 1 to ERR_STATUS[0]; verify both bit and counter cleared atomically. | NI_CFG_ERR_STATUS_RW1C |
| TP19 | LAST_ERR_INFO sticky capture | Trigger error A; verify `LAST_ERR_INFO` captures A's err_axi_id/src/dst. Trigger error B without clearing; verify `LAST_ERR_INFO` still shows A (sticky semantics per `NI_CFG_LAST_ERR_INFO_CAPTURE` rule). Software writes 1 to `ERR_STATUS[0]`; trigger error C; verify `LAST_ERR_INFO` now shows C. | NI_CFG_LAST_ERR_INFO_CAPTURE; NI_CFG_ERR_STATUS_RW1C |
| TP20 | CDC at fast aclk | Set aclk_freq = 2 × noc_clk_freq; issue burst traffic; verify no flit loss, no order corruption across CDC. | NI_CDC_AXI_TO_NOC_FIFO; NI_CDC_NOC_TO_AXI_FIFO |
| TP21 | CDC at slow aclk | Set aclk_freq = 0.1 × noc_clk_freq; same. | Same |
| TP22 | CDC at equal clocks | aclk_freq = noc_clk_freq; same; verify FIFOs degenerate to direct paths but still function. | Same |
| TP23 | Reset during AXI AW phase | Master raises awvalid; arst_ni asserts before awready; verify NMU returns to IDLE; verify any cross-domain in-flight is cleaned up by NoC-side draining. | NI_RST_OUTPUTS_LOW_AXI |
| TP24 | Reset during NoC injection | Mid-flit injection on `noc_req_o`; noc_rst_ni asserts; verify noc_req_o.valid drops to 0 same cycle. | NI_RST_OUTPUTS_LOW_NOC |
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

## Coverage model

Covergroups, each binned across the rules / scenarios it exercises:

- **cg_axi_handshake_aw / w / b / ar / r** — bins across (VALID-before-READY, READY-before-VALID, simultaneous), (AWLEN ∈ {0, 1-7, 8-15, 16+}), (id ∈ {0..MAX_UNIQUE_IDS-1}).
- **cg_noc_handshake_req_out / rsp_out / req_in / rsp_in** — bins across (valid-before-ready, ready-before-valid, simultaneous), (consecutive-back-to-back, intermittent).
- **cg_rob_state_machine** — bins per RoB Entry State (FREE / ALLOCATED / RESPONSE_RECEIVED / READY_TO_RELEASE), per RoB type (Normal / Simple / NoRoB).
- **cg_qos_modes** — bins across (Bypass, Fixed, Limiter at <threshold, Limiter ≥threshold, Regulator urgency=0, Regulator urgency mid, Regulator urgency=MAX).
- **cg_qos_clamp** — Regulator BASE_QOS + urgency at boundary (clamp to 15) and SOCKET_QOS lift (≥SOCKET_QOS).
- **cg_ecc** — bins across (no error, 1-bit corrected on W, 1-bit corrected on R, 2-bit uncorrected on W, 2-bit uncorrected on R).
- **cg_probe_packet** — bins across modes (Combined, Read, Write) and window-overlap scenarios.
- **cg_probe_txn** — bins across each latency bin coverage.
- **cg_err_status** — bins across (none, ecc_uncorr only, timeout only, both, write-1-clear).
- **cg_cdc_clock_ratio** — bins across aclk:noc_clk ratios (1:10, 1:2, 1:1, 2:1, 10:1).
- **cg_reset_phase** — bins across (reset during AW, W, B, AR, R, idle), partial-reset variants.
- **cg_mode_switch** — bins across (ACTIVE→PASSIVE during AXI traffic, ACTIVE→PASSIVE during NoC traffic, ACTIVE→PASSIVE idle, PASSIVE→ACTIVE).
- **cg_protocol_rule_hits** — one cover-property per rule ID in protocol_rules.md. Final count: **~95 rule IDs** across all sections (5 RST + 3 CDC + 11 AW + 8 W + 7 B + 11 AR + 9 R + 5 XCH + 2 RoB + 3 NoC handshake + 4 NoC flit + 4 NoC ECC + 21 AXI4-Lite CSR + 9 CFG = ~95). One cover bin per rule.

D3 coverage closure goal: 100% bin hits on every covergroup.

## ABV / FPV strategy

**ABV** — every FAIL-severity row in protocol_rules.md gets one SVA `assert property` in the testbench. Final count: **~85 FAIL-severity assertions** + **~10 RECOMMEND cover-properties**. Total ABV library size: ~95 properties.

**FPV** — formal verification scope:
- RoB allocator state machine (FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE) — verify no deadlock, no entry stuck, per-id ordering.
- ECC SECDED Hsiao gen + check round-trip — formally verify single-bit correction for all single-bit error patterns; double-bit detection for representative patterns (full enumeration is infeasible at 256-bit data, sampled).
- CDC async FIFO — verify no data loss / corruption / pointer divergence across all clock-ratio extremes.
- Reset entry sequencing — verify the wire-level reset values and post-reset transitions match pin_level_reset.md formally.

**Security role**: NI does NOT participate in security-critical paths (access control, attestation, key management). Sec_cm FPV is **not required**. AXI awprot/arprot are sampled but not enforced; protection-attribute checking is delegated to downstream slaves or upstream IP. *Reviewer assumption: confirm — if a future revision adds security gating (e.g., NMU enforces awprot[1]=0 for non-secure transactions), revisit and add sec_cm FPV.*

## Out of scope

- AXI4 atomic operations (ATOPs) — explicitly out of scope per ToO §ATOPs scope. ATOP transactions terminate with `bresp=SLVERR`; not exercised by DV beyond a single negative testpoint (TP_NEG_ATOP: send ATOP write, verify SLVERR + counter increment).
- AXI5 features (CHI-derived, cache-coherent).
- Multi-NI integration (cross-node ordering across the mesh) — handled at system-level DV, not at this NI's unit DV.
- Router DV — separate spec.
- System-level deadlock testing — see system DV.
- Power / clock-gating verification — separate concern.
