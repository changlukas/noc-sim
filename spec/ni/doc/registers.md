# Registers

`TODO(designer):` Reserved-bit policy is **not stated** in the source. Recommended default: "Reserved fields read as zero. Writes to reserved fields are ignored. Software should write reserved fields as zero to remain forward-compatible." Confirm and adopt before D1.

`TODO(designer):` The CSR memory map is documented in `06_qos.md` as a NoC-system / NI register map. The source does not state which AXI bus exposes these registers, the address window size, or the access widths permitted. Sub-word access policy and unmapped-offset policy must also be defined. The conventional choice is "32-bit aligned, sub-word returns SLVERR; offsets outside the documented range return SLVERR."

## Register map

| Offset | Name | Access | Reset | Description |
|---|---|---|---|---|
| 0x000 | `QOS_MODE` | RW | `TODO(designer)` | QoS mode select. See Theory of Operation §Datapath / NMU. |
| 0x004 | `QOS_FIXED_VALUE` | RW | `TODO(designer)` | Fixed-mode qos value. |
| 0x008 | `BANDWIDTH_LIMIT` | RW | `TODO(designer)` | Limiter bandwidth cap (1/256 bytes/cycle). |
| 0x00C | `SATURATION_THRESHOLD` | RW | `TODO(designer)` | Limiter saturation threshold. |
| 0x010 | `LOW_PRIORITY` | RW | `TODO(designer)` | Limiter low-priority qos value when threshold exceeded. |
| 0x014 | `BANDWIDTH_BUDGET` | RW | `TODO(designer)` | Regulator bandwidth budget (1/256 bytes/cycle). |
| 0x018 | `BASE_QOS` | RW | `TODO(designer)` | Regulator base qos (urgency = 0). |
| 0x01C | `SOCKET_QOS_EN` | RW | `TODO(designer)` | Socket-QoS-floor enable. |
| 0x020 | `SOCKET_QOS` | RW | `TODO(designer)` | Socket QoS floor value. |
| 0x040 | `PKT_PROBE_EN` | RW | `TODO(designer)` | Packet Probe enable. |
| 0x044 | `PKT_PROBE_MODE` | RW | `TODO(designer)` | 0 = Combined, 1 = Read, 2 = Write. |
| 0x048 | `PKT_WINDOW_SIZE` | RW | `TODO(designer)` | Statistic window (cycles). |
| 0x04C | `PKT_BYTE_COUNT` | RO | 0x0 | Bytes transferred (read-only counter). |
| 0x050 | `PKT_BANDWIDTH` | RO | 0x0 | Computed bandwidth result. |
| 0x060 | `TXN_PROBE_EN` | RW | `TODO(designer)` | Transaction Probe enable. |
| 0x064 | `TXN_THRESHOLD_0` | RW | `TODO(designer)` | Latency bin boundary 0 (cycles). |
| 0x068 | `TXN_THRESHOLD_1` | RW | `TODO(designer)` | Bin boundary 1. |
| 0x06C | `TXN_THRESHOLD_2` | RW | `TODO(designer)` | Bin boundary 2. |
| 0x070 | `TXN_THRESHOLD_3` | RW | `TODO(designer)` | Bin boundary 3. |
| 0x080 | `TXN_BIN_0_COUNT` | RO | 0x0 | Bin 0 count. |
| 0x084 | `TXN_BIN_1_COUNT` | RO | 0x0 | Bin 1 count. |
| 0x088 | `TXN_BIN_2_COUNT` | RO | 0x0 | Bin 2 count. |
| 0x08C | `TXN_BIN_3_COUNT` | RO | 0x0 | Bin 3 count. |
| 0x090 | `TXN_BIN_4_COUNT` | RO | 0x0 | Bin 4 count. |
| 0x094 | `TXN_MIN_LATENCY` | RO | 0x0 | Minimum observed latency. |
| 0x098 | `TXN_MAX_LATENCY` | RO | 0x0 | Maximum observed latency. |
| 0x09C | `TXN_TOTAL_COUNT` | RO | 0x0 | Total transactions counted. |
| 0x100 | `ERR_STATUS` | RO | 0x0 | Error status flags. |
| 0x104 | `ERR_COUNT` | RO | 0x0 | Total error count (saturating). |
| 0x108 | `ECC_UNCORR_ERR_CNT` | RO | 0x0 | ECC uncorrectable error count (saturating). |
| 0x10C | `LAST_ERR_INFO` | RO | 0x0 | Most-recent error context (AXI ID + src/dst Node ID). |
<!-- source: 06_qos.md §4.1 -->

`TODO(designer):` Register access width — the source assumes 32-bit registers but does not state this. Confirm and add to the section preface.

`TODO(designer):` All `RW` reset values are listed as `TODO`. Conventional defaults are 0x00000000, but `QOS_MODE` in particular may sensibly default to 0 (Bypass) — confirm.

## Register details

### QOS_MODE

- Offset: 0x000
- Width: `TODO(designer): confirm 32`
- Access: RW
- Reset: `TODO(designer)`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [1:0] | mode | RW | `TODO` | 0 = Bypass (use AXI awqos/arqos directly), 1 = Fixed, 2 = Limiter, 3 = Regulator. See Theory of Operation §Datapath / NMU. |
| [31:2] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.2 -->

### QOS_FIXED_VALUE

- Offset: 0x004 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [3:0] | value | RW | `TODO` | qos value driven into the flit header when `QOS_MODE = Fixed`. |
| [31:4] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.2 -->

### BANDWIDTH_LIMIT

- Offset: 0x008 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | limit | RW | `TODO` | Limiter bandwidth cap, units 1/256 bytes/cycle. See Theory of Operation §Datapath / NMU. |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.3 -->

### SATURATION_THRESHOLD

- Offset: 0x00C — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | threshold | RW | `TODO` | Limiter saturation threshold (bytes). |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.3 -->

### LOW_PRIORITY

- Offset: 0x010 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [3:0] | value | RW | `TODO` | qos value when Limiter detects over-budget. |
| [31:4] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.3 -->

### BANDWIDTH_BUDGET

- Offset: 0x014 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | budget | RW | `TODO` | Regulator target bandwidth, 1/256 bytes/cycle. |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.4.6 -->

### BASE_QOS

- Offset: 0x018 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [3:0] | base_qos | RW | `TODO` | Regulator base qos, used when urgency = 0. |
| [5:4] | urgency_step | RW | `TODO` | Urgency step (1..3). `TODO(designer):` source declares 2 bits but limits values to 1..3 — confirm whether 0 is reserved. |
| [31:6] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.4.6 -->

`TODO(designer):` `URGENCY_STEP` (2-bit) is declared as a separate register row in the source but no offset is given. Best guess: same register as `BASE_QOS`, occupying bits [5:4]. **This is an inferred consolidation; please confirm**.

### SOCKET_QOS_EN

- Offset: 0x01C — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [0] | enable | RW | `TODO` | Enable Socket-QoS floor. |
| [31:1] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.4.6 -->

### SOCKET_QOS

- Offset: 0x020 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [3:0] | floor | RW | `TODO` | Socket QoS floor value (qos lower bound). |
| [31:4] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §2.4.6 -->

### PKT_PROBE_EN

- Offset: 0x040 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [0] | enable | RW | `TODO` | Enable Packet Probe. |
| [31:1] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.2 -->

### PKT_PROBE_MODE

- Offset: 0x044 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [1:0] | mode | RW | `TODO` | 0 = Combined, 1 = Read-only, 2 = Write-only. |
| [31:2] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.2 -->

### PKT_WINDOW_SIZE

- Offset: 0x048 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | cycles | RW | `TODO` | Statistic window size (cycles). |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.2 -->

### PKT_BYTE_COUNT

- Offset: 0x04C — Width: 32 — Access: RO — Reset: 0x00000000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [31:0] | count | RO | 0x0 | Bytes transferred in the most recent window. |
<!-- source: 06_qos.md §3.2 -->

`TODO(designer):` Counter overflow / clear-on-read / window-rollover semantics are not specified for `PKT_BYTE_COUNT` and `PKT_BANDWIDTH`. Specify before D1.

### PKT_BANDWIDTH

- Offset: 0x050 — Width: 32 — Access: RO — Reset: 0x00000000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [31:0] | bw | RO | 0x0 | Computed bandwidth (units `TODO(designer): bytes/cycle? or bytes/window?`). |
<!-- source: 06_qos.md §3.2 -->

### TXN_PROBE_EN

- Offset: 0x060 — Width: `TODO` — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [0] | enable | RW | `TODO` | Enable Transaction Probe. |
| [31:1] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.3 -->

### TXN_THRESHOLD_0..3

- Offsets: 0x064, 0x068, 0x06C, 0x070 — Width: 16 (declared) — Access: RW — Reset: `TODO`

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | cycles | RW | `TODO` | Latency bin boundary, in cycles. Bin 0: latency < THRESHOLD_0; Bin N: latency ≥ THRESHOLD_{N-1}. |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.3 -->

### TXN_BIN_0_COUNT..4_COUNT

- Offsets: 0x080, 0x084, 0x088, 0x08C, 0x090 — Width: 32 — Access: RO — Reset: 0x00000000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [31:0] | count | RO | 0x0 | Number of transactions whose latency fell into this bin. |
<!-- source: 06_qos.md §3.3 -->

`TODO(designer):` Saturation behavior of bin counters is unspecified. Recommend they be saturating to `2^32 - 1` like the error counters.

### TXN_MIN_LATENCY / TXN_MAX_LATENCY

- Offsets: 0x094, 0x098 — Width: 16 — Access: RO — Reset: 0x0000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [15:0] | latency | RO | 0x0 | Minimum / maximum observed latency. `TODO(designer):` initial reset value semantics — does MIN start at `0xFFFF`? |
| [31:16] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §3.3 -->

### TXN_TOTAL_COUNT

- Offset: 0x09C — Width: 32 — Access: RO — Reset: 0x00000000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [31:0] | count | RO | 0x0 | Total transactions counted. |
<!-- source: 06_qos.md §3.3 -->

### ERR_STATUS

- Offset: 0x100 — Width: `TODO(designer): 32 likely` — Access: RO — Reset: 0x00000000

`TODO(designer):` Source declares this RO, but the clear mechanism in `06_qos.md §4.4` says counters clear on "write 1 to ERR_STATUS[0]". An RO register cannot be written. **There is a contradiction.** Most likely the access is W1C (write-1-to-clear). Resolve: either change the access to RW1C, or specify a separate ERR_CLEAR register, or remove the clear-via-write-1 sentence.

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [0] | ecc_uncorr_err | `TODO(designer): RW1C?` | 0x0 | Set to 1 when at least one ECC uncorrectable error has been observed since last clear. |
| [1] | timeout_err | `TODO(designer): RW1C?` | 0x0 | Set to 1 when a timeout occurs. **Trigger condition `TODO(designer)`**. |
| [7:2] | — | RO | 0x0 | Reserved. |
| [31:8] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §4.2, §4.4 -->

### ERR_COUNT

- Offset: 0x104 — Width: `ERR_COUNTER_WIDTH` (default 16) — Access: RO — Reset: 0x0000

`TODO(designer):` Hardware width is configurable (default 16) but the register is documented at offset 0x104 with no further width statement; if the register is always presented as 32 bits with the upper bits reserved, state that explicitly.

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [`ERR_COUNTER_WIDTH-1`:0] | count | RO | 0x0 | Saturating count of total errors observed. Cleared by writing 1 to `ERR_STATUS.ecc_uncorr_err` (per source §4.4 — see contradiction note above). |
| [31:`ERR_COUNTER_WIDTH`] | — | RO | 0x0 | Reserved (when register is presented as 32 bits). |
<!-- source: 06_qos.md §4.4 -->

### ECC_UNCORR_ERR_CNT

- Offset: 0x108 — Width: `ERR_COUNTER_WIDTH` (default 16) — Access: RO — Reset: 0x0000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [`ERR_COUNTER_WIDTH-1`:0] | count | RO | 0x0 | Saturating count of ECC uncorrectable errors. |
| [31:`ERR_COUNTER_WIDTH`] | — | RO | 0x0 | Reserved. |
<!-- source: 06_qos.md §4.4 -->

### LAST_ERR_INFO

- Offset: 0x10C — Width: `TODO(designer): 32 likely` — Access: RO — Reset: 0x00000000

| Bits | Field | Access | Reset | Description |
|---|---|---|---|---|
| [`AXI_ID_WIDTH-1`:0] (default [7:0]) | err_axi_id | RO | 0x0 | AXI ID of the offending transaction. |
| [`AXI_ID_WIDTH+X+Y-1`:`AXI_ID_WIDTH`] (default [15:8]) | err_src_id | RO | 0x0 | Source Node ID. |
| [`AXI_ID_WIDTH+2(X+Y)-1`:`AXI_ID_WIDTH+X+Y`] (default [23:16]) | err_dst_id | RO | 0x0 | Destination Node ID. |
| [31:24] | — | RO | 0x0 | Reserved (default configuration). |
<!-- source: 06_qos.md §4.3 -->

`TODO(designer):` `LAST_ERR_INFO` updates on every error or only on the first since last clear? Source does not state. Define before D1.

## Notes pending designer resolution

- INTR_STATE / INTR_ENABLE / INTR_TEST are **not** present, consistent with `interfaces.md` §Interrupts (no top-level interrupt outputs). If interrupts are added later, follow the OpenTitan triplet convention.
- The CSR register file's bus protocol is not specified. Most likely it is exposed via the AXI manager port at a reserved address window — `TODO(designer): confirm`.
