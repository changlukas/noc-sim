# Flit Format — Parameterized Design

NoC 基本傳輸單元。所有欄位寬度透過 symbol 參數表達，預設值對應目前實作配置（最大 16×16 mesh、AXI 256-bit data）。Multicast 相關欄位保留位置但功能未定義。

---

## 1. Overview

### 1.1 Design Philosophy

採用 **可參數化欄位寬度** 設計：

1. **每個欄位皆有對應 width parameter** — 包含 1-bit flag 在內，命名一致便於跨模組引用。HEADER_WIDTH 與 FLIT_WIDTH 為 derived（自動由欄位 parameter 累加產生）。
2. **預設值對齊 v0.4.0 配置** — 未調參時：
   - `FLIT_WIDTH = 408 bits`
   - `HEADER_WIDTH = 56 bits`
   - `PAYLOAD_WIDTH = 352 bits`
   - `FLIT_ECC_WIDTH = 10 bits`
   - v0.3.0 為 400/48/352/per-granule-32。變動原因見 §3.6。
3. **預留欄位明確標示** — 以下為 future extension 預留位置，當前傳輸時設 0、接收端忽略：
   - `rsvd_commtype` (2b)
   - `multicast` (8b)
   - `rsvd_mc_status` (2b)
   - W/R `*_rsvd`
4. **Multicast 編碼方式 TBD** — 本文件不定義 multicast 路由與 response 行為。
5. **兩層 data integrity**：
   - `route_par` (1b header) per-hop routing 保護
   - `flit_ecc` (10b header default) end-to-end SECDED
   - ECC 已從 v0.3.0 的 per-granule payload (`wecc`/`recc`/`ecc_fail`) 改為 whole-flit syndrome 在 header
   - router fabric 不檢 SECDED 但檢 routing parity

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
| `ROUTE_PAR_WIDTH` | 1 | Even parity over routing-critical fields (`src_id` + `dst_id` + `port_id`); always-on, checked at every router hop. Protects against single-bit errors that would cause misrouting |
| `FLIT_ECC_WIDTH` | 10 | Whole-flit SECDED syndrome covering `header[HEADER_WIDTH-FLIT_ECC_WIDTH-1:0] + payload`. Generated at NMU/NSU egress; checked at NMU/NSU ingress (router fabric does NOT check). Must satisfy SECDED Hamming bound: `2^(FLIT_ECC_WIDTH-1) ≥ FLIT_DATA_WIDTH + FLIT_ECC_WIDTH + 1` where `FLIT_DATA_WIDTH = HEADER_WIDTH - FLIT_ECC_WIDTH + PAYLOAD_WIDTH` |

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

#### Group 4 — ECC (whole-flit SECDED, see §3.6)

The previous per-granule ECC scheme (separate `wecc` / `recc` payload fields per W/R channel with 64-bit granule × 8-bit ECC = 32-bit total) is **superseded**. The new scheme places a single SECDED syndrome over the entire flit (header + payload, excluding the syndrome itself) in the `flit_ecc` header field; see Group 2 above and §3.6.

The legacy ECC parameters are retained as documentation aliases (no functional role in the new scheme):

| Parameter | Status |
|-----------|--------|
| `ECC_GRANULE_WIDTH` | retired (per-granule scheme superseded) |
| `ECC_PER_GRANULE_WIDTH` | retired |
| `ECC_FAIL_WIDTH` | retired (`ecc_fail` payload field also dropped from B; uncorrectable ECC at NSU surfaces as `bresp=SLVERR` in-band, with detailed error type in `ERR_STATUS` CSR) |
| `ECC_WIDTH` | retired; replaced by `FLIT_ECC_WIDTH` (Group 2) |

#### Group 5 — B Channel Reserved

| Parameter | Default | Description |
|-----------|---------|-------------|
| `RSVD_MC_STATUS_WIDTH` | 2 | Reserved for future multicast status extension |

#### Group 6 — Composite / Derived

| Parameter | Default | Formula |
|-----------|---------|---------|
| `WSTRB_WIDTH` | 32 | `AXI_DATA_WIDTH / 8` |
| `HEADER_WIDTH` | 56 | Σ Group 1+2 fields (見 §2.1); was 48 in v0.3.0 (per-granule ECC era) |
| `HEADER_DATA_WIDTH` | 46 | `HEADER_WIDTH - FLIT_ECC_WIDTH`; bits of header protected by `flit_ecc` |
| `AW_PAYLOAD_WIDTH` | 108 | Σ AW fields (見 §3.1) |
| `W_PAYLOAD_WIDTH` | 352 | Σ W fields (見 §3.2); `wecc` removed; `w_rsvd` expanded |
| `AR_PAYLOAD_WIDTH` | 108 | Σ AR fields (見 §3.1) |
| `B_PAYLOAD_WIDTH` | 64 | Σ B fields (見 §3.3); `ecc_fail` removed; `b_rsvd` expanded |
| `R_PAYLOAD_WIDTH` | 352 | Σ R fields (見 §3.4); `recc` removed; `r_rsvd` expanded |
| `PAYLOAD_WIDTH` | 352 | `max(AW, W, AR, B, R)_PAYLOAD_WIDTH` |
| `FLIT_DATA_WIDTH` | 398 | `HEADER_DATA_WIDTH + PAYLOAD_WIDTH`; bits protected by `flit_ecc` |
| `FLIT_WIDTH` | 408 | `HEADER_WIDTH + PAYLOAD_WIDTH`; was 400 in v0.3.0 |
| `LINK_WIDTH` | 410 | `FLIT_WIDTH + 2` (valid + ready) |

### 1.3 Flit Structure

```
  HEADER_WIDTH-1                    0   PAYLOAD_WIDTH-1                   0
  ┌────────────────────────────────────┬──────────────────────────────────┐
  │       Header (HEADER_WIDTH)        │      Payload (PAYLOAD_WIDTH)     │
  └────────────────────────────────────┴──────────────────────────────────┘
  |<──────────────────── Flit: FLIT_WIDTH ─────────────────────────────>|
```

v0.4.0 預設配置：

- Header = 56 bits
- Payload = 352 bits
- Flit = 408 bits

(v0.3.0 為 48/352/400。Header 從 48→56 因加 `route_par` (1b) + `flit_ecc` (10b) 並回收 3-bit MSB `rsvd` 槽位，淨 +8。)

所有 AXI channel 共用 `FLIT_WIDTH`。較短 payload 以 zero-padding 對齊至 `PAYLOAD_WIDTH`。Request 與 Response flit 寬度對稱。

Flit 內 wdata/rdata 採 little-endian byte ordering（byte address 0 = data[7:0]），與 AXI bus 一致。Header 與 payload metadata 使用 MSB-first bit numbering（[MSB:LSB]），遵循 SystemVerilog 慣例。

---

## 2. Header Format

Header 預設 `HEADER_WIDTH = 56 bits`（v0.3.0 為 48 bits。v0.4.0 新增 `route_par` + `flit_ecc` 欄位）。

提供兩種配置：

- Valid/Ready mode (No-VC)
- Credit-Based mode (With-VC)

兩者僅 vc_id 槽位語意不同，欄位位置與寬度完全相同。

### 2.1 Bit Allocation

按 pipeline stage 分組（LSB→MSB）：arbitration → routing → wormhole control → reserved future → ECC。

| Field | Width Symbol | Default Range | Stage | Description |
|-------|--------------|---------------|-------|-------------|
| qos | `QOS_WIDTH` | [3:0] | arbitration | QoS priority |
| axi_ch | `AXI_CH_WIDTH` | [6:4] | arbitration | AXI channel type (5 channels: AW/W/AR/B/R) |
| src_id | `X_WIDTH + Y_WIDTH` | [14:7] | routing | Source node ID (X+Y coordinate) |
| dst_id | `X_WIDTH + Y_WIDTH` | [22:15] | routing | Destination node ID (X+Y coordinate) |
| port_id | `PORT_ID_WIDTH` | [24:23] | routing | Target local port index at destination router |
| vc_id ‡ | `VC_ID_WIDTH` | [27:25] | routing | Virtual Channel ID (Credit-Based mode) |
| route_par | `ROUTE_PAR_WIDTH` | [28] | routing | Even parity over `src_id` + `dst_id` + `port_id`; checked at every router hop |
| last | `LAST_WIDTH` | [29] | wormhole | Packet end marker (1 = last flit of packet) |
| rob_req | `ROB_REQ_WIDTH` | [30] | wormhole | RoB request flag (NMU sets; 1 = RoB allocate) |
| rob_idx | `ROB_IDX_WIDTH` | [35:31] | wormhole | RoB entry index (`rob_req=1` valid) |
| rsvd_commtype | `RSVD_COMMTYPE_WIDTH` | [37:36] | reserved | Reserved (future communication type extension) |
| multicast | `MULTICAST_WIDTH` | [45:38] | reserved | Reserved (future multicast destination encoding) |
| flit_ecc | `FLIT_ECC_WIDTH` | [55:46] | ECC | Whole-flit SECDED syndrome (covers `header[45:0]` + `payload`) |
| | **HEADER_WIDTH** = 56 | | | |

**‡ Version-dependent field:**

- **Valid/Ready mode (No-VC):**
  - `vc_id` 槽位（[27:25]）作為 reserved，傳送時設為 0、接收端忽略
  - NUM_VC > 1 時透過 signal-level 多組 valid/ready/credit array 實現 VC，不靠 header 識別
- **Credit-Based mode (With-VC):**
  - `vc_id` 攜帶 `VC_ID_WIDTH` bits（最多 8 VC），與 credit-based flow control 搭配
  - router 用 `vc_id` 配對對應 VC FIFO

### 2.0 Field Ordering Rationale

按 pipeline access timing 分組：
- **Stage 1 (arbitration)**: `qos`, `axi_ch` — 第一級流水線就要做仲裁判決，置於 LSB 端方便 partial decode
- **Stage 2 (routing)**: `src_id` / `dst_id` / `port_id` / `vc_id` / `route_par` — 路徑決策同時做 parity 檢查
- **Stage 3 (wormhole control)**: `last` / `rob_req` / `rob_idx` — 路徑鎖控與 response 重排
- **Reserved**: `rsvd_commtype` / `multicast` — future extensions，置於 MSB 端方便 future bump 不影響既有 bit position
- **ECC (last)**: `flit_ecc` — 必須在所有其他欄位 settled 後才能算出 syndrome，置於 MSB 端最後

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

#### 2.2.7 VC ID (`VC_ID_WIDTH` = 3 bits)

支援最多 `2^VC_ID_WIDTH` 個 Virtual Channels（預設 8）。

- **Credit-Based mode (With-VC):** `vc_id` 攜帶 VC 索引，搭配 credit-based flow control。Router 用 `vc_id` 配對對應 VC FIFO。
- **Valid/Ready mode (No-VC):** `vc_id` 槽位作為 reserved，傳送時設為 0、接收端忽略。NUM_VC > 1 時透過 signal-level 多組 valid/ready/credit array 實現 VC，不靠 header 識別。

VC 用途：請求/回應分離避免 deadlock、traffic class 隔離、QoS 分桶（per `06_qos.md` 與 NMU §VC selection 政策）。

#### 2.2.8 Routing Parity: route_par (`ROUTE_PAR_WIDTH` = 1 bit)

Always-on even parity over routing-critical fields：

- `src_id` (8 bits) + `dst_id` (8 bits) + `port_id` (2 bits) = 18 bits
- NMU 在 flit 注入時計算
- **每一跳 router 在 stage 2 routing 階段重新計算並比對**，不一致則丟棄 flit + 觸發 fatal error report（見 §3.6）

設計用意：保護 routing 過程中 single-bit error 造成 misroute（path bit-flip 把封包導到錯誤 node）。雖然 `flit_ecc` (SECDED) 在 endpoint 也能偵測，但 routers 不檢查 flit_ecc（per AMD §Data Integrity）— 中途 routing-critical bit-flip 必須靠 route_par 早期攔截。

#### 2.2.9 Reserved: rsvd_commtype (`RSVD_COMMTYPE_WIDTH` = 2 bits)

預留欄位，當前所有傳輸視為 Unicast。寬度預設 2 bits 為未來通訊類型擴展（例如 multicast、reduction、broadcast、anycast — 共 4 種類別）保留位置，編碼方式 **TBD**。傳輸時設為 0，接收端忽略。

#### 2.2.10 Reserved: multicast (`MULTICAST_WIDTH` = 8 bits)

預留欄位。寬度預設 8 bits 為未來 multicast 編碼方案保留位置（例如 8-bit destination bitmask 或 4×4 bounding box 或 256-entry table index），**編碼方式與路由行為均未定義**。傳輸時設為 0，接收端忽略。

#### 2.2.11 ECC: flit_ecc (`FLIT_ECC_WIDTH` = 10 bits)

Whole-flit SECDED syndrome。範圍：覆蓋 `header[HEADER_WIDTH-FLIT_ECC_WIDTH-1:0]` + `payload`，**但不包含 `flit_ecc` 自己**。

- **Generate**: NMU 在 flit 注入 `noc_*_o` 之前計算
- **Pass-through**: routers 不檢查 `flit_ecc`（router fabric 只檢 `route_par`）
- **Check**: NSU/NMU 在 ingress 收到 flit 時重算對比。single-bit error 自動校正。double-bit error 觸發 fatal error report

**SECDED Hamming bound 約束**: `2^(FLIT_ECC_WIDTH-1) ≥ FLIT_DATA_WIDTH + FLIT_ECC_WIDTH + 1`。Default 配置：`FLIT_DATA_WIDTH = 46 + 352 = 398`，需要 `FLIT_ECC_WIDTH ≥ 10`（`2^9 = 512 ≥ 409` ✓）。Integrator 若擴 HEADER 或 PAYLOAD 至 ≥ 502 bit，需把 `FLIT_ECC_WIDTH` 從 10 升到 11。

詳見 §3.6 ECC Design。

### 2.3 Bit Layout Diagrams (default values)

#### Valid/Ready Mode (No-VC; `vc_id` 槽位 reserved)

```
55              46 45                 38 37 36 35     31 30 29 28 27 25 24 23 22       15 14        7 6  4 3   0
┌─────────────────┬─────────────────────┬─────┬─────────┬──┬──┬──┬─────┬─────┬──────────┬──────────┬─────┬─────┐
│   flit_ecc      │     multicast       │rsvd_│ rob_idx │rb│ls│rp│vc_id│port │  dst_id  │  src_id  │ axi │ qos │
│      10b        │       8b            │comm │   5b    │req│  │  │ 3b  │id 2b│    8b    │    8b    │ch 3b│  4b │
│ (SECDED over    │   (reserved)        │type │         │1b│1b│1b│(rsvd│     │          │          │     │     │
│  header[45:0]+  │                     │ 2b  │         │  │  │  │ in  │     │          │          │     │     │
│  payload)       │                     │     │         │  │  │  │ VR) │     │          │          │     │     │
└─────────────────┴─────────────────────┴─────┴─────────┴──┴──┴──┴─────┴─────┴──────────┴──────────┴─────┴─────┘
```
（其中 `rb` = rob_req, `ls` = last, `rp` = route_par）

#### Credit-Based Mode (With-VC; `vc_id` 攜帶 VC 索引)

```
55              46 45                 38 37 36 35     31 30 29 28 27 25 24 23 22       15 14        7 6  4 3   0
┌─────────────────┬─────────────────────┬─────┬─────────┬──┬──┬──┬─────┬─────┬──────────┬──────────┬─────┬─────┐
│   flit_ecc      │     multicast       │rsvd_│ rob_idx │rb│ls│rp│vc_id│port │  dst_id  │  src_id  │ axi │ qos │
│      10b        │       8b            │comm │   5b    │req│  │  │ 3b  │id 2b│    8b    │    8b    │ch 3b│  4b │
│                 │                     │ 2b  │         │1b│1b│1b│ ✓✓ │     │          │          │     │     │
└─────────────────┴─────────────────────┴─────┴─────────┴──┴──┴──┴─────┴─────┴──────────┴──────────┴─────┴─────┘
```

兩 mode 唯一差異：`vc_id` 槽位語意。

- VR mode：`vc_id` 槽位作為 reserved
- CB mode：`vc_id` 攜帶 VC 索引

其他 bit position 完全相同。Wire-level 寬度 = `LINK_WIDTH = HEADER_WIDTH + PAYLOAD_WIDTH + 2 = 410 bits`。

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
| w_rsvd | derived (55) | [351:297] | Reserved |
| | **W_PAYLOAD_WIDTH** | | |

注意：v0.3.0 的 per-granule `wecc` 欄位已**撤掉**，改由 header `flit_ecc` 提供 whole-flit SECDED 保護（見 §3.6）。原 `wecc` 佔的 32 bits 併入 `w_rsvd`（從 23 → 55 bits）作為 future extension 預留。

### 3.3 B Channel Payload (`B_PAYLOAD_WIDTH` = 64 bits)

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| bid | `AXI_ID_WIDTH` | [7:0] | Write transaction ID |
| bresp | `AXI_RESP_WIDTH` | [9:8] | Write response status |
| buser | `AXI_USER_WIDTH` | [17:10] | AXI user signal |
| rsvd_mc_status | `RSVD_MC_STATUS_WIDTH` | [19:18] | Reserved (future multicast status) |
| b_rsvd | derived (44) | [63:20] | Reserved |
| | **B_PAYLOAD_WIDTH** | | |

`buser` 為 AXI user signal pass-through。`rsvd_mc_status` 預留供未來 multicast status 編碼，當前傳輸時設 0、接收端忽略。

注意：v0.3.0 的 `ecc_fail` payload field 已**撤掉**。NSU 偵測到 W ECC uncorrectable 時：

- **直接以 `bresp=SLVERR` in-band 回報**
- 在 `ERR_STATUS` CSR 寫入對應 ECC error bit（per `protocol_rules.md` `NOC_FLIT_HDR_FLIT_ECC_*` 規則）

Master 端只需讀 `bresp` 判斷成功失敗。具體 error 類別查 CSR。

### 3.4 R Channel Payload (`R_PAYLOAD_WIDTH` = 352 bits)

| Field | Width Symbol | Default Range | Description |
|-------|--------------|---------------|-------------|
| rlast | `AXI_LAST_WIDTH` | [0] | Last beat marker |
| rid | `AXI_ID_WIDTH` | [8:1] | Read transaction ID |
| rresp | `AXI_RESP_WIDTH` | [10:9] | Read response status |
| ruser | `AXI_USER_WIDTH` | [18:11] | AXI user signal |
| rdata | `AXI_DATA_WIDTH` | [274:19] | Read data |
| r_rsvd | derived (77) | [351:275] | Reserved |
| | **R_PAYLOAD_WIDTH** | | |

注意：v0.3.0 的 per-granule `recc` 欄位已**撤掉**，改由 header `flit_ecc` 提供 whole-flit SECDED 保護。原 `recc` 佔的 32 bits 併入 `r_rsvd`（從 45 → 77 bits）作為 future extension 預留。NMU 收到 R flit 時，flit_ecc 偵測到 uncorrectable 直接以 `rresp=SLVERR` 回報 master。

### 3.5 W/R Layout Convention

W 與 R channel 採用相同欄位排列慣例：control fields → data → reserved。**ECC 已從 payload 移到 header `flit_ecc`**，per-channel payload 不再含 ECC field。

| Aspect | W Channel | R Channel |
|--------|-----------|-----------|
| Transaction ID | N/A (AXI4 removed WID) | rid (`AXI_ID_WIDTH`) |
| Response status | N/A | rresp (`AXI_RESP_WIDTH`) |
| Byte enable | wstrb (`WSTRB_WIDTH`) | N/A |
| Data | wdata (`AXI_DATA_WIDTH`) | rdata (`AXI_DATA_WIDTH`) |
| ECC | (moved to header `flit_ecc`) | (moved to header `flit_ecc`) |

### 3.6 ECC Design (whole-flit SECDED + dst_id parity)

採兩層保護機制，協同覆蓋 routing 與 data integrity：

**Layer 1 — `route_par` (header bit, 1-bit even parity)**

| Property | Value |
|----------|-------|
| Coverage | `src_id` (8b) + `dst_id` (8b) + `port_id` (2b) = 18 bits routing-critical fields |
| Generated by | Source NI (NMU on request, NSU on response) |
| Checked by | **每一跳 router** — fatal error if mismatch |
| End-to-end check | NMU/NSU 也會重算驗證 |
| Detection | Single-bit (any odd-bit) error in routing fields |
| Purpose | 即時偵測 routing-critical bit-flip，避免 misroute 到錯誤 node |

**Layer 2 — `flit_ecc` (header field, SECDED syndrome)**

| Property | Value (default) |
|----------|-----------------|
| Width | `FLIT_ECC_WIDTH = 10 bits` (parameter) |
| Coverage | `header[HEADER_WIDTH-FLIT_ECC_WIDTH-1:0]` + `payload` = `FLIT_DATA_WIDTH = 398 bits` |
| Generated by | Source NI on flit egress |
| Checked by | **僅 destination NI**（router fabric 不檢查 `flit_ecc`，per AMD §Data Integrity） |
| Detection | 1-bit correct, 2-bit detect (SECDED) |
| Hamming bound | `2^(FLIT_ECC_WIDTH-1) ≥ FLIT_DATA_WIDTH + FLIT_ECC_WIDTH + 1` |
| Purpose | End-to-end flit integrity; 偵測 NoC fabric 內部任何 bit-level corruption |

**Two-layer scope diagram**:

```
Source NI                  Router 1          Router 2          Router N          Dest NI
  ↓                          ↓                 ↓                 ↓                 ↓
Generate                  Check              Check             Check          Check + Correct
  route_par                 route_par          route_par         route_par      route_par
  flit_ecc                  (skip)             (skip)            (skip)         flit_ecc (SECDED)
  └────────────────────────[ pass-through unchanged ]──────────────────────────┘
```

- **route_par failures**: router immediately reports fatal interrupt → flit dropped or quarantined（per `protocol_rules.md` `NOC_ROUTER_ROUTE_PAR_CHECK`）
- **flit_ecc failures at endpoint**: 1-bit auto-correct (transparent to AXI); 2-bit→`bresp/rresp = SLVERR` + log `ERR_STATUS` ECC bit

**Why two layers**:
- `route_par` 提供 hop-level 即時保護（router 不需做 SECDED 仍能擋掉 misroute）
- `flit_ecc` 提供 end-to-end 完整保護（router 不負擔 ECC 計算成本，整段 NoC 雙端 NMU/NSU 各算一次即可）

**Parameter sensitivity**: 若 `HEADER_WIDTH` 或 `PAYLOAD_WIDTH` 擴大使 `FLIT_DATA_WIDTH ≥ 502` bits，integrator 必須將 `FLIT_ECC_WIDTH` 從 10 升到 11（per Hamming bound 約束 — `2^9 = 512 ≥ 502 + 10 + 1 = 513` 不成立）。

### 3.7 Union Alignment & Utilization

各 channel payload 以 union 方式共享 `PAYLOAD_WIDTH` 空間：

| Channel | Network | Actual Payload | Padding | Flit Total |
|---------|---------|---------------|---------|------------|
| AW | REQ | 108 | 244 | 408 |
| W | REQ | 297 (last+user+data+strb) | 55 (rsvd) | 408 |
| AR | REQ | 108 | 244 | 408 |
| B | RSP | 20 (id+resp+user+rsvd_mc_status) | 44 | 408 |
| R | RSP | 275 (last+id+resp+user+data) | 77 | 408 |

**Effective utilization**（非 reserved 欄位佔 `PAYLOAD_WIDTH` 比例，預設值。ECC 已從 payload 移到 header，故 utilization 數字下修）：

| Channel | Effective Payload Bits | Utilization |
|---------|----------------------|-------------|
| W | 297 (last+user+data+strb) | 84.4% |
| R | 275 (last+id+resp+user+data) | 78.1% |
| AW | 105 (id+addr+len+size+burst+cache+lock+prot+region+user) | 29.8% |
| AR | 105 | 29.8% |
| B | 18 (id+resp+user) | 5.1% |

典型 burst write（awlen=15）組成 1×AW + 16×W + 1×B = 18 flits，W flit 佔 89%。

v0.4.0 vs v0.3.0 比較：

- Header 從 48 → 56 bits（+16.7% header overhead）
- ECC 不再佔 payload（從 32-bit `wecc`/`recc` 移到 10-bit header `flit_ecc`）
- W utilization 從 93.5% → 84.4%。註：v0.3.0 把 `wecc` 算進 effective bits（329/352），v0.4.0 因 ECC 已移出 payload，effective 只算 `last+user+data+strb`（297/352）。實際 data bandwidth 不變（W 維持 297 bit data）。
- Burst total wire bytes：18×400 = 900 bytes → 18×408 = 918 bytes，+2% wire overhead，換取更乾淨的 ECC 抽象與 future-extension 預留

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

預設 `LINK_WIDTH = 410` bits。

- v0.3.0 為 402 bits
- Header 從 48→56 bit 淨增加 8 bit (加 route_par(1) + flit_ecc(10), 回收 v0.3.0 上方 3-bit `rsvd`)
- Flit 從 400→408 bit
- 加 valid + ready 2 bit 後共 410 bit

兩 mode (VR / CB) 的 physical link 寬度相同。`vc_id` 槽位語意差異不影響 wire width。

### 4.2 Flow Control Modes

| Feature | Valid/Ready (VR) | Credit-Based (CB) |
|---------|------------------|-------------------|
| VC identification | Signal-level per-VC valid/ready arrays | Header `vc_id` field shared physical link |
| Wires per link | `NUM_VC × LINK_WIDTH` | `LINK_WIDTH` (shared) |
| Wire count (NUM_VC=4, default) | 1,640 wires/dir | 410 wires/dir |
| Header overhead | 0 bits | `VC_ID_WIDTH` = 3 bits (allocated as explicit field, see §2.1) |
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
  - 五通道 union-aligned：W/R 佔滿 352b（utilization 78\~84%，ECC 已從 payload 移到 header），AW/AR 108b，B 64b
  - 典型 burst（awlen=15）整體 padding waste 約 8%

- **Two-Layer Data Integrity** — `flit_ecc` (`FLIT_ECC_WIDTH`), `route_par` (`ROUTE_PAR_WIDTH`)
  - **Layer 1 routing protection**: 1-bit even parity over (`src_id` + `dst_id` + `port_id`); checked at every router hop; fatal interrupt on mismatch
  - **Layer 2 end-to-end SECDED**: whole-flit syndrome covering header (excluding `flit_ecc` itself) + payload; 1-bit correct + 2-bit detect; checked at NMU/NSU endpoint only (router fabric pass-through)
  - 保護範圍：Source NI generate → Router check route_par each hop / pass-through flit_ecc → Dest NI check flit_ecc
  - `FLIT_ECC_WIDTH` parameter 化以支援不同 flit size 的 SECDED bound（`2^(FLIT_ECC_WIDTH-1) ≥ FLIT_DATA_WIDTH + FLIT_ECC_WIDTH + 1`）

- **Dual Physical Network** — Req/Rsp link (`LINK_WIDTH` each)
  - 獨立 full-duplex link，消除 Req-Rsp circular dependency（protocol deadlock avoidance）
  - 每方向每 cycle `AXI_DATA_WIDTH/8` bytes（預設 32），Req/Rsp 對稱且 flit 寬度統一（預設 408b）

- **Reserved for Future Extension** — `rsvd_commtype` (`RSVD_COMMTYPE_WIDTH`), `multicast` (`MULTICAST_WIDTH`), `rsvd_mc_status` (`RSVD_MC_STATUS_WIDTH`), W/R/B `*_rsvd`
  - Header 預留 10 bits（commtype 2 + multicast 8）、B payload 預留 2 bits（mc_status）供未來通訊類型 / multicast 擴展使用
  - W payload 預留 55 bits、R payload 預留 77 bits（從原 wecc/recc 位置回收）供未來 AXI feature extension（例如 ATOPs payload、AXI5 sideband）

---

## 7. Limitations & Out-of-Scope

- **AXI4-Stream / Atomic Operations** — 不在本設計範圍。僅支援 AXI4 五通道（AW/W/AR/B/R）
- **Multicast / Reduction** — 本文件不定義；`rsvd_commtype` / `multicast` / `rsvd_mc_status` 為預留欄位，編碼方式 TBD
- **RoB sizing** — `ROB_IDX_WIDTH=5`（32 entries）基於典型 embedded SoC workload（CPU + DMA）。高 outstanding 需求（如 GPU）可能需擴展，但需同步調整 flit header（`ROB_IDX_WIDTH` 與其他 reserved field 重新分配）

---

## Related Documents

- [QoS Design](06_qos.md)
- [Router Specification](03_router.md)
- [Network Interface Specification](04_network_interface.md)
- [Physical Channel Architecture](05_physical_channel.md)
- [Width Converter](10_width_converter.md)
