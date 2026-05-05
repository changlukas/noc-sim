# Registers

NI exposes a software-visible CSR file via the dedicated AXI4-Lite subordinate port (`csr_*`). All CSRs are 32-bit-aligned. Access policies:

- **Sub-word access**: not supported. CSR writes with `csr_wstrb != 0xF` (any byte deasserted) trigger `csr_bresp = SLVERR` and the write is dropped. Reads ignore byte strobes (full 32-bit word returned).
- **Unmapped offset**: any `csr_awaddr` or `csr_araddr` that doesn't match a register in §Register map below triggers `csr_bresp = DECERR` (writes) or `csr_rresp = DECERR` (reads). Unmapped reads return `csr_rdata = 0x0`.
- **Misaligned access**: `csr_awaddr[1:0] != 0` or `csr_araddr[1:0] != 0` triggers `SLVERR`.

CSR memory map below is sourced from noc-sim/docs/design/06_qos.md §4 (post-conflict-fix); see provenance comments inline.

## Reserved-bit policy

- **Writes**: bits in the Reserved column of any register layout are ignored — software writes to those bit positions have no effect on hardware state.
- **Reads**: Reserved bits return 0.
- **RW1C bits**: bits not in the §ERR_STATUS layout (i.e., the upper 30 bits of ERR_STATUS) are Reserved and follow the standard reserved-bit policy above (write-ignored, read-zero).
- **Forward compatibility**: software should not assume Reserved bits will remain 0 in future revisions. Always mask reads of Reserved bits to 0 before logical comparison.

## Register map

| Offset | Register | Access | Reset | Description |
|--------|----------|--------|-------|-------------|
| **QoS Generator** |||||
| 0x000 | `QOS_MODE` | RW | 0x0 (Bypass) | QoS 模式選擇 (0=Bypass, 1=Fixed, 2=Limiter, 3=Regulator). See §QOS_MODE. |
| 0x004 | `QOS_FIXED_VALUE` | RW | 0x0 | Fixed mode qos 值. See §QOS_FIXED_VALUE. |
| 0x008 | `BANDWIDTH_LIMIT` | RW | 0x0 | Limiter 頻寬限制 (1/256 bytes/cycle). |
| 0x00C | `SATURATION_THRESHOLD` | RW | 0x0 | Limiter 飽和閾值 (bytes). |
| 0x010 | `LOW_PRIORITY` | RW | 0x0 | Limiter 超標時 qos. |
| 0x014 | `BANDWIDTH_BUDGET` | RW | 0x0 | Regulator 頻寬預算 (1/256 bytes/cycle). |
| 0x018 | `BASE_QOS` | RW | 0x0 | Regulator 基礎 qos + URGENCY_STEP combined. See §BASE_QOS for field layout. <!-- source: 06_qos.md §4.1 + §4.5 (post-fix) --> |
| 0x01C | `SOCKET_QOS_EN` | RW | 0x0 | Regulator Socket QoS 啟用. |
| 0x020 | `SOCKET_QOS` | RW | 0x0 | Regulator Socket QoS 下限. |
| **Packet Probe** |||||
| 0x040 | `PKT_PROBE_EN` | RW | 0x0 | 啟用 Packet Probe. |
| 0x044 | `PKT_PROBE_MODE` | RW | 0x0 (Combined) | 統計模式 (0=Combined, 1=Read, 2=Write). |
| 0x048 | `PKT_WINDOW_SIZE` | RW | 0x0 | 統計視窗 (cycles). |
| 0x04C | `PKT_BYTE_COUNT` | RO | 0x0 | 傳輸 bytes 累計 (saturating). |
| 0x050 | `PKT_BANDWIDTH` | RO | 0x0 | 計算的頻寬 (per current window). |
| **Transaction Probe** |||||
| 0x060 | `TXN_PROBE_EN` | RW | 0x0 | 啟用 Transaction Probe. |
| 0x064 | `TXN_THRESHOLD_0` | RW | 0x0 | 延遲閾值 0 (cycles). |
| 0x068 | `TXN_THRESHOLD_1` | RW | 0x0 | 延遲閾值 1. |
| 0x06C | `TXN_THRESHOLD_2` | RW | 0x0 | 延遲閾值 2. |
| 0x070 | `TXN_THRESHOLD_3` | RW | 0x0 | 延遲閾值 3. |
| 0x080 | `TXN_BIN_0_COUNT` | RO | 0x0 | Bin 0 計數 (saturating). |
| 0x084 | `TXN_BIN_1_COUNT` | RO | 0x0 | Bin 1 計數. |
| 0x088 | `TXN_BIN_2_COUNT` | RO | 0x0 | Bin 2 計數. |
| 0x08C | `TXN_BIN_3_COUNT` | RO | 0x0 | Bin 3 計數. |
| 0x090 | `TXN_BIN_4_COUNT` | RO | 0x0 | Bin 4 計數. |
| 0x094 | `TXN_MIN_LATENCY` | RO | 0xFFFF | 最小延遲 (16-bit; initialised to all-1s sentinel value; first observed transaction-latency overwrites; subsequent observations write only if smaller). Cleared back to 0xFFFF via `TXN_PROBE_EN` 1→0→1 transition. |
| 0x098 | `TXN_MAX_LATENCY` | RO | 0x0 | 最大延遲. |
| 0x09C | `TXN_TOTAL_COUNT` | RO | 0x0 | 總 transaction 數 (saturating). |
| **Error Status** |||||
| 0x100 | `ERR_STATUS` | RW1C | 0x0 | 錯誤狀態 — write 1 to clear bit and the associated saturating counter. See §ERR_STATUS. <!-- source: 06_qos.md §4.1, post-fix RW1C --> |
| 0x104 | `ERR_COUNT` | RO | 0x0 | 錯誤計數 (`ERR_COUNTER_WIDTH` bits, saturating). Cleared via `ERR_STATUS[1]` write-1 (timeout_err). |
| 0x108 | `ECC_UNCORR_ERR_CNT` | RO | 0x0 | ECC uncorrectable 錯誤計數 (saturating). Cleared via `ERR_STATUS[0]` write-1 (ecc_uncorr_err). |
| 0x10C | `LAST_ERR_INFO` | RO | 0x0 | 最近錯誤資訊 (sticky semantics; see §LAST_ERR_INFO for field layout). |

## §BASE_QOS Register (0x018) Field Layout

<!-- source: 06_qos.md §4.5 (post-fix) -->

| Field | Bit | Width | Description | Reset |
|-------|-----|-------|-------------|-------|
| `BASE_QOS` | [3:0] | 4 | Regulator mode 基礎 QoS 值 (urgency_level=0 時使用，0~15). See ToO §QoSGen. | 0x0 |
| `URGENCY_STEP` | [5:4] | 2 | Urgency 每次調整的步進值 (1~3；軟體寫 0 時硬體視同 1). See ToO §QoSGen Regulator mode. | 0x0 |
| Reserved | [31:6] | 26 | — | 0x0 |

## §QOS_MODE Register (0x000) Field Layout

| Field | Bit | Width | Description | Reset |
|-------|-----|-------|-------------|-------|
| `QOS_MODE` | [1:0] | 2 | 0=Bypass, 1=Fixed, 2=Limiter, 3=Regulator | 0x0 |
| Reserved | [31:2] | 30 | — | 0x0 |

## §QOS_FIXED_VALUE Register (0x004) Field Layout

| Field | Bit | Width | Description | Reset |
|-------|-----|-------|-------------|-------|
| `QOS_FIXED_VALUE` | [3:0] | 4 | Fixed mode 輸出 qos 值 (0~15) | 0x0 |
| Reserved | [31:4] | 28 | — | 0x0 |

## §ERR_STATUS Register (0x100) Field Layout

<!-- source: 06_qos.md §4.2 -->

| Field | Bit | Width | Description | Reset |
|-------|-----|-------|-------------|-------|
| `ecc_uncorr_err` | [0] | 1 | ECC uncorrectable 錯誤發生; write 1 clears bit + `ECC_UNCORR_ERR_CNT`. | 0x0 |
| `timeout_err` | [1] | 1 | Timeout 錯誤發生; write 1 clears bit + `ERR_COUNT`. | 0x0 |
| Reserved | [31:2] | 30 | — | 0x0 |

## §LAST_ERR_INFO Register (0x10C) Field Layout

<!-- source: 06_qos.md §4.3 -->

Field widths derived from defaults: `AXI_ID_WIDTH=8`, `X_WIDTH+Y_WIDTH=8`. For non-default configurations: register layout adjusts at compile time. If `AXI_ID_WIDTH=16`, `err_axi_id` occupies [15:0], `err_src_id` occupies [23:16], `err_dst_id` occupies [31:24], no Reserved bits. If total width exceeds 32 bits, register splits into LAST_ERR_INFO_LO (0x10C) + LAST_ERR_INFO_HI (0x110); see §Compile-time-conditional registers below.

| Field | Bit | Width | Description | Reset |
|-------|-----|-------|-------------|-------|
| `err_axi_id` | [7:0] | `AXI_ID_WIDTH` | 錯誤 transaction 的 AXI ID. | 0x0 |
| `err_src_id` | [15:8] | `X_WIDTH + Y_WIDTH` | 錯誤來源 node ID. | 0x0 |
| `err_dst_id` | [23:16] | `X_WIDTH + Y_WIDTH` | 錯誤目標 node ID. | 0x0 |
| Reserved | [31:24] | 8 | — | 0x0 |

**Update semantics** (resolved per ToO §ECC and protocol_rules.md `NI_CFG_LAST_ERR_INFO_CAPTURE`): **sticky** — first error since last clear is captured; subsequent errors do not overwrite until software clears via `ERR_STATUS` RW1C write. Rationale: prevents losing the original triggering error while system processes subsequent cascaded errors. Test in dv/plan TP19. *Reviewer assumption: confirm vs alternative (overwrite — last error wins).*

## Counter saturation behavior

<!-- source: 06_qos.md §4.4 -->

All error counters and bin counters use **saturating arithmetic**: increment up to `2^W - 1`, then hold; no wrap-around. Clear mechanisms:

| Counter group | Clear mechanism |
|---|---|
| Error counters (`ERR_COUNT`, `ECC_UNCORR_ERR_CNT`) | Software writes 1 to corresponding `ERR_STATUS[N]` bit; counter clears atomically with the bit |
| Packet Probe counters (`PKT_BYTE_COUNT`, `PKT_BANDWIDTH`) | Software writes `PKT_PROBE_EN = 0` then `PKT_PROBE_EN = 1`; on the 0→1 transition, counters reset to 0. *Reviewer assumption: this mechanism not in noc-sim 06_qos.md §3 originally; introduced here for testability.* |
| Transaction Probe counters (`TXN_BIN_*_COUNT`, `TXN_TOTAL_COUNT`) | Same — `TXN_PROBE_EN` 1→0→1 transition resets all bin counters and TXN_TOTAL_COUNT to 0 |
| Latency extremes (`TXN_MIN_LATENCY`, `TXN_MAX_LATENCY`) | Same — `TXN_PROBE_EN` 1→0→1 resets MIN to 0xFFFF and MAX to 0x0 |

## Cross-reference to behavior

For QoS Generator behavior, see [Theory of Operation §QoSGen](./theory_of_operation.md#qos-generator).
For ECC error counter triggering, see [Theory of Operation §ECC](./theory_of_operation.md#ecc).
For Probe counter update timing, see protocol_rules.md `NI_CFG_PROBE_PKT_BYTE_COUNT` and `NI_CFG_PROBE_TXN_LATENCY` for cycle-level update specification.
