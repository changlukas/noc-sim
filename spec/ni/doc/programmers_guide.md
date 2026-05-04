# Programmer's Guide

`TODO(designer):` This entire file is mostly placeholder. The source documents focus on hardware behavior and have **no software-perspective initialization sequence, no use-case walkthroughs, and no interrupt servicing flow**. Software documentation for `ni` must be authored from designer knowledge before D1.

The skeletons below capture the structure and known facts; they do not constitute a complete programmer's guide.

## Initialization

`TODO(designer):` Define the verified post-reset software initialization sequence. The sequence below is a **plausible draft** based on the register set; it has not been verified by DV and may be incorrect.

```c
// 1. Bring NI to a clean state.
//    TODO(designer): Is there a software-issued NI reset, or only system rst_ni?
//    Source describes no software reset register.

// 2. Clear any residual error state.
//    TODO(designer): Source contradicts itself on ERR_STATUS access (RO vs W1C).
//    Resolve in registers.md before specifying step here.

// 3. Configure QoS Generator. Default mode is Bypass (= 0); other modes
//    require corresponding parameters to be programmed BEFORE switching mode.
write(QOS_MODE, 0);                  // Bypass: pass through awqos/arqos

// 4. (optional) Enable Packet Probe.
write(PKT_WINDOW_SIZE, 0x1000);      // 4096-cycle window
write(PKT_PROBE_MODE, 0);            // Combined R+W
write(PKT_PROBE_EN, 1);

// 5. (optional) Configure Transaction Probe latency bins.
write(TXN_THRESHOLD_0, 0x0010);
write(TXN_THRESHOLD_1, 0x0040);
write(TXN_THRESHOLD_2, 0x0100);
write(TXN_THRESHOLD_3, 0x0400);
write(TXN_PROBE_EN, 1);

// 6. NI is ready. AXI traffic on axi_in_* will be packetized and injected.
```

`TODO(designer):` Confirm that AXI traffic is gated on **any** software action (per the source it appears `ni` is "always on" once reset is released and there is no enable bit). If so, step 6 is a no-op and the comment should reflect that.

## Use case A: Single AXI write through `ni` (no QoS, no probes)

`TODO(designer):` Source describes only the hardware datapath. A driver-level walkthrough — what the AXI master sees on AW/W, observable B latency, error path — must be authored.

**Precondition:** `ni` came out of reset; default `QOS_MODE = Bypass`; ECC enabled; the destination NI is also out of reset.

**Action:** Issue an AXI4 write through the connected AXI master. Address must encode the destination Node ID per `XY_ADDR_OFFSET_X` / `XY_ADDR_OFFSET_Y` (default: `addr[39:32]` = Node ID).

**Observable result:** B response on `axi_in_rsp_o` after a system-dependent latency. Per `theory_of_operation.md` §Performance: AXI AW → `noc_req_o` adds `CUT_AX ? 2 : 1` cycle; the round trip through the mesh adds further hop-count-dependent latency; `noc_rsp_i` → AXI B adds `CUT_RSP ? 2 : 1` cycle.

**Failure mode (uncorrectable W ECC at destination):** B carries `bresp = SLVERR` and `ecc_fail = 1`; `ECC_UNCORR_ERR_CNT` increments at the destination NI; `ERR_STATUS.ecc_uncorr_err` sets at the destination NI. The originating master sees only the SLVERR response; it does **not** observe destination CSR state.

`TODO(designer):` Source does not document an AXI-level error reporting path back to the originating NI's CSR. Confirm whether `ECC_UNCORR_ERR_CNT` is local-only or cross-NI.

## Use case B: Burst write with QoS Regulator

`TODO(designer):` Author. The QoS Regulator's behavior depends on a closed-loop interaction with response bytes; for software, the relevant questions are: when may CSRs be updated relative to in-flight traffic? Are mid-flight changes glitch-free? Source does not address.

## Use case C: Reading performance probes

`TODO(designer):` Snapshot vs. continuous mode is mentioned in `06_qos.md` §3.2 as a Probe feature, but the CSR set does not include a snapshot trigger or a window-rollover indication. Define before D1.

## Error handling

| Error | How software detects | Recovery |
|---|---|---|
| ECC uncorrectable on writes (NSU side) | AXI `bresp = SLVERR` at the originator; `ERR_STATUS.ecc_uncorr_err = 1` at the local NI; `ECC_UNCORR_ERR_CNT++` (saturating). | Software re-issues the write if appropriate; system may decide to disable the affected master / region. |
| ECC uncorrectable on reads (NMU side) | AXI `rresp = SLVERR`; `ERR_STATUS.ecc_uncorr_err = 1` locally; `ECC_UNCORR_ERR_CNT++`. | Re-issue if appropriate. |
| Timeout | `ERR_STATUS.timeout_err = 1`; `LAST_ERR_INFO` populated. **Trigger condition `TODO(designer)`** — undefined in source. | `TODO(designer)`. |
| RoB full backpressure | Not a software-visible error; AXI handshake stalls. | None — hardware-level flow control. |

`TODO(designer):` Recovery procedures for clearing error state are blocked on the `ERR_STATUS` access-mode contradiction noted in `registers.md`.
<!-- source: 04_network_interface.md §5 FR-06; 06_qos.md §4.2, §4.4 -->

## Interrupt handling

`ni` has no top-level interrupt outputs (see `interfaces.md` §Interrupts). All error reporting is via CSR polling.

`TODO(designer):` If a system-level interrupt is added (recommended for ECC uncorrectable and timeout), document the IRQ source set and clearing protocol here.

## Register accesses during operation

`TODO(designer):` This entire section is unspecified in the source documents. Below is the minimum set of question that **must be answered before D1**:

- Which CSRs are safe to write while `ni` is processing AXI traffic? In particular, can `QOS_MODE` change with traffic in flight, or must the host quiesce first?
- Are reads of `*_BIN_*_COUNT` and `PKT_BYTE_COUNT` racy with hardware updates? (Likely yes — see `programmers_guide` §Status reads in the wctmr template for the convention.)
- Is the 64-bit `LAST_ERR_INFO` (or any wider field that crosses a CSR word boundary) atomic from the AXI side?
- Does writing `*_PROBE_EN = 0` immediately freeze counters, or after a window boundary?
- Are bins / probe counters cleared on `*_PROBE_EN` rising edge, or do they persist?

## Reset handling from software

`ni` provides no software-issued reset path per the source. Full reset is via system `rst_ni`.

`TODO(designer):` This is consistent with the wctmr example's "no software reset register" pattern but should be explicitly confirmed for `ni`. Some NoC NIs offer a software-issued drain-and-quiesce sequence (drain in-flight flits, deassert AXI ready, then complete reset); if needed, define here.
