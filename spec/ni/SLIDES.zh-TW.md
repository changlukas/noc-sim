# NI 投影片內容（繁中版）

本檔為唯一作業版本。技術名詞、訊號名、暫存器名、規則 ID、AMD pg313 verbatim 引用全部保留英文。

- **聽眾**：內部 review（熟悉 AXI4 + 一般 NoC 概念的工程師，非 Versal 專案背景）。
- **風格**：每張 slide 用 Takeaway-first + Why / What / How 階層式組織。Why 為什麼要這個機制，What 提供什麼，How 怎麼運作。Trade-off 在有明確設計選擇時獨立列出。Speaker notes 放 PPT 的 speaker-notes 面板。
- **投影片數**：12 張。
- **drafting 狀態**：A5 wave 2026-05-08，Why / What / How 重整完成。

**Drafting 進度：**

| Batch | Slides | 狀態 |
|---|---|---|
| 1 | 1-6（Title / Components / NMU overview / Address Map / QoS / ECC） | ✓ Why/What/How |
| 2 | 7-12（RoB / NSU overview / Excl Monitor / Downsize / Credit / Closing） | ✓ Why/What/How |

---

## Slide 1 — Title + Scope

> **AXI4-over-NoC Network Interface**
>
> Design walkthrough — v0.4.0, A5 wave 2026-05-08

**Takeaway**：本 deck 介紹 AXI4-over-NoC NI block — per-tile single-chimney 設計、章節跟 AMD pg313 NoC Architecture 對齊、跟 AMD 不同的設計差異點明確標出。

**範圍：**

- 每個 tile 一個 NI，single-chimney（NMU + NSU 共用一對 NoC link）。
- 本投影片只涵蓋 NI 範圍。NoC fabric router (NPS) 為另一份 spec。
- 章節順序：AMD pg313 NoC Architecture。

**Speaker notes：** 本 deck 介紹 AXI4-over-NoC Network Interface 這個 block。NI 是 per-tile 的 chimney，把 AXI4 traffic 轉換進 NoC flit、反向亦同 — manager-side ingress (NMU) 與 subordinate-side egress (NSU) 都住在同一個 NI 內。我們依 AMD pg313 章節順序排列，方便熟悉 Versal NoC 的 reviewer 直接 cross-reference。跟 AMD 不同的設計選擇都會明確標出。Router（AMD 用 NPS 一詞）是相鄰的基礎建設，spec 另立。

---

## Slide 2 — NoC Components Overview

**Takeaway**：NI = NMU + NSU + CSR + irq_o，per-tile single-chimney（NMU 與 NSU 共用一對 NoC link）。

### Why — 為什麼有 NI 這個 boundary

- AXI4 是 point-to-point protocol，M × N 全互連 crossbar 在 die area / routing / power 撐不住。
- NoC 用 packet-switched 把連線壓到 O(M+N)，但不自動提供 AXI4 的 ordering / integrity / flow control 保證。
- NI 是這層 protocol boundary。

**System context (AMD pg313 verbatim):**

> *"An AXI master sends read/write requests to a connected NoC access point (NMU). The NMU relays the requests through a set of NoC packet switches (NPSs) before the requests reach a destination (NoC slave unit NSU or output port)."*
> — AMD pg313 §NoC Communication

### What — NI 內部組成

- **NMU**（Network Manager Unit）— 從 local AXI master 收 request，注入 NoC 變成 request flit。
- **NSU**（Network Subordinate Unit）— 從 NoC 收 request flit，driving local AXI slave。
- **CSR file** — 軟體可見的 runtime control（QoS / Probes / Errors / Quiesce / Exclusive Monitor clear）。
- `irq_o` — 單一 level-sensitive 中斷給 host CPU。

### How — Per-tile single-chimney 模式

| Link | NMU | NSU |
|---|---|---|
| Outbound request | drives | — |
| Inbound request | — | samples |
| Outbound response | — | drives |
| Inbound response | samples | — |

- NMU 與 NSU 可獨立 enable。
- 每個 tile 一個 NI。
- Tile 內若有多個 IP（CPU + DMA + accelerator），先經上游 AXI crossbar 多工後再進 NI — per-IP 識別靠 AXI ID，flit header 不另外加欄位，flit 格式更精簡。

**Visual asset：** Top-level block diagram — 一個 tile 內顯示 CPU / DMA / accelerator 在左 → 上游 AXI crossbar → NI block (NMU + NSU + CSR) → NoC fabric。Response path 用虛線回繪，`irq_o` 線拉出去到 CPU。

**Speaker notes：** NI 是 host 端 AXI4 protocol 與 fabric 端 NoC flit protocol 的邊界。NI 內部有兩個功能上獨立的半邊 — NMU 與 NSU — 共用實體 NoC link 線（single-chimney）。兩半邊在 NI 內部不會直接溝通 — 唯一的耦合點是共用的 link。這個設計對齊 FlooNoC `floo_axi_chimney.sv` topology 與 AMD pg313 的 NMU + NSU 拆分。Tile 內多 IP 時，由上游 AXI crossbar 仲裁。進入 NoC 之後，這些 IP 用 AXI ID 區分，沒有額外加 per-IP flit header 欄位，這讓 flit 格式保持精簡。CSR file 是軟體看 NI 的窗口：QoS configuration、performance probes、error status（v0.4.0 共 3 個 RW1C event-class bits）、NMU 的 quiesce control、NSU 的 Exclusive Monitor clear trigger。`irq_o` 是單一 level-sensitive 線，當任何 unmasked error-status bit set 就 assert。

---

## Slide 3 — NMU (Network Manager Unit) overview

**Takeaway**：NMU 進行 AXI to NoC protocol 轉換，負責 ordering / integrity / flow control 等。

### Why — NMU 在 master 端要做什麼

- AXI4 master 不直接連 slave，要透過 NoC。
- AXI transaction 必須 packetize 成 flit 才能在 NoC 上路由。
- NoC 的 OoO / 損壞 / backpressure 都要 NMU 處理或在 NMU 端看見。

### What — NMU 提供（per AMD pg313 §NoC Master Unit; adapted to our spec where indicated）

- Asynchronous clock domain crossing and rate matching between the AXI master and the NoC.
- Conversion from/to AXI protocol to NoC flit format. *(adapted: AMD writes "NPP")*
- Address matching and route control. *(→ Slide 4)*
- WRAP, INCR, and FIXED burst support.
- Read re-tagging to allow out of order service and prevent interconnect blocking. *(→ Slide 7)*
- Write order enforcement.
- Ingress QoS control. *(→ Slide 5)*
- Handling of the AXI exclusive access feature.
- Configurable AXI data width: 64, 128, 256, or 512 bits. *(adapted: we drop 32-bit, AXI4-Stream not supported)*
- Up to 32 outstanding AXI reads and 32 outstanding AXI writes. *(matches AMD pg313 default)*
- Two-layer ECC integrity — per-hop routing parity + end-to-end whole-flit SECDED. *(→ Slide 6, our addition beyond AMD's NMU bullet list)*

**AMD verbatim（inline）：** *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*（AMD pg313 §NoC Master Unit）

### How — Block diagram + dataflow

Block diagram（右側，ref.jpg 風 — 簡單方框）：

```
AXI Slave I/F                  NMU                        NoC
(Async Boundary)
  AW ────►   ┌────────────────────────────────────┐
  W  ────►   │  Address Map  →  Packetizing       │
  AR ────►   │                  ↓                 │  ──►
  B  ◄────   │  ECC Gen     →   QoS Order Control │
  R  ◄────   │                  ↓                 │  ◄──
             │  De-packetizing  VC Mapping        │
             │      ↑           ↓                 │
             │  ECC Check  ←  Read Re-Ordering    │
             └────────────────────────────────────┘
```

- AW / AR 從左進 → Address Map 解出目的 tile ID → Packetizing 組 payload → QoS Order Control 配 qos → ECC Gen 附 integrity → VC Mapping 選 egress VC → 注入 `noc_req_o`。
- Response 路徑反向：`noc_rsp_i` → ECC Check → Read Re-Ordering（per-AXI-ID 順序）→ De-packetizing → AXI master。
- 每個 sub-block 都是 registered stage，request-to-NoC critical path 短（幾個 cycle）+ async FIFO 過 NoC clock domain。

**Speaker notes：** 走一遍資料流。AW/AR 從左邊進來 → Address Map 解出目的 tile 的 ID → Packetizing 組 flit payload → QoS Order Control 配上 per-flit qos 欄位 → ECC Gen 附上 per-hop routing parity bit 與 whole-flit SECDED syndrome → VC Mapping 選 egress virtual channel → flit 注入到 outbound request link。每個 sub-block 都是 registered stage，request-to-NoC 端到端 critical path 短（幾個 cycle），加上 async FIFO 過 NoC clock domain。Response path 反過來：inbound response flit → ECC Check（single-bit 沉默修正、double-bit 紀錄）→ Read Re-Ordering 強制 per-AXI-ID response 順序 → De-packetizing 還原 B/R 給 AXI master。Outstanding count 與 AMD 一致（32）。不 chop bursts，一張 wide flit 把一個 AXI message 端到端帶完。Wide flit 格式讓 AMD 的 256-byte chopping 沒必要。

---

## Slide 4 — Address Map mechanism

**Takeaway**：NoC bus 需依賴座標將封包送到目的地。NI 內部提供 3 種 address-to-coordinate 轉換方式：XY-routed、Source-routed、ID-table。

### Why — 為什麼需要 address translation

- AXI master 的 awaddr 是純地址，不帶「這要去哪個 tile」資訊。
- NoC fabric 要知道 destination 才能路由 flit。
- NMU 要做這層翻譯：address → (dst_id, local_addr)。

### What — 三種 routing mode（設計階段選定）

- **XY-routed**（預設）：抽 awaddr 的 X/Y bit → 直接成 dst_id — 規則 2D mesh
- **Source-routed**：path 預先寫入 flit header — 靜態 topology / 設計階段最佳化
- **ID-table**（SAM）：System Address Map lookup → dst_id — 多區段 address space / 軟體定義 region

### How — 預設配置 + bit-field 抽取

- 預設 `X_WIDTH = 4 bits`、`Y_WIDTH = 4 bits` → 8-bit dst_id，最大 16 × 16 mesh = 256 tiles。
- Address bit 抽取的 offset 每軸都可在設計階段配置。

Bit-field 範例（XY-routed mode、預設 offsets）：

```
   awaddr [63:0]
            │
            ├─ bits [39:36] → Y 座標（4 bits）
            ├─ bits [35:32] → X 座標（4 bits）
            └─ bits [31:0]  → local address（透傳到目的 NSU）
```

- SAM table 是設計階段參數，v0.4.0 不支援 runtime 修改（要改就重 elaborate）。
- Address translation 與 packetizing 平行跑，不額外多 pipeline stage。

**Visual asset：** `awaddr` bit-field 分解圖（上面那張 ASCII tree，PPT 內重畫成乾淨方塊）。

**Speaker notes：** 三種 routing mode 給整合者依不同 topology 彈性。XY-routed 是規則 mesh 的預設 — 快、簡單、deterministic。Source-routed 適合在 flit 構建時就知道 route 的 pre-computed 路徑。ID-table 透過設計階段 SAM lookup table 把 AXI address 區段對到目的 tile ID — 適用於 address space 被切成不同 tile 各自區段的情境。預設配置下，4-bit X 加 4-bit Y 支援最大 16 × 16 mesh、256 個目的 tile。Address bit offset 是每軸可配置的設計階段參數 — 整合者依自己 address space 的 layout 決定座標放在 `awaddr` 哪幾位元。`local address` 部分會原封不動透傳到目的 NSU，由 NSU 拿來 driving local AXI slave。我們刻意選了統一的 3-mode selector，而不是 AMD Versal 的 Master-Specified ID + Re-mapping 雙機制（那綁 PL-interconnect 耦合）。

---

## Slide 5 — QoS Generator

**Takeaway**：不同 master 的 traffic profile 與 SLA 需求不同。NMU ingress 的 QoS Generator 提供 4 mode shaping 策略，讓整合者依 master 行為選用。

### Why — 為什麼需要 QoS shaping

- NoC 上多 master 共用 fabric。
- 直接信任 master 提供的 `axi_*qos` 不可靠 — 不是每個 master 都會正確設定 qos。
- 需要在 NMU ingress 做 shaping。

### What — 四種 mode（runtime CSR 切換）

| Mode | 行為 |
|---|---|
| Bypass | 直接 pass through AXI awqos / arqos |
| Fixed | 每張 flit 都用 CSR 設定的固定值覆蓋 |
| Bandwidth-limiter | 流量超過設定 bandwidth 時降 priority |
| Urgency-regulator | 對 bandwidth target 做 feedback control 升級 urgency |

### How — 部署位置 + 典型 per-master 對映

- 實作位置：NMU ingress（FlitPack 後、Injection Buffer 前）。
- NSU 不重算 QoS。Response flit 透過 MetaBuffer 從 request flit 繼承 `qos` field。

典型 per-master 部署建議：

| Master 類型 | 建議 mode | 理由 |
|---|---|---|
| CPU | Bypass | CPU 自己有 qos discipline，信任 master |
| DMA engine | Bandwidth-limiter | 高 throughput 但 bursty，限流避免吃光頻寬 |
| 即時 accelerator（real-time SLA）| Urgency-regulator | 需要自適應 priority 才能達 bandwidth target |
| 測試 / debug | Fixed | 固定 priority 方便重現 |

### How — qos 結果選 VC（NUM_VC 數值決定 partition 粒度）

NMU 的 VC Mapping 是 `vc_id = f(R/W, qos)` 純函數。R/W bit 選 subset，subset 內用 qos 值選具體 VC。Partition 粒度依 `NUM_VC` 而定：

| NUM_VC | Request VCs | Response VCs | qos 在 VC 選擇上的角色 |
|---|---|---|---|
| 1（預設） | VC[0] shared | VC[0] shared | 對 VC 選擇無影響，只影響 router QoS arbitration |
| 2 | VC[0] | VC[1] | R/W 分 VC，qos 仍只影響 router |
| 4 | VC[0..1] | VC[2..3] | qos 高/低 → 兩個 request VC（response 同樣） |
| 8 | VC[0..3] | VC[4..7] | qos 4 tier × R/W subset |

預設 `NUM_VC=1` 配置下，qos 對 NMU 本地 VC 無影響，只在 router 端 QoS arbitration 起作用。

### Trade-off — QoS 不會 preempt wormhole-locked W-burst

- HEAD flit 一旦 grant，整個 burst 鎖定 output port 直到 `wlast`。
- 中途到的 higher-QoS packet 等待。
- 仲裁粒度是 per-packet（HEAD flit），不是 per-flit — 保 burst 連續性，犧牲中途 preemption。

**Visual asset（選擇性）：** Limiter / Regulator mode 的 bandwidth-vs-priority 趨勢圖 — 顯示超量時 priority 怎麼掉（Limiter）或不足量時 urgency 怎麼升（Regulator）。

**Speaker notes：** 四種 mode 對應四種 master 行為。Bypass 信任 master 自己給的 `axi_*qos`，適合 CPU 這類 well-behaved master。Fixed 對某個 port 強制單一 priority，常用於測試或低優先 master。Bandwidth-limiter 把吵的 master 限流，避免它餓死同 fabric 的別人。Urgency-regulator 自適應：觀察 response bandwidth，當 master 落後 target 就升級 urgency。典型 SoC 配置 CPU 用 Bypass、DMA 用 Limiter、即時 accelerator 用 Regulator。AMD Versal NoC 用不同模型（per-NPS Differentiated QoS 加 cycle-level scoring），是 router 端、Versal 專屬。Wormhole-lock 互動：W-burst 的 HEAD flit 一旦在某仲裁點獲准，整個 burst 鎖定該 output port 直到 `wlast`。中途到的 higher-QoS packet 必須等 lock 釋放。

---

## Slide 6 — ECC Scheme

**Takeaway**：Flit 從 source NI 到 dest NI 過 N 個 router，每 hop 都有 SEU 機率。三層分工把所有 fabric 故障模式蓋住，error reporting 統一不從 AXI rresp 合成 SLVERR。

### Why — 為什麼分層

- NoC fabric 中每張 flit 過好幾個 router 才會到目的，每一跳都是 noise / glitch / single-event upset 機會。
- 端點才檢 ECC：mis-route 不可偵測。
- 每 hop full ECC：gate count 與 latency 線性放大。
- 解：分層分工，每層 scope 不同。

### What — Three-layer integrity scheme（AMD pg313 §Data Integrity / §Parity 直接適用）

#### Layer 1 — Per-hop routing parity

> *"The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity."* — AMD pg313 §Parity

- 由 source NMU / NSU 在注入 flit 時產生。
- 在每個 router output 與目的 NI sink 都檢查（每跳都驗）。
- Mismatch flit 在偵測點直接 drop，避免誤送到錯的 tile。

#### Layer 2 — End-to-end whole-flit SECDED ECC

> *"SECDED ECC across the entire flit."* — AMD pg313 §Data Integrity
>
> *"No ECC checking is performed in the switch fabric."* — AMD pg313 §Data Integrity

- 由 source NI 產生，只在目的 NI sink 檢查。
- Router 不檢查也不重生。
- Single-bit 沉默修正、double-bit 偵測後 forward 加 log。

#### Layer 3 — AXI host-side parity（選用 sideband，預設啟用）

> *"1 bit per byte for Data."* — AMD pg313 §Parity
>
> *"1 bit per byte for AxAddress."* — AMD pg313 §Parity
>
> *"Parity is checked in the NMU/NSU pipeline when an AXI field is consumed (used by logic). When an AXI field is modified by NMU/NSU logic, parity is regenerated."* — AMD pg313 §Parity
>
> *"Data parity for read responses is generated as 1 bit per byte after the ECC check stage, when the data is converted from NPP to AXI protocol."* — AMD pg313 §Parity

- 在 AXI 邊界檢查，只 log 不 enforce 成 SLVERR。
- AXI fields 被 NMU / NSU logic 改寫時（例如 address remap）parity 重新產生。
- NSU 端 R-response 的 data parity 在 ECC check 之後才產生。

### How — AMD pg313 圖直接對映

AMD pg313 §Data Integrity *End-to-End Protection* 圖（X25510070521）— 檔案 `images/End-to-End Protection.png`。AMD 4 row 對映到我們 3 layer：

| AMD figure row | 對應 |
|---|---|
| Data Parity + Addr Parity | Layer 3 AXI host-side parity（合成一層）|
| ECC | Layer 2 end-to-end whole-flit SECDED |
| DST ID Parity | Layer 1 per-hop routing parity |

Caption 疊在 slide 上：*"Source: AMD pg313 §Data Integrity. AMD Data + Addr Parity = Layer 3, AMD ECC = Layer 2, AMD DST ID Parity = Layer 1."*

### Trade-off — (B)-philosophy：fabric error 統一不從 AXI rresp 回報

> *"Packet domain parity and ECC generation and checking is always enabled."* — AMD pg313 §Data Integrity
>
> *"Correctable ECC errors are corrected on the fly, and the count of correctable errors is incremented. Uncorrectable ECC errors result in a fatal interrupt."* — AMD pg313 §Data Integrity
>
> *"By default, all interrupts are masked."* — AMD pg313 §Data Integrity

Fabric ECC 與 parity error raise interrupt 加累加 counter，但**不會 synth SLVERR** 在 AXI rresp / bresp 上。受損 flit 用 `OKAY` forward 到目的，後續 application-level integrity（HBM endpoint ECC、軟體 CRC）負責 recovery。AXI rresp / bresp 留給 end-to-end memory error 用。**v0.4.0 沒有 fabric-driven SLVERR synthesis** — 把 AMD 對 uncorrectable ECC 的「fatal interrupt」立場乾淨地推廣到所有 fabric event。

**Speaker notes：** 三層 integrity 各有不同 scope。Per-hop routing parity 是最便宜的 check，1-bit XOR 蓋 `{dst_id, last}`。Router 每個 output port 都驗，parity 失敗立刻 drop，避免誤送到錯的 tile。Whole-flit SECDED 是重量級 check，涵蓋整張 flit（header 加 payload，不包 syndrome 自己），source NI 產生、只在目的端檢查。Router 不檢查 SECDED：每跳重做要多一個 cycle、加一個數量級的 gate count，比 parity bit 貴太多。AXI host-side parity 是選用的第三層，per-byte 在 AXI 邊界蓋 data 與 address，驗但不 escalate 成 SLVERR。右邊 AMD 圖畫的是 Versal NoC，scheme 與我們對應，差別在 AMD 把 Data Parity 與 Addr Parity 畫成兩 row，我們合成一個 AXI host-side parity 層。v0.4.0 的 error reporting policy 統一：fabric event 不 synth SLVERR 給 AXI master，所有 fabric-side error 走 CSR + IRQ 路徑。AXI rresp / bresp 留給 end-to-end memory error（HBM / DDR endpoint ECC 經由自然 rresp 傳回 master）。AMD 對 uncorrectable ECC 也是同一立場，我們把它推廣到所有 fabric event。

---

## Slide 7 — Reorder Buffer (RoB) types (NMU expansion)

**Takeaway**：NoC 的 packet 會 OoO 抵達，但 AXI4 要 per-ID 順序。RoB 在 NMU 端重建順序，3 mode 平衡面積與重排能力。

### Why — AXI4 ordering vs NoC OoO

- AXI4 strict ordering rule：same `axi_id` 的 read transaction，response 必須以 issue 順序回到 master。
- Direct crossbar 上 trivially 成立（slave 同條線回，順序不會錯）。
- Packet-switched NoC 不保證：同 master 連續發 R0、R1（同 axi_id），response 可能因為走不同路徑、congestion 不同而 OoO 抵達。對 master 是 protocol violation。

**Purpose (AMD pg313 verbatim):**

> *"Read re-tagging to allow out of order service and prevent interconnect blocking."* — AMD pg313 §NoC Master Unit
>
> *"The read re-order buffer (RROB) holds 64 32-byte entries. An AXI read that is more than 32 bytes consumes multiple entries."* — AMD pg313 §NoC Master Unit *(我們預設 32 entries，FlooNoC-aligned，模式選擇見下表)*

### What — 三種 mode（每個 response channel 獨立配置）

| Mode | 面積成本 | 用途 |
|---|---|---|
| **NoRoB**（預設）| 最低 — 不分配 | NoC 自己保證 same-source-same-dest 的順序 / single-issue master |
| **SimpleRoB** | 小 — 單一 release pointer | naive FIFO，可接受 cross-AXI-ID HoL |
| **NormalRoB** | 最大 — per-AXI-ID linked-list + adaptive bypass | 跨 AXI ID 完整 out-of-order，同 destination fast-path |

- **B / R channel 獨立配置**：`B_ROB_TYPE`、`R_ROB_TYPE` 各自設定。
- 典型多目的部署：R channel 用 NormalRoB、B channel 用 SimpleRoB（B 是 metadata-only，省面積）。

### How — Allocation policy + 運作範例

A5 設計者確認（2026-05-08）：

- Allocation policy：lowest-index-first（多個 FREE entry 時選最低 index）。
- Tie-breaker：兩個 entry 同 cycle 都 ready 時，較低 `rob_idx` 先 release（per-AXI-ID 的 issue order）。

運作範例：Master 連續發 R0、R1、R2（同 AXI ID），response 從 NoC 不一定按順序回（不同路徑、congestion 不同）。RoB 保留 entry 0 / 1 / 2，等 entry 0 收到 response 才 release，即使 entry 2 比 entry 0 早收到 response 也要等。NormalRoB 的 `prev_dest` adaptive bypass 是 fast-path：連續 same-AXI-ID 打同一個目的 NSU 且前一個還沒回時，跳過 per-ID linked-list 的 overhead 直接走 fast-path（同 dest 的 NoC 必保順序）。

### AMD verbatim 對映

> *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."* — AMD pg313 §NoC Master Unit §Read Reorder Buffer

NormalRoB 直接實作這個機制。

**Visual asset：** RoB state machine — `FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE`。

**Speaker notes：** RoB 處理 NoC packet-switched 與 AXI4 ordering 的衝突。NoC 不同路徑、不同 congestion 會讓 same-AXI-ID 的 response OoO 回到 NMU，AXI4 規範 same-AXI-ID response 必須 in-order，差距由 RoB 補。三 mode 在面積與重排能力之間給整合者選擇。NoRoB 最便宜，要求 NoC 自己保證順序，適合 single-issue master 或單純 topology。SimpleRoB 用單一 release pointer，跨 ID 仍按 issue order release，會 cross-ID HoL 但結構簡單。NormalRoB 用 per-AXI-ID linked-list 加 adaptive bypass，面積最大、效能最佳。AMD Versal NoC RROB 預設 64×32-byte（HBM 變體 64×64），我們預設較保守。

---

## Slide 8 — NSU (Network Subordinate Unit) overview

**Takeaway**：NSU 是 slave 側的 protocol boundary — 收 flit、driving local AXI slave，加上 W reassembly、Downsize、Exclusive Monitor 等 slave 特有功能。

### Why — NSU 在 slave 端要做什麼

- NoC 只負責路由 flit 到目的 NI，最後一段必須翻成 AXI 才能 driving local slave。
- NSU 是 slave 端的 protocol boundary，跟 NMU 對稱。

### What — NSU 提供（per AMD pg313 §NoC Slave Unit; adapted to our spec where indicated）

- Conversion of NoC packetized data (NPD) to and from AXI protocol data.
- Asynchronous clock domain crossing and rate-matching between the AXI slave and the NoC.
- AXI exclusive access handling. *(→ Slide 9)*
- Configurable AXI interface widths of 64, 128, 256, or 512 bits. *(adapted: we drop 32-bit)*
- Read response buffering — *"buffered before forwarding to minimize bubbles (stalls) in the read responses."* (AMD verbatim)
- W-burst reassembly before driving the local AXI slave. *(our addition)*
- Data-width down-conversion when AXI slave is wider than NoC payload. *(→ Slide 10, our addition)*
- Response-side integrity — same two-layer ECC scheme on outbound flits (per Slide 6).
- QoS / ordering metadata inherited from inbound request flit (no NSU-side QoS recomputation).

**Core operation (AMD pg313 verbatim):**

> *"The NSU logic de-packetizes the received NoC data packets and converts them into AXI transactions. The re-created AXI transaction passes through the buffered asynchronous data crossing and rate-matching (fast to slow) logic to the AXI master interface."* — AMD pg313 §NoC Slave Unit

### How — Block diagram + dataflow

Block diagram（鏡像 NMU 風格）：

```
                            NSU                          AXI Master I/F
                                                         (Async Boundary)
   ──►  ┌──────────────────────────────────────┐  ────►  AW
        │  ECC Check  →  De-packetizing        │  ────►  W
   ──►  │     ↓            ↓                   │  ────►  AR
        │     W Reassembly Downsize            │  ◄────  B
        │             ↓                        │  ◄────  R
        │  Exclusive Monitor  Read Resp Buffer │
        │             ↓            ↑           │
   ◄──  │  ECC Gen   ←  Packetizing B/R        │
        └──────────────────────────────────────┘
```

- Inbound request：NoC flit → ECC Check → De-packetizing → W Reassembly → Downsize → driving local AXI slave 的 AW / W / AR。
- Outbound response：local slave 回 B / R → Read Response Buffer → Packetizing B/R → ECC Gen → 注入 `noc_rsp_o`。
- Exclusive Monitor 旁觀 AR / AW 的 Exclusive 屬性、配對檢查。
- MetaBuffer（圖中沒明畫）snapshot inbound request flit header（rob_idx、src_id、qos、axi_id），response 直接繼承這些值，省得 NSU 重新計算或推斷。

### AMD verbatim 對映

> *"Conversion of NoC packetized data (NPD) to and from AXI protocol data."* — AMD pg313 §NoC Slave Unit
>
> *"buffered before forwarding to minimize bubbles."* — AMD pg313 §NoC Slave Unit
>
> *"AXI exclusive access handling."* — AMD pg313 §NoC Slave Unit

**Speaker notes：** NSU 是 NoC 端到 AXI slave 端的轉接介面，跟 NMU 對稱。Inbound request 流程：NoC flit 進來 → ECC Check（per-hop parity 已在 router 驗過、whole-flit SECDED 在這裡驗）→ De-packetizing 拆出 AXI 欄位 → W Reassembly 把多 flit 的 W burst 還原 → Downsize 在 slave 比 NoC payload 寬時把 wide flit 拆成 narrow AXI beats → driving local AXI slave 的 AW/W/AR。Exclusive Monitor 旁觀 AR/AW 的 Exclusive 屬性、配對檢查。Outbound response 流程：本地 slave 回 B/R → Read Response Buffer 緩衝 → Packetizing B/R 組 response flit → ECC Gen 附 integrity → 注入 outbound response link。MetaBuffer（圖中沒明畫）是 NSU 內部關鍵 sub-block：snapshot 每個 inbound request flit 的 header（rob_idx、src_id、qos、axi_id），response 產生時直接繼承這些值，省得 NSU 重新計算或推斷。

---

## Slide 9 — Exclusive Monitor (NSU expansion)

**Takeaway**：AXI4 提供 LDREX / STREX 做 lock-free atomic。NSU 端用 reservation table 追蹤 pending Exclusive read，配對 STREX 時做 match check。Single-NI scope。

### Why — Atomic primitive 需要的支援

- AXI4 提供 Exclusive Access (LDREX / STREX) 給 lock-free atomic（compare-and-swap、test-and-set 等）。
- 需要 slave 端追蹤 pending Exclusive read reservation。
- NSU 是 slave 端的 protocol boundary，這個責任落在 NSU。

### What — Per-AXI-ID 的 reservation table

- 每個 reservation entry 存 `(axi_id, awaddr, awsize, awlen)`，在 Exclusive AR 進來時建立。
- 最多 8 個並行 reservation（可設定）。
- Exclusive AW 進來時做 match check：
  - **Match** → 寫入正常完成，`bresp = EXOKAY`。
  - **Mismatch**（不同 ID、不同 addr、或中途有 normal write 蓋過同一 line）→ 寫入退化成 normal write（仍會 commit），但 `bresp = OKAY`。
- 軟體可透過 CSR clear 整個 reservation table — 典型用途：OS 在 process 被 kill 時清掉它持有的 Exclusive。

### How — LDREX / STREX 範例流程

```
1. CPU 發 LDREX (Exclusive AR) addr=0x1000, axi_id=3
2. NSU 在 reservation table 新增 entry (id=3, addr=0x1000, ...)
3. CPU 收 R 資料、計算新值
4. CPU 發 STREX (Exclusive AW) addr=0x1000, axi_id=3
5. NSU match check：id=3 + addr=0x1000 → match → bresp=EXOKAY
   若中間有別人寫 0x1000 → entry invalidated → bresp=OKAY、CPU retry
```

### Trade-off — Single-NI scope

- 跨多個 NI 的 multi-master coherency 不在 v0.4.0 範圍。
- 跨 NI 需要 directory protocol 或 snoop bus，是另一層 design。
- 影響：single-NI 範圍內 LDREX / STREX 行為合規。multi-NI 場景下 coherency 由 software 或 system-level mechanism 保證。

### AMD verbatim 對映

> *"AXI exclusive access handling."* — AMD pg313 §NoC Slave Unit

**Visual asset：** Reservation table state diagram — Exclusive AR → allocate entry → 等 Exclusive AW → match check → match=EXOKAY，途中 overlap normal write → invalidate。

**Speaker notes：** Exclusive Access 是 AXI4 用來實作 lock-free atomic（compare-and-swap、test-and-set 等）的機制。CPU 的 LDREX 對 NSU 來說是一個帶 AxLOCK=Exclusive 的 AR，CPU 算完新值後發 STREX（帶 AxLOCK=Exclusive 的 AW），NSU 比對 reservation 是否還有效，有效就 EXOKAY、CPU 知道 atomic 成功，無效就 OKAY、CPU 要 retry。Reservation 失效的情況：(1) 被別的 master 寫到同一個 cache line，(2) entry 被軟體 clear，(3) reservation table 滿了被新的 LDREX 擠掉。我們的設計刻意維持 single-NI scope — 跨 NI coherency 是另一個層級的問題（directory protocol 或 snoop bus），v0.4.0 沒做。Race semantics：軟體 clear 跟同 cycle 的 NSU 事件衝突時的細節，spec 內有規則細節保證，slide 不深入。AMD Versal 也有等價機制，但細節（reservation depth、race policy）是 Versal-specific。

---

## Slide 10 — Downsize (NSU expansion)

**Takeaway**：當 local AXI slave 比 NoC payload 寬時，NSU 把 wide flit 拆成多個 narrow AXI beat。R 路徑反向累積。

### Why — 寬度不一致的場景

- AXI slave 的 DATA_WIDTH 不一定跟 NoC payload 寬度一致。
- DATA_WIDTH > FLIT_PAYLOAD_WIDTH 時必須拆。
- NSU Downsize 處理這層轉換，跟 NMU Upsize 對稱。

### What — Data-width 降寬機制

- **W path**：一張 wide W flit 拆成 N 個 narrow AXI W beats，driving local slave。Lane mapping 用原始 `awaddr` 加 per-beat offset。
- **R path**：N 個 narrow AXI R beats 從 slave 累積成一張 wide R flit，再注入 response link。
- **No-conversion case**（`DATA_WIDTH == FLIT_PAYLOAD_WIDTH`）：block 退化成 pass-through。
- 每個 port 的 `DATA_WIDTH` 設計階段固定。
- `wstrb` 全程帶過 — 沒被 master 寫到的 lane 帶 `wstrb=0`，slave 只 commit master 真的有寫的 byte。

### How — 寬度範例

NoC payload 256 bits、local AXI slave 64 bits → Downsize 把 1 wide W flit 拆成 4 個 64-bit AXI W beats（lane 0-7、8-15、16-23、24-31 byte），slave 端看到的就是普通 4-beat burst。R path 反過來累積。

```
W path (NoC → AXI slave):
   1 wide flit                     4 narrow AXI W beats
   [256-bit payload + wstrb]   →   beat 0: bytes [7:0],   wstrb [7:0]
                                   beat 1: bytes [15:8],  wstrb [15:8]
                                   beat 2: bytes [23:16], wstrb [23:16]
                                   beat 3: bytes [31:24], wstrb [31:24]

R path (AXI slave → NoC):
   4 narrow AXI R beats        →   1 wide flit (accumulate)
```

**Visual asset：**（上方 ASCII 圖即視覺草稿）

**Speaker notes：** Downsize 跟 NMU 那邊的 Upsize 對稱 — Upsize 是 master 比 NoC 窄時把窄 W beat 累積成一張 wide flit，Downsize 是 slave 比 NoC 寬時把 wide flit 拆成多個 narrow beat。實作關鍵是 lane mapping：原始 `awaddr` 加每 beat 的 byte offset 決定每個 narrow beat 對應 wide flit 的哪幾個 bytes。`wstrb` 全程跟著走 — over-fetch 的 lane 帶 `wstrb=0`，slave 自然忽略，slave 端的 commit 行為跟 master 直接打 narrow burst 一樣。具體 width 範例：256-bit NoC payload 對 64-bit slave，1 wide flit 拆 4 beat，beat 0 對 bytes [7:0]、beat 1 對 [15:8]、依此類推。AMD AXI Conversion 章節有類似概念但細節是 Versal Memory Controller 對應的 bandwidth-matching，跟我們 fabric-only 的範圍不同。

---

## Slide 11 — Credit-Based Flow Control + NPS scope footnote

**Takeaway**：Flit 流量需要 backpressure 機制。Credit-based 是 AMD pg313 既有 NoC 標準做法，verbatim 採用。

### Why — 為什麼需要 flow control

- Send 跟 receive 速率不一定 match — receiver buffer 滿了就要 source 停。
- 傳統 ready / valid handshake 一條 link 上能做，多 hop 會引進 latency 跟 deadlock 風險。
- Credit-based 把 receiver 容量 grant 給 source，乾淨解決。

### What — AMD pg313 §Credit-Based Flow Control 完整描述（直接適用）

> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."*
> — AMD pg313 §Credit-Based Flow Control

### How — 運作意涵 + 啟動序列範例

運作意涵：

- 啟動時走 bi-directional credit-init handshake，雙方 ready 後 credit 才開始流。
- Source 端逐 VC 記 credit 數，receiver 每 cycle 每 VC 最多回 1 credit。
- Source 必須在所選 VC 上有 ≥ 1 credit 才能 assert flit。
- 持續 credit 餓死 = 該 VC 永久 stall（v0.4.0 沒有 timeout 自動升級成 SLVERR，軟體靠 PENDING counter / IRQ 自己處理）。

範例（NUM_VC = 2 的啟動序列）：

```
cycle 0:    reset deassert. NMU credit_count[VC0,VC1] = 0
cycle N:    NMU 與下游 router 互相 assert *_credit_init_ready
cycle N+1:  雙方都 ready，router 開始送 credit 給 NMU
cycle N+5:  NMU credit_count[VC0] = 4（router 連送 4 cycle credit）
cycle N+6:  NMU 有 flit 要送 VC0 → assert noc_*_valid_o，credit_count[VC0]−−
cycle N+7:  NMU credit_count[VC0] = 3，等 receiver 回 credit 才繼續送
```

### NPS（NoC Packet Switch）scope footnote — 相鄰 / NI 範圍外

- NoC fabric router (NPS) 在 NMU 跟 NSU 之間，spec 另立。
- Per-VC 的 cycle-level arbitration 在 router 內做，NI 端只做 flit-construct-time 的 VC mapping。AMD pg313 verbatim（router scope）：

  > *"For every cycle, each output port performs Least Recently Used (LRU) arbitration on all virtual channels of the three input ports."*

- Per-hop routing-parity check 也在 router output 做（已在 Slide 6 ECC 涵蓋）。

**Visual asset：** Sequence diagram — post-reset → init handshake → credit exchange begins → flit injection → credit return（時序圖樣式，類似上方範例）。

**Speaker notes：** Credit-based flow control 是 NoC 整體的 flit 流量契約。Source（NMU 或 NSU）持有的 per-VC credit 計數代表 receiver 端還能容納多少 flit，source 每送一張 flit 就遞減該 VC 的 credit、receiver 每處理完一張就回 1 credit。Bi-directional credit-init handshake 是啟動序列：reset 後雙方都從 0 credit 開始，先互相確認彼此 ready 才開始 credit 交換。實際運作時，這保證 flit 不會 overflow receiver buffer。Persistent credit starvation：receiver 端 hang 或 router 不回 credit 時，source 在那條 VC 永久 stall。v0.4.0 不做自動 timeout escalation，由軟體透過 PENDING counter / IRQ 偵測並從外部處理。NPS scope：router 不在這份 spec 範圍，但 NoC 的整體運作離不開 router，這頁簡單帶過 router 在做什麼（per-VC arbitration、per-hop parity check）讓 audience 有完整 picture。AMD Versal NoC 的 8×8 switch、24-token register、Differentiated QoS scoring 等細節是 Versal-specific，我們不沿用。

---

## Slide 12 — Closing

**Takeaway**：Spec 在 architectural level 鎖定（136 rules、51 testpoints、UVM 1.2）。下一步集中在補功能（atomics、Protocol Library）跟回頭做 safety 機制。

### Status — DV plan 摘要（A5 wave 結算）

| 項目 | 數量 |
|---|---|
| Protocol rules | 136（126 FAIL + 10 RECOMMEND）|
| Testpoints | 51 |
| ABV assertions | 126（每個 FAIL rule 1 個 SVA assert）|
| Coverage covergroups | 17 |
| FPV scope | RoB allocator state machine、ECC SECDED gen+check round-trip、IRQ assertion function、CDC async FIFO、reset entry sequencing |
| Framework | UVM 1.2（A5 designer-confirmed）|

### Next — 未來工作項目

#### 1. AXI4 ATOPs 支援

- 目前 sample-only，ATOP transaction 終止為 SLVERR。
- 正式支援估約 3 週設計加 DV。
- 列入 v0.5.0 candidate。

#### 2. v0.5.0 Plugin-side Protocol Reference Library

- 標準 AXI4 / AXI4-Lite / APB rule 抽到共用 library。
- BFM spec 只描述特定擴充。
- 攤開規則維護成本，跨多個 BFM spec 共享。

#### 3. Debug / safety 機制

- Outstanding-tx Timeout、watchdog、error injection。
- 等主架構穩定後（multi-NI integration 跑過、cross-coverage 收斂）重啟評估。
- 預估 post-v1 發起。

**Q & A**

**Visual asset（選擇性）：** 上方 spec deliverable summary table 整潔放在最下、做 closing artifact。

**Speaker notes：** DV plan 數字是 A5 wave 結算後的最終值。Protocol rule 從 138 減到 136，移除的兩條是 AXI4_MST_TIMEOUT_SLVERR 與 NI_CFG_QUIESCE_LIVENESS（兩條都是 Outstanding-tx Timeout 路徑相關）。51 個 testpoint 涵蓋 NMU、NSU、CSR、CDC、reset、QoS、ECC、Exclusive、credit、quiesce。ABV 是 protocol_rules.md 每個 FAIL rule 對應一個 SVA assert。FPV scope 鎖在可靜態驗證的關鍵正確性：RoB no-deadlock、ECC SECDED 數學、CDC pointer 正確性。Framework 選 UVM 1.2 由設計者確認（產業標準、in-house 已有）。Future work：ATOPs v0.4.0 只 sample 不執行，v0.5.0 plugin 側打算把標準 protocol rule 抽 library 減重，debug / safety 機制（包括 Outstanding-tx Timeout）等系統 design 確定後再回頭評估。
