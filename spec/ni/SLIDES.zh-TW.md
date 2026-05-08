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
| 2 | 7-12（RoB / NSU overview / Excl Monitor / Downsize / Credit / Closing） | ✓ drafted |

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

---

## Slide 7 — Reorder Buffer (RoB) types (NMU expansion)

**三種 mode** — 每個 response channel 獨立配置：

| Mode | 面積成本 | 用途 |
|---|---|---|
| **NoRoB**（預設） | 最低 — 不分配 | NoC 自己保證 same-source-same-dest 的順序；single-issue master |
| **SimpleRoB** | 小 — 單一 release pointer | naive FIFO；可接受 cross-AXI-ID HoL |
| **NormalRoB** | 最大 — per-AXI-ID linked-list + adaptive bypass | 跨 AXI ID 完整 out-of-order；同 destination fast-path |

**B / R channel 獨立配置：** `B_ROB_TYPE`、`R_ROB_TYPE` 各自設定。典型多目的部署：R channel 用 NormalRoB、B channel 用 SimpleRoB（B 是 metadata-only，省面積）。

**A5 設計者確認（2026-05-08）：**

- Allocation policy：lowest-index-first（多個 FREE entry 時選最低 index）。
- Tie-breaker：兩個 entry 同 cycle 都 ready 時，較低 `rob_idx` 先 release（per-AXI-ID 的 issue order）。

**運作範例：** Master 連續發 R0、R1、R2（同 AXI ID），response 從 NoC 不一定按順序回（不同路徑、congestion 不同）。RoB 保留 entry 0/1/2，等 entry 0 收到 response 才 release，即使 entry 2 比 entry 0 早收到 response 也要等。NormalRoB 的 `prev_dest` adaptive bypass 是 fast-path：連續 same-AXI-ID 打同一個目的 NSU 且前一個還沒回時，跳過 per-ID linked-list 的 overhead 直接走 fast-path（同 dest 的 NoC 必保順序）。

**AMD verbatim：**

> *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."* — AMD pg313 §NoC Master Unit §Read Reorder Buffer

**Visual asset：** RoB state machine — `FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE`。

**Speaker notes：** RoB 的存在是因為 NoC 是 packet-switched — 不同 destination 的 response 可能因為 NoC 路徑不同、congestion 不同而 out-of-order 回到 NMU。但 AXI4 強制 same-AXI-ID 的 response 必須 in-order — 所以需要 RoB 在 NMU 端把 out-of-order 的 NoC response 重排成 in-order 的 AXI response。三 mode 在面積跟重排能力之間給整合者選擇：NoRoB 最便宜但要求 NoC 自己保證順序（適合 single-issue master 或單純 NoC topology）；SimpleRoB 用單一 release pointer，跨 ID 也照 issue order release，會發生 cross-ID HoL 但結構簡單；NormalRoB 用 per-AXI-ID linked-list 加 adaptive bypass，最大面積但效能最佳。AMD Versal NoC RROB 預設 64×32-byte（HBM 變體 64×64）；我們預設較保守。

---

## Slide 8 — NSU (Network Subordinate Unit) overview

**NSU 提供：**

- NoC 與 AXI slave 之間的 asynchronous clock domain crossing 與 rate matching。
- NoC flit format ↔ AXI4 protocol 雙向轉換。
- W-burst reassembly — driving local AXI slave 前先還原 burst。
- Data-width down-conversion — AXI slave 比 NoC payload 寬時用（→ Slide 10）。
- AXI4 Exclusive Access — per-AXI-ID monitor、軟體可清的 reservation table（→ Slide 9）。
- Read response buffering — 解耦 slave 端的 timing 與 NoC 注入的 back-pressure。
- Response-side integrity — outbound response flit 套用同樣的 two-layer ECC（per Slide 6）。
- QoS / ordering metadata 從 inbound request flit 繼承（NSU 不重新計算 QoS）。

**Block diagram（鏡像 NMU 風格）：**

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

**AMD verbatim：**

> *"Conversion of NoC packetized data (NPD) to and from AXI protocol data."* — AMD pg313 §NoC Slave Unit
>
> *"buffered before forwarding to minimize bubbles."* — AMD pg313 §NoC Slave Unit
>
> *"AXI exclusive access handling."* — AMD pg313 §NoC Slave Unit

**Speaker notes：** NSU 是 NoC 端到 AXI slave 端的轉接介面，跟 NMU 對稱。Inbound request 流程：NoC flit 進來 → ECC Check（per-hop parity 已在 router 驗過、whole-flit SECDED 在這裡驗）→ De-packetizing 拆出 AXI 欄位 → W Reassembly 把多 flit 的 W burst 還原 → Downsize 在 slave 比 NoC payload 寬時把 wide flit 拆成 narrow AXI beats → driving local AXI slave 的 AW/W/AR。Exclusive Monitor 旁觀 AR/AW 的 Exclusive 屬性、配對檢查。Outbound response 流程：本地 slave 回 B/R → Read Response Buffer 緩衝 → Packetizing B/R 組 response flit → ECC Gen 附 integrity → 注入 outbound response link。MetaBuffer（圖中沒明畫）是 NSU 內部關鍵 sub-block：snapshot 每個 inbound request flit 的 header（rob_idx、src_id、qos、axi_id），response 產生時直接繼承這些值，省得 NSU 重新計算或推斷。

---

## Slide 9 — Exclusive Monitor (NSU expansion)

**AXI4 Exclusive Access 支援 — per-AXI-ID 的 reservation table：**

- 每個 reservation entry 存 `(axi_id, awaddr, awsize, awlen)`，在 Exclusive AR 進來時建立。
- 最多 8 個並行 reservation（可設定）。
- Exclusive AW 進來時做 match check：
  - **Match** → 寫入正常完成，`bresp = EXOKAY`。
  - **Mismatch**（不同 ID、不同 addr、或中途有 normal write 蓋過同一 line）→ 寫入退化成 normal write（仍會 commit），但 `bresp = OKAY`。
- 軟體可透過 CSR clear 整個 reservation table — 典型用途：OS 在 process 被 kill 時清掉它持有的 Exclusive。
- **Single-NI scope** — 跨多個 NI 的 multi-master coherency 不在 v0.4.0 範圍（需要 directory 或 snoop protocol）。

**LDREX/STREX 範例流程：**

```
1. CPU 發 LDREX (Exclusive AR) addr=0x1000, axi_id=3
2. NSU 在 reservation table 新增 entry (id=3, addr=0x1000, ...)
3. CPU 收 R 資料、計算新值
4. CPU 發 STREX (Exclusive AW) addr=0x1000, axi_id=3
5. NSU match check：id=3 + addr=0x1000 → match → bresp=EXOKAY
   若中間有別人寫 0x1000 → entry invalidated → bresp=OKAY、CPU retry
```

**AMD verbatim：**

> *"AXI exclusive access handling."* — AMD pg313 §NoC Slave Unit

**Visual asset：** Reservation table state diagram — Exclusive AR → allocate entry → 等 Exclusive AW → match check → match=EXOKAY；途中 overlap normal write → invalidate。

**Speaker notes：** Exclusive Access 是 AXI4 用來實作 lock-free atomic（compare-and-swap、test-and-set 等）的機制。CPU 的 LDREX 對 NSU 來說是一個帶 AxLOCK=Exclusive 的 AR；CPU 算完新值後發 STREX（帶 AxLOCK=Exclusive 的 AW）；NSU 比對 reservation 是否還有效，有效就 EXOKAY、CPU 知道 atomic 成功，無效就 OKAY、CPU 要 retry。Reservation 失效的情況：(1) 被別的 master 寫到同一個 cache line；(2) entry 被軟體 clear；(3) reservation table 滿了被新的 LDREX 擠掉。我們的設計刻意維持 single-NI scope — 跨 NI coherency 是另一個層級的問題（directory protocol 或 snoop bus），v0.4.0 沒做。Race semantics：軟體 clear 跟同 cycle 的 NSU 事件衝突時的細節，spec 內有規則細節保證；slide 不深入。AMD Versal 也有等價機制，但細節（reservation depth、race policy）是 Versal-specific。

---

## Slide 10 — Downsize (NSU expansion)

**Data-width 降寬機制 — local AXI slave 比 NoC payload 寬時觸發。**

- **W path:** 一張 wide W flit 拆成 N 個 narrow AXI W beats，driving local slave。Lane mapping 用原始 `awaddr` 加 per-beat offset。
- **R path:** N 個 narrow AXI R beats 從 slave 累積成一張 wide R flit，再注入 response link。
- **No-conversion case**（`DATA_WIDTH == FLIT_PAYLOAD_WIDTH`）：block 退化成 pass-through。
- 每個 port 的 `DATA_WIDTH` 設計階段固定。
- `wstrb` 全程帶過 — 沒被 master 寫到的 lane 帶 `wstrb=0`，slave 只 commit master 真的有寫的 byte。

**寬度範例：** NoC payload 256 bits、local AXI slave 64 bits → Downsize 把 1 wide W flit 拆成 4 個 64-bit AXI W beats（lane 0-7、8-15、16-23、24-31 byte），slave 端看到的就是普通 4-beat burst。R path 反過來累積。

**Visual asset：**

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

**Speaker notes：** Downsize 跟 NMU 那邊的 Upsize 對稱 — Upsize 是 master 比 NoC 窄時把窄 W beat 累積成一張 wide flit，Downsize 是 slave 比 NoC 寬時把 wide flit 拆成多個 narrow beat。實作關鍵是 lane mapping：原始 `awaddr` 加每 beat 的 byte offset 決定每個 narrow beat 對應 wide flit 的哪幾個 bytes。`wstrb` 全程跟著走 — over-fetch 的 lane 帶 `wstrb=0`，slave 自然忽略；slave 端的 commit 行為跟 master 直接打 narrow burst 一樣。具體 width 範例：256-bit NoC payload 對 64-bit slave，1 wide flit 拆 4 beat，beat 0 對 bytes [7:0]、beat 1 對 [15:8]、依此類推。AMD AXI Conversion 章節有類似概念但細節是 Versal Memory Controller 對應的 bandwidth-matching，跟我們 fabric-only 的範圍不同。

---

## Slide 11 — Credit-Based Flow Control + NPS scope footnote

**機制（AMD pg313 verbatim — 直接適用我們的設計）：**

> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."*
> — AMD pg313 §Credit-Based Flow Control

**運作意涵：**

- 啟動時走 bi-directional credit-init handshake，雙方 ready 後 credit 才開始流。
- Source 端逐 VC 記 credit 數；receiver 每 cycle 每 VC 最多回 1 credit。
- Source 必須在所選 VC 上有 ≥ 1 credit 才能 assert flit。
- 持續 credit 餓死 = 該 VC 永久 stall（v0.4.0 沒有 timeout 自動升級成 SLVERR；軟體靠 PENDING counter / IRQ 自己處理）。

**範例（NUM_VC = 2 的啟動序列）：**

```
cycle 0:    reset deassert. NMU credit_count[VC0,VC1] = 0
cycle N:    NMU 與下游 router 互相 assert *_credit_init_ready
cycle N+1:  雙方都 ready；router 開始送 credit 給 NMU
cycle N+5:  NMU credit_count[VC0] = 4（router 連送 4 cycle credit）
cycle N+6:  NMU 有 flit 要送 VC0 → assert noc_*_valid_o，credit_count[VC0]−−
cycle N+7:  NMU credit_count[VC0] = 3，等 receiver 回 credit 才繼續送
```

**NPS（NoC Packet Switch）scope footnote — 相鄰 / NI 範圍外：**

- NoC fabric router (NPS) 在 NMU 跟 NSU 之間，spec 另立。
- Per-VC 的 cycle-level arbitration 在 router 內做；NI 端只做 flit-construct-time 的 VC mapping。AMD pg313 verbatim（router scope）：

  > *"For every cycle, each output port performs Least Recently Used (LRU) arbitration on all virtual channels of the three input ports."*

- Per-hop routing-parity check 也在 router output 做（已在 Slide 6 ECC 涵蓋）。

**Visual asset：** Sequence diagram — post-reset → init handshake → credit exchange begins → flit injection → credit return（時序圖樣式，類似上方範例）。

**Speaker notes：** Credit-based flow control 是 NoC 整體的 flit 流量契約。Source（NMU 或 NSU）持有的 per-VC credit 計數代表 receiver 端還能容納多少 flit；source 每送一張 flit 就遞減該 VC 的 credit、receiver 每處理完一張就回 1 credit。Bi-directional credit-init handshake 是啟動序列：reset 後雙方都從 0 credit 開始，先互相確認彼此 ready 才開始 credit 交換。實際運作時，這保證 flit 不會 overflow receiver buffer。Persistent credit starvation：如果 receiver 端 hang 或 router 不回 credit，source 就在那條 VC 永久 stall — v0.4.0 不做自動 timeout escalation（曾考慮過 Outstanding-tx Timeout，後來決定不在初版做安全機制），由軟體透過 PENDING counter / IRQ 偵測並從外部處理。NPS scope：router 不在這份 spec 範圍，但 NoC 的整體運作離不開 router，這頁簡單帶過 router 在做什麼（per-VC arbitration、per-hop parity check）讓 audience 有完整 picture。AMD Versal NoC 的 8×8 switch、24-token register、Differentiated QoS scoring 等細節是 Versal-specific，我們不沿用。

---

## Slide 12 — Closing

**DV plan 摘要（A5 wave 結算）：**

| 項目 | 數量 |
|---|---|
| Protocol rules | 136（126 FAIL + 10 RECOMMEND） |
| Testpoints | 51 |
| ABV assertions | 126（每個 FAIL rule 1 個 SVA assert） |
| Coverage covergroups | 17 |
| FPV scope | RoB allocator state machine、ECC SECDED gen+check round-trip、IRQ assertion function、CDC async FIFO、reset entry sequencing |
| Framework | UVM 1.2（A5 designer-confirmed） |

**未來工作：**

- AXI4 ATOPs（atomic operations）支援 — v0.4.0 sample-only 終止為 SLVERR；正式支援 deferred 至 future revision（約 3 週設計 + DV）。
- v0.5.0 Plugin-side Protocol Reference Library — 把標準 AXI4 / AXI4-Lite / APB rule 抽成共用 library entry，BFM spec 只需描述特定擴充。
- Debug / safety mechanisms — Outstanding-tx Timeout、watchdog、error injection 等在 v1 後重新評估。

**Q & A**

**Visual asset（選擇性）：** 上方 spec deliverable summary table 整潔放在最下、做 closing artifact。

**Speaker notes：** 這頁是 deck 的 closing。DV plan 數字是 A5 wave 結算後的最終值：post-A5 的 protocol_rules.md 從 138 減到 136（移除 Outstanding-tx Timeout 路徑相關的 AXI4_MST_TIMEOUT_SLVERR 跟 NI_CFG_QUIESCE_LIVENESS）。51 個 testpoint 涵蓋 NMU、NSU、CSR、CDC、reset、QoS、ECC、Exclusive、credit、quiesce 等所有 NI 功能。ABV 是 protocol_rules.md 每個 FAIL severity rule 對應一個 SVA assert property。FPV scope 鎖在那些可以靜態驗證的關鍵正確性 — RoB no-deadlock、ECC SECDED 數學、CDC pointer 正確性。Framework 選 UVM 1.2 是設計者確認後決定（產業標準、in-house 已有）。Future work 把這次刻意簡化的部分列出來：ATOPs 我們 v0.4.0 只 sample 不執行；v0.5.0 plugin 側打算把標準 protocol rule 抽 library 減重；debug/safety 機制（包括我們這次刪掉的 Outstanding-tx Timeout）等系統 design 確定後再回頭評估。
