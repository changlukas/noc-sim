# Flit Format — Parameterized Design

NoC 基本傳輸單元。所有欄位寬度透過 symbol 參數表達，預設值對應目前實作配置（最大 16×16 mesh、AXI 256-bit data）。Multicast 相關欄位保留位置但功能未定義。

---

## 1. Overview

### 1.1 Design Philosophy

採用 **可參數化欄位寬度** 設計：

1. **每個欄位皆有對應 width parameter** — 包含 1-bit flag 在內，命名一致便於跨模組引用
2. **預設值對齊現行配置** — 未調參時 `FLIT_WIDTH = 400 bits`，`HEADER_WIDTH = 48 bits`
3. **預留欄位明確標示** — `rsvd_commtype` (2b)、`multicast` (8b)、`rsvd_mc_status` (2b) 為 future extension 預留位置，當前傳輸時設 0、接收端忽略
4. **Multicast 編碼方式 TBD** — 本文件不定義 multicast 路由與 response 行為

### 1.2 Design Parameters

#### Group 1 — Topology

| Parameter | Default | Description |
|-----------|---------|-------------|
| `X_WIDTH` | 4 | X coordinate width (max 16 columns) |
| `Y_WIDTH` | 4 | Y coordinate width (max 16 rows) |
| `PORT_ID_WIDTH` | 2 | Local port index width (4 ports per router) |

`src_id` 與 `dst_id` 寬度為 `X_WIDTH + Y_WIDTH`（預設 8 bits），編碼 `[X_WIDTH-1:0]=x`、`[X_WIDTH+Y_WIDTH-1:X_WIDTH]=y`。

#### Group 2 — Header Fields

| Parameter | Default | Description |
|-----------|---------|-------------|
| `QOS_WIDTH` | 4 | QoS priority |
| `AXI_CH_WIDTH` | 3 | AXI channel type (5 channels: AW/W/AR/B/R) |
| `LAST_WIDTH` | 1 | Packet end marker |
| `ROB_REQ_WIDTH` | 1 | RoB request flag |
| `ROB_IDX_WIDTH` | 5 | RoB index (32 entries) |
| `RSVD_COMMTYPE_WIDTH` | 2 | Reserved for future communication type extension |
| `MULTICAST_WIDTH` | 8 | Reserved for future multicast extension (encoding TBD) |
| `VC_ID_WIDTH` | 3 | Virtual Channel ID (Credit-Based mode only, max 8 VCs) |

#### Group 3 — AXI Payload Sub-Fields

下列寬度遵循 AXI4 規範，調整可能需 AXI bridge / converter 同步修改：

| Parameter | Default | Description |
|-----------|---------|-------------|
| `AXI_ADDR_WIDTH` | 64 | AXI address |
| `AXI_ID_WIDTH` | 8 | AXI transaction ID |
| `AXI_DATA_WIDTH` | 256 | AXI data (32 bytes) |
| `AXI_USER_WIDTH` | 8 | AXI user signal |
| `AXI_LEN_WIDTH` | 8 | Burst length |
| `AXI_SIZE_WIDTH` | 3 | Burst size |
| `AXI_BURST_WIDTH` | 2 | Burst type |
| `AXI_CACHE_WIDTH` | 4 | Cache attributes |
| `AXI_LOCK_WIDTH` | 1 | Exclusive access |
| `AXI_PROT_WIDTH` | 3 | Protection type |
| `AXI_REGION_WIDTH` | 4 | Memory region identifier |
| `AXI_RESP_WIDTH` | 2 | Response status (bresp/rresp) |
| `AXI_LAST_WIDTH` | 1 | AXI last beat (wlast/rlast) |

#### Group 4 — ECC

| Parameter | Default | Description |
|-----------|---------|-------------|
| `ECC_GRANULE_WIDTH` | 64 | Data bits per ECC granule |
| `ECC_PER_GRANULE_WIDTH` | 8 | ECC bits per granule (Hsiao SECDED) |
| `ECC_FAIL_WIDTH` | 1 | ECC error flag |
| `ECC_WIDTH` | derived = 32 | `(AXI_DATA_WIDTH / ECC_GRANULE_WIDTH) × ECC_PER_GRANULE_WIDTH` |

#### Group 5 — B Channel Reserved

| Parameter | Default | Description |
|-----------|---------|-------------|
| `RSVD_MC_STATUS_WIDTH` | 2 | Reserved for future multicast status extension |

#### Group 6 — Composite / Derived

| Parameter | Default | Formula |
|-----------|---------|---------|
| `WSTRB_WIDTH` | 32 | `AXI_DATA_WIDTH / 8` |
| `HEADER_WIDTH` | 48 | Σ Group 1+2 fields (見 §2.1) |
| `AW_PAYLOAD_WIDTH` | 108 | Σ AW fields (見 §3.1) |
| `W_PAYLOAD_WIDTH` | 352 | Σ W fields (見 §3.2) |
| `AR_PAYLOAD_WIDTH` | 108 | Σ AR fields (見 §3.1) |
| `B_PAYLOAD_WIDTH` | 64 | Σ B fields (見 §3.3) |
| `R_PAYLOAD_WIDTH` | 352 | Σ R fields (見 §3.4) |
| `PAYLOAD_WIDTH` | 352 | `max(AW, W, AR, B, R)_PAYLOAD_WIDTH` |
| `FLIT_WIDTH` | 400 | `HEADER_WIDTH + PAYLOAD_WIDTH` |
| `LINK_WIDTH` | 402 | `FLIT_WIDTH + 2` (valid + ready) |

### 1.3 Flit Structure

```
  HEADER_WIDTH-1                    0   PAYLOAD_WIDTH-1                   0
  ┌────────────────────────────────────┬──────────────────────────────────┐
  │       Header (HEADER_WIDTH)        │      Payload (PAYLOAD_WIDTH)     │
  └────────────────────────────────────┴──────────────────────────────────┘
  |<──────────────────── Flit: FLIT_WIDTH ─────────────────────────────>|
```

預設配置下 Header = 48 bits、Payload = 352 bits、Flit = 400 bits。所有 AXI channel 共用 `FLIT_WIDTH`，較短 payload 以 zero-padding 對齊至 `PAYLOAD_WIDTH`。Request 與 Response flit 寬度對稱。

Flit 內 wdata/rdata 採 little-endian byte ordering（byte address 0 = data[7:0]），與 AXI bus 一致。Header 與 payload metadata 使用 MSB-first bit numbering（[MSB:LSB]），遵循 SystemVerilog 慣例。

---

## 2. Header Format

Header 預設 `HEADER_WIDTH = 48 bits`，提供 Valid/Ready mode（No-VC）與 Credit-Based mode（With-VC）兩種配置。兩者僅 vc_id / rsvd 區段配置不同，其餘欄位位置與寬度完全相同。

### 2.1 Bit Allocation

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| qos | `QOS_WIDTH` | [3:0] | QoS priority |
| axi_ch | `AXI_CH_WIDTH` | [6:4] | AXI channel type |
| src_id | `X_WIDTH + Y_WIDTH` | [14:7] | Source node ID |
| dst_id | `X_WIDTH + Y_WIDTH` | [22:15] | Destination node ID |
| port_id | `PORT_ID_WIDTH` | [24:23] | Target local port index |
| last | `LAST_WIDTH` | [25] | Packet end marker |
| rob_req | `ROB_REQ_WIDTH` | [26] | RoB request flag |
| rob_idx | `ROB_IDX_WIDTH` | [31:27] | RoB index |
| rsvd_commtype | `RSVD_COMMTYPE_WIDTH` | [33:32] | Reserved (future commtype) |
| multicast | `MULTICAST_WIDTH` | [41:34] | Reserved (future multicast) |
| vc_id ‡ | `VC_ID_WIDTH` | [44:42] | Virtual Channel ID |
| rsvd ‡ | derived | [47:45] | Reserved |
| | **HEADER_WIDTH** | | |

**‡ Version-dependent fields:**

- **Valid/Ready mode (No-VC):** Header 高位 `VC_ID_WIDTH + 3` bits 全為 rsvd（預設 [47:42]，6 bits），無 vc_id。VC 由 signal-level 多組 valid/ready lines 實現。
- **Credit-Based mode (With-VC):** vc_id 佔 `VC_ID_WIDTH` bits（預設 [44:42]），其後 3 bits 為 rsvd（預設 [47:45]）。VC ID 編碼於 header，搭配 credit-based flow control。

### 2.2 Field Definitions

#### 2.2.1 QoS (`QOS_WIDTH` = 4 bits)

由 NI 自 AXI `awqos`（Write）或 `arqos`（Read）提取，作為 router arbitration 優先級依據。

| Value | Priority | Description |
|-------|----------|-------------|
| 0 | Lowest | Best effort |
| 15 | Highest | Real-time critical |

AW/AR payload 不重複儲存 qos，header 為唯一來源。Response flit 繼承對應 request 的 `qos`。Router arbiter 以 `qos` 為第一優先級比較依據。詳見 [QoS Design](06_qos.md)。

#### 2.2.2 AXI Channel Type (`AXI_CH_WIDTH` = 3 bits)

| Value | Name | Channel | Description |
|-------|------|---------|-------------|
| 0 | AW | REQ | Write Address |
| 1 | W | REQ | Write Data |
| 2 | AR | REQ | Read Address |
| 3 | B | RSP | Write Response |
| 4 | R | RSP | Read Response |
| 5\~7 | — | — | Reserved |

Router 依據 `axi_ch` 判定 flit 所屬之 Request 或 Response network。

#### 2.2.3 Node ID (src_id / dst_id, `X_WIDTH + Y_WIDTH` bits each)

採用座標編碼：

| Bits | Field | Description |
|------|-------|-------------|
| `[X_WIDTH-1:0]` | x | X coordinate (預設 4 bits, 0\~15) |
| `[X_WIDTH+Y_WIDTH-1:X_WIDTH]` | y | Y coordinate (預設 4 bits, 0\~15) |

XY routing 依據 dst_id 的 x/y 座標進行路由決策。src_id 用於 response 回程路由及 error handling 來源識別。src_id 置於 dst_id 前方的理由見 Section 2.3。

#### 2.2.4 Port ID (`PORT_ID_WIDTH` = 2 bits)

獨立於 node ID，指定目標 Router 的 local port：

| Value | Port | Typical Usage |
|-------|------|---------------|
| 0 | Port 0 | CPU / Processor |
| 1 | Port 1 | DMA Engine |
| 2 | Port 2 | Memory Controller |
| 3 | Port 3 | Accelerator |

Request 時指定目標 local port；response 時 NI 填入原始 requester 的 `port_id`。

#### 2.2.5 Flit Control: last (`LAST_WIDTH` = 1 bit)

| Value | Meaning |
|-------|---------|
| 0 | Not last flit (more flits in this packet) |
| 1 | Last flit of packet (tail) |

Wormhole switching 依據 `last` 釋放 path lock。Single-flit packet（AW, AR, B）恆為 `last=1`；multi-flit packet（W, R）僅末尾設 `last=1`。

#### 2.2.6 RoB Fields: rob_req (`ROB_REQ_WIDTH` = 1 bit), rob_idx (`ROB_IDX_WIDTH` = 5 bits)

| Field | Width | Description |
|-------|-------|-------------|
| rob_req | `ROB_REQ_WIDTH` | 1 = request RoB reorder; 0 = no reorder |
| rob_idx | `ROB_IDX_WIDTH` | RoB entry index (預設 5b → 0\~31) |

Source NI 分配 `rob_idx`，destination NI 依此 index 將 response 寫入對應 RoB slot。`rob_req=0` 時 `rob_idx` 為 don't care。RoB 架構見 Section 5。

#### 2.2.7 Reserved: rsvd_commtype (`RSVD_COMMTYPE_WIDTH` = 2 bits)

預留欄位，當前所有傳輸視為 Unicast。寬度預設 2 bits 為未來通訊類型擴展（例如 multicast、reduction 等）保留位置，編碼方式 **TBD**。傳輸時設為 0，接收端忽略。

#### 2.2.8 Reserved: multicast (`MULTICAST_WIDTH` = 8 bits)

預留欄位。寬度預設 8 bits 為未來 multicast 編碼方案保留位置（例如 destination mask、bounding box 或其他形式），**編碼方式與路由行為均未定義**。傳輸時設為 0，接收端忽略。

#### 2.2.9 VC ID & Reserved

**vc_id (`VC_ID_WIDTH` = 3 bits, Credit-Based mode only):**

支援最多 `2^VC_ID_WIDTH` 個 Virtual Channels（預設 8）。用途含 Request/Response 分離避免 deadlock、traffic class 隔離。Valid/Ready mode 中此 bits 歸入 reserved。

**Reserved bits:**

| Mode | Bit Range (default) | Width Formula |
|------|---------------------|---------------|
| Valid/Ready (No-VC) | [47:42] | `VC_ID_WIDTH + 3` (=6) |
| Credit-Based (With-VC) | [47:45] | 3 |

傳輸時設為 0，接收端忽略。

### 2.3 Field Ordering Rationale

| Priority | Category | Fields | Usage |
|----------|----------|--------|-------|
| 1 | Arbitration | qos | Arbiter priority comparison |
| 2 | Opcode | axi_ch | Flit type / network selection |
| 3 | Routing | src_id, dst_id, port_id | XY routing, local port selection |
| 4 | Flit Control | last | Wormhole path lock release |
| 5 | Flow Control | rob_req, rob_idx | Destination NI processing |
| 6 | Reserved | rsvd_commtype, multicast | Future extension |
| 7 | VC / Reserved | vc_id, rsvd | Version-dependent |

高頻存取欄位置於 LSB 端，第一級 pipeline 僅需 qos + axi_ch 即可完成 arbitration 與 channel 分離。Reserved 欄位置於 MSB 端，未來擴展不影響既有 bit position。

src_id 置於 dst_id 前方：response 路由以 src_id 為回程目的地，error handling 需優先識別來源。

### 2.4 Bit Layout Diagrams (default values)

#### Valid/Ready Mode (No-VC)

```
 47    42 41         34 33 32 31    27  26   25  24 23 22    15 14     7  6  4  3  0
┌───────┬─────────────┬─────┬──────┬────┬────┬─────┬────────┬────────┬─────┬──────┐
│ rsvd  │  multicast  │rsvd_│ rob  │rob │last│port │ dst_id │ src_id │ axi │ qos  │
│  6b   │   8b        │comm │idx 5b│req │ 1b │id 2b│   8b   │   8b   │ch 3b│  4b  │
│       │ (reserved)  │type │      │ 1b │    │     │        │        │     │      │
│       │             │ 2b  │      │    │    │     │        │        │     │      │
└───────┴─────────────┴─────┴──────┴────┴────┴─────┴────────┴────────┴─────┴──────┘
```

#### Credit-Based Mode (With-VC)

```
 47 45 44 42 41         34 33 32 31    27  26   25  24 23 22    15 14     7  6  4  3  0
┌────┬────┬─────────────┬─────┬──────┬────┬────┬─────┬────────┬────────┬─────┬──────┐
│rsvd│ vc │  multicast  │rsvd_│ rob  │rob │last│port │ dst_id │ src_id │ axi │ qos  │
│ 3b │id 3b│   8b       │comm │idx 5b│req │ 1b │id 2b│   8b   │   8b   │ch 3b│  4b  │
│    │    │ (reserved)  │type │      │ 1b │    │     │        │        │     │      │
│    │    │             │ 2b  │      │    │    │     │        │        │     │      │
└────┴────┴─────────────┴─────┴──────┴────┴────┴─────┴────────┴────────┴─────┴──────┘
```

---

## 3. Payload Format

各 channel payload 以 union 方式共享 `PAYLOAD_WIDTH`（預設 352 bits，由 W/R channel 主導），較短 payload 以 zero-padding 補齊。

### 3.1 AW/AR Channel Payload (`AW_PAYLOAD_WIDTH` / `AR_PAYLOAD_WIDTH` = 108 bits)

AW 與 AR 結構相同，以下以 AW 為例。AR 各欄位將 `aw` 前綴替換為 `ar`。

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| awid | `AXI_ID_WIDTH` | [7:0] | Transaction ID |
| awaddr | `AXI_ADDR_WIDTH` | [71:8] | Write address |
| awlen | `AXI_LEN_WIDTH` | [79:72] | Burst length (0\~255) |
| awsize | `AXI_SIZE_WIDTH` | [82:80] | Burst size |
| awburst | `AXI_BURST_WIDTH` | [84:83] | Burst type |
| awcache | `AXI_CACHE_WIDTH` | [88:85] | Cache attributes |
| awlock | `AXI_LOCK_WIDTH` | [89] | Exclusive access |
| awprot | `AXI_PROT_WIDTH` | [92:90] | Protection type |
| awregion | `AXI_REGION_WIDTH` | [96:93] | Memory region identifier |
| awuser | `AXI_USER_WIDTH` | [104:97] | AXI user signal |
| aw_rsvd | derived (3) | [107:105] | Reserved (alignment) |
| | **AW_PAYLOAD_WIDTH** | | |

### 3.2 W Channel Payload (`W_PAYLOAD_WIDTH` = 352 bits)

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| wlast | `AXI_LAST_WIDTH` | [0] | Last beat marker |
| wuser | `AXI_USER_WIDTH` | [8:1] | AXI user signal |
| wdata | `AXI_DATA_WIDTH` | [264:9] | Write data |
| wstrb | `WSTRB_WIDTH` | [296:265] | Write strobe (per-byte enable) |
| wecc | `ECC_WIDTH` | [328:297] | ECC (SECDED per granule) |
| w_rsvd | derived (23) | [351:329] | Reserved |
| | **W_PAYLOAD_WIDTH** | | |

`wecc`：每 `ECC_GRANULE_WIDTH` bits wdata granule 產生 `ECC_PER_GRANULE_WIDTH` bits SECDED ECC，總計 `(AXI_DATA_WIDTH / ECC_GRANULE_WIDTH) × ECC_PER_GRANULE_WIDTH` bits（預設 32 bits）。由 responder 驗證，錯誤透過 `bresp` 回報。

### 3.3 B Channel Payload (`B_PAYLOAD_WIDTH` = 64 bits)

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| bid | `AXI_ID_WIDTH` | [7:0] | Write transaction ID |
| bresp | `AXI_RESP_WIDTH` | [9:8] | Write response status |
| buser | `AXI_USER_WIDTH` | [17:10] | AXI user signal |
| ecc_fail | `ECC_FAIL_WIDTH` | [18] | ECC error detected (SECDED uncorrectable) |
| rsvd_mc_status | `RSVD_MC_STATUS_WIDTH` | [20:19] | Reserved (future multicast status) |
| b_rsvd | derived (43) | [63:21] | Reserved |
| | **B_PAYLOAD_WIDTH** | | |

`ecc_fail` 為 NoC 內部欄位，NI 記錄於 CSR，AXI interface 透過 `bresp` 回報（任一錯誤 → SLVERR）。`buser` 為 AXI user signal pass-through。`rsvd_mc_status` 預留供未來 multicast status 編碼，當前傳輸時設 0、接收端忽略。

### 3.4 R Channel Payload (`R_PAYLOAD_WIDTH` = 352 bits)

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| rlast | `AXI_LAST_WIDTH` | [0] | Last beat marker |
| rid | `AXI_ID_WIDTH` | [8:1] | Read transaction ID |
| rresp | `AXI_RESP_WIDTH` | [10:9] | Read response status |
| ruser | `AXI_USER_WIDTH` | [18:11] | AXI user signal |
| rdata | `AXI_DATA_WIDTH` | [274:19] | Read data |
| recc | `ECC_WIDTH` | [306:275] | ECC (SECDED per granule) |
| r_rsvd | derived (45) | [351:307] | Reserved |
| | **R_PAYLOAD_WIDTH** | | |

`recc`：與 `wecc` 同公式產生（預設 32 bits）。由 requester 驗證，錯誤由軟體處理。

### 3.5 W/R Layout Convention

W 與 R channel 採用相同欄位排列慣例：control fields → data → ECC → reserved。

| Aspect | W Channel | R Channel |
|--------|-----------|-----------|
| Transaction ID | N/A (AXI4 removed WID) | rid (`AXI_ID_WIDTH`) |
| Response status | N/A | rresp (`AXI_RESP_WIDTH`) |
| Byte enable | wstrb (`WSTRB_WIDTH`) | N/A |
| Data | wdata (`AXI_DATA_WIDTH`) | rdata (`AXI_DATA_WIDTH`) |
| ECC | wecc (`ECC_WIDTH`) | recc (`ECC_WIDTH`) |

### 3.6 ECC Design

採用 SECDED（Single Error Correct, Double Error Detect）。

| Parameter | Value (default) |
|-----------|-----------------|
| Data granule | `ECC_GRANULE_WIDTH` (=64) |
| ECC per granule | `ECC_PER_GRANULE_WIDTH` (=8, Hsiao SECDED) |
| Granules per data word | `AXI_DATA_WIDTH / ECC_GRANULE_WIDTH` (=4) |
| Total ECC width | `ECC_WIDTH` (=32) |
| Error correction | 1-bit per granule |
| Error detection | 2-bit per granule |

**End-to-end scope:**

```
Source NI (generate) → Router (pass-through) → ... → Dest NI (check)
```

- **Generate:** Source NI 於 flit 注入時計算 ECC，填入 `wecc`/`recc`
- **Transparent:** Router 不檢查、不修改 ECC（pass-through）
- **Check:** Destination NI 重算 ECC 比對 — 1-bit error 自動校正；2-bit error 設定 `ecc_fail=1`（B channel）或透過 `rresp` 回報

### 3.7 Union Alignment & Utilization

各 channel payload 以 union 方式共享 `PAYLOAD_WIDTH` 空間：

| Channel | Network | Actual Payload | Padding | Flit Total |
|---------|---------|---------------|---------|------------|
| AW | REQ | 108 | 244 | 400 |
| W | REQ | 352 | 0 | 400 |
| AR | REQ | 108 | 244 | 400 |
| B | RSP | 64 | 288 | 400 |
| R | RSP | 352 | 0 | 400 |

**Effective utilization**（非 reserved 欄位佔 `PAYLOAD_WIDTH` 比例，預設值）：

| Channel | Effective Bits | Utilization |
|---------|---------------|-------------|
| W | 329 (last+user+data+strb+ecc) | 93.5% |
| R | 307 (last+id+resp+user+data+ecc) | 87.2% |
| AW | 105 (id+addr+len+size+burst+cache+lock+prot+region+user) | 29.8% |
| AR | 105 | 29.8% |
| B | 19 (id+resp+user+ecc_fail) | 5.4% |

典型 burst write（awlen=15）組成 1×AW + 16×W + 1×B = 18 flits，W flit 佔 89%，整體 padding waste 約 8%。單次傳輸（awlen=0）padding waste 約 50%，但 burst workload 下整體 link utilization 仍屬高效。

---

## 4. Physical Link Format

Request 與 Response 使用獨立 physical link（雙通道 full-duplex），消除 request-response circular dependency，為 protocol deadlock avoidance 的主要機制。每 cycle 傳輸一個完整 flit。

### 4.1 Link Signals

Request link 與 Response link 結構對稱，差異僅在 flit data 欄位名稱（req / rsp）：

| Field | Bit | Width | Description |
|-------|-----|-------|-------------|
| valid | [0] | 1 | Data valid |
| ready | [1] | 1 | Receiver ready |
| flit | `[FLIT_WIDTH+1:2]` | `FLIT_WIDTH` | Flit data (req or rsp) |
| | | **`LINK_WIDTH`** | |

預設 `LINK_WIDTH = 402` bits。兩版 header 的 physical link 寬度相同 — version 差異僅在 header 內部 bit 解釋，不影響 wire width。

### 4.2 Flow Control Modes

| Feature | Handshake (Ver. A) | Credit-Based (Ver. B) |
|---------|--------------------------|--------------------------|
| VC identification | Signal-level (per-VC valid/ready) | Header `vc_id` field |
| Wires per link | `NumVC × LINK_WIDTH` | `LINK_WIDTH` (shared) |
| Wire count (4 VC, default) | 1,608 wires/dir | 402 wires/dir |
| Header overhead | 0 bits | `VC_ID_WIDTH` (=3) (from rsvd) |
| Arbitration | Per-VC at receiver | Muxed at sender |

ASIC 實作推薦 Credit-Based mode — wire count 不隨 VC 數增長，適合高 VC 數與 wire-constrained 場景。Valid/Ready mode 適用 VC ≤ 2 且無需 credit tracking 的簡化設計。

---

## 5. RoB Design Analysis

### 5.1 rob_idx vs axi_id

兩者獨立，用途不同：

| Field | Location | Scope | Purpose |
|-------|----------|-------|---------|
| axi_id | Payload | Per-transaction stream | AXI per-ID ordering |
| rob_idx | Header | Shared across all IDs | RoB entry index |

### 5.2 RoB Architecture

```
                    ┌─────────────────────────────────────┐
                    │         Reorder Buffer              │
                    │  ┌─────┬─────┬─────┬─────┬─────┐   │
                    │  │  0  │  1  │  2  │ ... │ N-1 │   │
                    │  └─────┴─────┴─────┴─────┴─────┘   │
                    │         ↑                           │
                    │     rob_idx                         │
                    └─────────────────────────────────────┘
                              ↑
┌─────────────────────────────┴─────────────────────────────┐
│                    Status Table                            │
│  ┌──────────┬──────────┬──────────┬──────────┐            │
│  │ axi_id=0 │ axi_id=1 │ axi_id=2 │   ...    │            │
│  │ rob_idx  │ rob_idx  │ rob_idx  │          │            │
│  │ rob_req  │ rob_req  │ rob_req  │          │            │
│  └──────────┴──────────┴──────────┴──────────┘            │
└───────────────────────────────────────────────────────────┘
```

每個 NI 擁有獨立 `2^ROB_IDX_WIDTH`-entry RoB（預設 32），所有 outstanding transactions 共享（跨 axi_id 與 destination）。Source NI 於發送 request 時分配 `rob_idx`，response 攜帶相同 index 返回。

同一 `axi_id` 的多筆 outstanding transactions 各佔一個 rob_idx，per-ID ordering 由 Status Table 保證 — 同 ID responses 依 rob_idx 分配順序依序 release。

---

## 6. Functionality Supported by Packet Design

- **XY Routing** — `src_id`, `dst_id` (`X_WIDTH + Y_WIDTH` each)
  - 座標編碼，Router 從 `dst_id` 提取座標進行 X-first then Y routing
  - `src_id` 用於 response 回程路由與 error handling 來源識別
  - 預設支援最大 16×16 mesh（256 nodes）

- **Wormhole Switching & AXI Response Interleaving** — `last` (`LAST_WIDTH`)
  - Packet-level path lock，`last=1` 釋放路徑
  - Single-flit packet（AW, AR, B）恆為 `last=1`；multi-flit（W, R）僅末尾設 `last=1`
  - 支援 AXI read data interleaving：不同 `rid` 的 R packet 可在 packet boundary 交錯傳輸

- **Multi-port Addressing** — `port_id` (`PORT_ID_WIDTH`)
  - 每 Router 最多 `2^PORT_ID_WIDTH` 個 local ports（預設 4：CPU / DMA / MemCtrl / Accelerator）
  - Response 時填入原始 requester 的 `port_id` 確保回程正確

- **QoS Arbitration** — `qos` (`QOS_WIDTH`)
  - `2^QOS_WIDTH` 級優先級（預設 16：0=Best Effort, 15=Real-time Critical）
  - 置於 header LSB 端，支援單級 pipeline partial decode

- **Virtual Channel** — `vc_id` (`VC_ID_WIDTH`, Credit-Based mode), rsvd (Valid/Ready mode)
  - Valid/Ready mode：signal-level VC（多組 valid/ready lines）
  - Credit-Based mode：header-level VC（max `2^VC_ID_WIDTH` VCs，預設 8），搭配 credit-based flow control
  - 用途：Req/Rsp 分離避免 deadlock、traffic class 隔離

- **Out-of-Order Completion** — `rob_req` (`ROB_REQ_WIDTH`), `rob_idx` (`ROB_IDX_WIDTH`)
  - `2^ROB_IDX_WIDTH`-entry RoB per NI（預設 32），跨 axi_id 與 destination 共享
  - 允許 response 亂序返回，由 destination NI 依 `rob_idx` 重排

- **AXI Protocol Mapping** — `axi_ch` (`AXI_CH_WIDTH`), 5 channel payloads (`PAYLOAD_WIDTH` max)
  - 五通道 union-aligned：W/R 佔滿 352b（utilization 87\~93%），AW/AR 108b，B 64b
  - 典型 burst（awlen=15）整體 padding waste 約 8%

- **End-to-End Data Integrity** — `wecc`/`recc` (`ECC_WIDTH`), `ecc_fail` (`ECC_FAIL_WIDTH`)
  - SECDED ECC per granule：1-bit correct, 2-bit detect
  - 保護範圍：Source NI generate → Router pass-through → Dest NI check

- **Dual Physical Network** — Req/Rsp link (`LINK_WIDTH` each)
  - 獨立 full-duplex link，消除 Req-Rsp circular dependency（protocol deadlock avoidance）
  - 每方向每 cycle `AXI_DATA_WIDTH/8` bytes（預設 32），Req/Rsp 對稱且 flit 寬度統一（預設 400b）

- **Reserved for Future Extension** — `rsvd_commtype` (`RSVD_COMMTYPE_WIDTH`), `multicast` (`MULTICAST_WIDTH`), `rsvd_mc_status` (`RSVD_MC_STATUS_WIDTH`)
  - Header 預留 10 bits、B payload 預留 2 bits 供未來通訊類型 / multicast 擴展使用，當前編碼 TBD

---

## 7. Limitations & Out-of-Scope

- **AXI4-Stream / Atomic Operations** — 不在本設計範圍。僅支援 AXI4 五通道（AW/W/AR/B/R）
- **Header integrity** — ECC 僅保護 payload data（wdata/rdata），不涵蓋 header 及 payload metadata（wstrb、axi_id 等）。Header integrity 依賴 physical link layer（wire-level parity 或 CRC）。`dst_id` bit flip 將導致 misrouting
- **Multicast / Reduction** — 本文件不定義；`rsvd_commtype` / `multicast` / `rsvd_mc_status` 為預留欄位，編碼方式 TBD
- **RoB sizing** — `ROB_IDX_WIDTH=5`（32 entries）基於典型 embedded SoC workload（CPU + DMA）。高 outstanding 需求（如 GPU）可能需擴展

---

## Related Documents

- [QoS Design](06_qos.md)
- [Router Specification](03_router.md)
- [Network Interface Specification](04_network_interface.md)
- [Physical Channel Architecture](05_physical_channel.md)
- [Width Converter](10_width_converter.md)
