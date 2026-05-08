# NI 投影片內容（繁中版）

本檔為唯一作業版本。技術名詞、訊號名、暫存器名、規則 ID、AMD pg313 verbatim 引用全部保留英文。

- **聽眾**：內部 review（熟悉 AXI4 + 一般 NoC 概念的工程師；非 Versal 專案背景）。
- **風格**：IP-datasheet 風（AMD pg313 §NoC Master Unit / §NoC Slave Unit 樣板）。每張 slide 是一段完整描述（bullets + 必要時 block diagram 或表格），speaker notes 放 PPT 的 speaker-notes 面板。
- **投影片數**：12 張。
- **drafting 狀態**：A5 wave 2026-05-08，進行中。

**Drafting 進度：**

| Batch | Slides | 狀態 |
|---|---|---|
| 1 | 1-6（Title / Components / NMU overview / Address Map / QoS / ECC） | ✓ drafted（已加深） |
| 2 | 7-12（RoB / NSU overview / Excl Monitor / Downsize / Credit / Closing） | pending |

---

## Slide 1 — Title + Scope

> **AXI4-over-NoC Network Interface**
>
> Design walkthrough — v0.4.0, A5 wave 2026-05-08

**範圍：**

- 每個 tile 一個 NI，single-chimney（NMU + NSU 共用一對 NoC link）。
- 本投影片只涵蓋 NI 範圍；NoC fabric router (NPS) 為另一份 spec。
- 章節順序：AMD pg313 NoC Architecture。

**Speaker notes：** 本 deck 介紹 AXI4-over-NoC Network Interface 這個 block。NI 是 per-tile 的 chimney，把 AXI4 traffic 轉換進 NoC flit、反向亦同 — manager-side ingress (NMU) 與 subordinate-side egress (NSU) 都住在同一個 NI 內。我們依 AMD pg313 章節順序排列，方便熟悉 Versal NoC 的 reviewer 直接 cross-reference；跟 AMD 不同的設計選擇都會明確標出。Router（AMD 用 NPS 一詞）是相鄰的基礎建設，spec 另立。

---

## Slide 2 — NoC Components Overview

**NI 內部組成：**

- **NMU**（Network Manager Unit）— 從 local AXI master 收 request，注入 NoC 變成 request flit。
- **NSU**（Network Subordinate Unit）— 從 NoC 收 request flit，driving local AXI slave。
- **CSR file** — 軟體可見的 runtime control（QoS / Probes / Errors / Quiesce / Exclusive Monitor clear）。
- `irq_o` — 單一 level-sensitive 中斷給 host CPU。

**Per-tile single-chimney 模式：**

| Link | NMU | NSU |
|---|---|---|
| Outbound request | drives | — |
| Inbound request | — | samples |
| Outbound response | — | drives |
| Inbound response | samples | — |

NMU 與 NSU 可獨立 enable。每個 tile 一個 NI。**Tile 內若有多個 IP（CPU + DMA + accelerator），先經上游 AXI crossbar 多工後再進 NI** — per-IP 識別靠 AXI ID，flit header 不另外加欄位，flit 格式更精簡。

**Visual asset：** Top-level block diagram — 一個 tile 內顯示 CPU / DMA / accelerator 在左 → 上游 AXI crossbar → NI block (NMU + NSU + CSR) → NoC fabric。Response path 用虛線回繪；`irq_o` 線拉出去到 CPU。

**Speaker notes：** NI 是 host 端 AXI4 protocol 與 fabric 端 NoC flit protocol 的邊界。NI 內部有兩個功能上獨立的半邊 — NMU 與 NSU — 共用實體 NoC link 線（single-chimney）。兩半邊在 NI 內部不會直接溝通 — 唯一的耦合點是共用的 link。這個設計對齊 FlooNoC `floo_axi_chimney.sv` topology 與 AMD pg313 的 NMU + NSU 拆分。Tile 內多 IP 時，由上游 AXI crossbar 仲裁；進入 NoC 之後，這些 IP 用 AXI ID 區分，沒有額外加 per-IP flit header 欄位，這讓 flit 格式保持精簡。CSR file 是軟體看 NI 的窗口：QoS configuration、performance probes、error status（v0.4.0 共 3 個 RW1C event-class bits）、NMU 的 quiesce control、NSU 的 Exclusive Monitor clear trigger。`irq_o` 是單一 level-sensitive 線，當任何 unmasked error-status bit set 就 assert。

---

## Slide 3 — NMU (Network Manager Unit) overview

**NMU 提供：**

- AXI master 與 NoC 之間的 asynchronous clock domain crossing 與 rate matching。
- AXI4 protocol ↔ NoC flit format 雙向轉換。
- Address matching 與 route control — 三種 routing mode（→ Slide 4）。
- WRAP / INCR / FIXED burst 支援。
- Read response 重排序，由 Reorder Buffer (RoB) 處理 — 三種 mode 平衡面積與重排序能力（→ Slide 7）。
- Write order 強制 — W-burst 在 egress link 上保持連續。
- Ingress QoS control — 四種 mode（→ Slide 5）。
- 兩層 ECC integrity — per-hop routing parity + end-to-end whole-flit SECDED（→ Slide 6）。
- AXI4 Exclusive Access 支援（轉送到 NSU Exclusive Monitor）。
- 可配置 AXI 資料寬度：64、128、256、或 512 bits。
- 最多 32 個 outstanding AXI reads + 32 個 outstanding AXI writes。

**Block diagram（右側，ref.jpg 風 — 簡單方框）：**

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

**AMD verbatim（inline）：** *"Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*（AMD pg313 §NoC Master Unit）

**Speaker notes：** 走一遍資料流。AW/AR 從左邊進來 → Address Map 解出目的 tile 的 ID → Packetizing 組 flit payload → QoS Order Control 配上 per-flit qos 欄位 → ECC Gen 附上 per-hop routing parity bit 與 whole-flit SECDED syndrome → VC Mapping 選 egress virtual channel → flit 注入到 outbound request link。每個 sub-block 都是 registered stage；request-to-NoC 端到端 critical path 短（幾個 cycle），加上 async FIFO 過 NoC clock domain。Response path 反過來：inbound response flit → ECC Check（single-bit 沉默修正、double-bit 紀錄）→ Read Re-Ordering 強制 per-AXI-ID response 順序 → De-packetizing 還原 B/R 給 AXI master。我們在 outstanding count 上比較保守（32 vs AMD 的 64），且不 chop bursts — 一張 wide flit 把一個 AXI message 端到端帶完。我們的 wide flit 格式讓 AMD 的 256-byte chopping 沒必要。

---

## Slide 4 — Address Map mechanism

**三種 routing mode** — 設計階段選定：

| Mode | 機制 | 適用情境 |
|---|---|---|
| **XY-routed**（預設） | 從 AXI `awaddr` / `araddr` bit 抽取 — 可配置 bit 欄位解碼成 (X, Y) mesh 座標 | 規則 2D mesh 部署 |
| **Source-routed** | 預先計算的 flit-header route path | 靜態 / 設計階段路徑最佳化 |
| **ID-table**（SAM） | System Address Map lookup — 把 address range 對到目的 tile ID | 多區段 address space；軟體定義 region 對應 |

**預設配置：**

- `X_WIDTH = 4 bits`、`Y_WIDTH = 4 bits` → 8-bit 目的 tile ID — 支援最大 16 × 16 mesh = 256 個 tiles。
- Address bit 抽取的 offset 每軸都可在設計階段配置。

**Bit-field 範例（XY-routed mode、預設 offsets）：**

```
   awaddr [63:0]
            │
            ├─ bits [39:36] → Y 座標（4 bits）
            ├─ bits [35:32] → X 座標（4 bits）
            └─ bits [31:0]  → local address（透傳到目的 NSU）
```

**另：**

- SAM table 是設計階段參數 — v0.4.0 不支援 runtime 修改（要改就重 elaborate）。
- Address translation 與 packetizing 平行跑 — 不額外多 pipeline stage。

**Visual asset：** `awaddr` bit-field 分解圖（上面那張 ASCII tree，PPT 內重畫成乾淨方塊）。

**Speaker notes：** 三種 routing mode 給整合者依不同 topology 彈性。XY-routed 是規則 mesh 的預設 — 快、簡單、deterministic。Source-routed 適合在 flit 構建時就知道 route 的 pre-computed 路徑。ID-table 透過設計階段 SAM lookup table 把 AXI address 區段對到目的 tile ID — 適用於 address space 被切成不同 tile 各自區段的情境。預設配置下，4-bit X 加 4-bit Y 支援最大 16 × 16 mesh、256 個目的 tile。Address bit offset 是每軸可配置的設計階段參數 — 整合者依自己 address space 的 layout 決定座標放在 `awaddr` 哪幾位元。`local address` 部分會原封不動透傳到目的 NSU，由 NSU 拿來 driving local AXI slave。我們刻意選了統一的 3-mode selector，而不是 AMD Versal 的 Master-Specified ID + Re-mapping 雙機制（那綁 PL-interconnect 耦合）。

---

## Slide 5 — QoS Generator

**四種 mode** — 透過 CSR runtime 切換：

| Mode | 行為 |
|---|---|
| Bypass | 直接 pass through AXI awqos / arqos。 |
| Fixed | 每張 flit 都用 CSR 設定的固定值覆蓋。 |
| Bandwidth-limiter | 流量超過設定 bandwidth 時降 priority。 |
| Urgency-regulator | 對 bandwidth target 做 feedback control 升級 urgency。 |

**典型 per-master 部署建議：**

| Master 類型 | 建議 mode | 理由 |
|---|---|---|
| CPU | Bypass | CPU 自己有 qos discipline；信任 master |
| DMA engine | Bandwidth-limiter | 高 throughput 但 bursty；限流避免吃光頻寬 |
| 即時 accelerator（real-time SLA） | Urgency-regulator | 需要自適應 priority 才能達 bandwidth target |
| 測試 / debug | Fixed | 固定 priority 方便重現 |

**另：**

- 只有 NMU 有；NSU 透過 response-side metadata 繼承 per-flit qos（response path 不重新計算 QoS）。
- QoS 不會 preempt wormhole-locked W-bursts — 仲裁粒度是 per-packet（HEAD flit），不是 per-flit。

**Visual asset（選擇性）：** Limiter / Regulator mode 的 bandwidth-vs-priority 趨勢圖 — 顯示超量時 priority 怎麼掉（Limiter）或不足量時 urgency 怎麼升（Regulator）。

**Speaker notes：** 四種 QoS mode 涵蓋從純 passthrough（Bypass — 信任 AXI master 自己的 qos）到主動 shaping（Regulator — feedback loop）的光譜。典型 SoC 配置：CPU master 用 Bypass、DMA 用 Limiter、即時 accelerator 用 Regulator — 各 master 依自己的行為 profile 選 mode。Bypass 適合 AXI master 已經會給 well-behaved qos 的情境。Fixed 讓整合者能對某個 port 強制單一 priority — 適合測試或單純的低優先 master。Bandwidth-limiter 把吵的 master 限流，避免它餓死同 fabric 的別人。Urgency-regulator 最自適應 — 觀察實際 response bandwidth，當 master 落後 target 時升級 urgency（拉高 effective qos）。AMD Versal NoC 用不同模型 — per-NPS Differentiated QoS 加 cycle-level scoring — 是 router 端、Versal 專屬的。Wormhole-lock 互動：W-burst 的 HEAD flit 一旦在某仲裁點獲准，整個 burst 就鎖定該 output port 直到 `wlast`。中途到的 higher-QoS packet 不能搶 — 必須等 lock 釋放。

---

## Slide 6 — ECC Scheme

**三層 integrity：**

1. **Per-hop routing parity** — 保護 routing 關鍵欄位（destination ID + last-flit indicator），每個 router 與目的 NI sink 都檢查。Mismatch flit 在偵測點直接 drop。
2. **End-to-end whole-flit SECDED ECC** — 涵蓋整張 flit。Source NI 產生，只在目的 NI sink 檢查。Router 不檢查也不重生。
3. **AXI host-side parity**（選用 sideband，預設啟用）— 在 AXI 邊界檢查；只 log 不 enforce 成 SLVERR。

**每層 gen / check 點摘要：**

| Layer | 產生點 | 檢查點 |
|---|---|---|
| Per-hop routing parity（dst_id + last-flit indicator） | NMU / NSU 注入 flit 時 | 每個 router output **+** 目的 NI sink（每跳都檢查） |
| End-to-end whole-flit SECDED | NMU / NSU 注入 flit 時 | 只在目的 NI sink（router 不檢查） |
| AXI host-side parity（data + address，per-byte） | AXI master、NSU output | NMU input、AXI slave input、NMU output |

**AMD verbatim（直接放在 slide 上）：**

> *"SECDED ECC across the entire flit."* — AMD pg313 §Data Integrity
>
> *"No ECC checking is performed in the switch fabric."* — AMD pg313 §Data Integrity
>
> *"1 bit per byte for Data."* / *"1 bit per byte for AxAddress."* — AMD pg313 §Parity
>
> *"The NPP packet (DST ID + LAST) field is also protected by 1-bit even parity."* — AMD pg313 §Parity
>
> *"Uncorrectable ECC errors result in a fatal interrupt."* — AMD pg313 §Data Integrity
>
> *"By default, all interrupts are masked."* — AMD pg313 §Data Integrity

**Error reporting policy：**

Fabric ECC 與 parity error 會 raise interrupt 加累加 counter，但**不會 synth SLVERR** 在 AXI rresp / bresp 上。受損 flit 用 `OKAY` 直接 forward 到目的；後續 / application-level integrity（HBM endpoint ECC、軟體 CRC）負責 recovery。AXI rresp / bresp 留給 end-to-end memory error 用。**v0.4.0 沒有 fabric-driven SLVERR synthesis。**

**Visual asset：** AMD pg313 §Data Integrity *End-to-End Protection* 圖（X25510070521）— 檔案 `images/End-to-End Protection.png`。圖中 4 row 對映到我們 3 層如下：

- AMD *Data Parity* + AMD *Addr Parity* → 我們的 **AXI host-side parity**（一層涵蓋兩者）。
- AMD *ECC* → 我們的 **end-to-end whole-flit SECDED**。
- AMD *DST ID Parity* → 我們的 **per-hop routing parity**（`route_par` 涵蓋 DST ID + last-flit indicator）。

Caption 疊在 slide 上：*「Source: AMD pg313 §Data Integrity. 對映到我們的 scheme：AMD Data + Addr Parity = 我們的 AXI parity；AMD ECC = 我們的 whole-flit SECDED；AMD DST ID Parity = 我們的 per-hop routing parity (dst_id + last)」*

**Speaker notes：** 三層 integrity，每一層 scope 不同。Per-hop routing parity 是最便宜的 check — 對 destination-ID 與 last-flit-indicator 欄位做 1-bit XOR。Router 在每個 output port 都驗；parity 失敗就立刻 drop，避免誤送到錯的 tile。Whole-flit SECDED 是重量級 check — 涵蓋整張 flit（header 加 payload，不包 syndrome 自己）；source NI 產生、只在目的端檢查。Router 不檢查的原因是這樣會多大概一個 cycle / hop 加一個數量級的 gate count，比 parity bit 貴太多。AXI host-side parity 是選用的第三層在 AXI 邊界，per-byte 涵蓋 data 跟 address — 驗但不 escalate 成 SLVERR。右邊那張 AMD 圖剛好就是這個 pattern，畫的是 Versal NoC；我們的 scheme 把每一 row 都實作了，差別只在 AMD 把 Data Parity 跟 Addr Parity 畫成兩 row，我們合成一個 AXI host-side parity 層。v0.4.0 的 error reporting policy 是統一的：fabric event 一律不 synth SLVERR 給 AXI master。所有 fabric-side error 走 CSR + IRQ 路徑。AXI rresp / bresp 留給 end-to-end memory error（HBM / DDR endpoint ECC 經由自然 rresp 傳回 master）。這跟 AMD 對 uncorrectable ECC 的立場一致 — 並且我們把這個立場乾淨地推廣到所有 fabric event。
