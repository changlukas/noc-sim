# NI 投影片內容（繁中版）

本檔為 `SLIDES.md` 的繁體中文對映版本，與英文版同步維護（參照 plugin README 雙語慣例）。技術名詞、訊號名、暫存器名、規則 ID、AMD pg313 verbatim 引用全部保留英文。

- **聽眾**：內部 review（熟悉 AXI4 + 一般 NoC 概念的工程師；非 Versal 專案背景）。
- **風格**：IP-datasheet 風（AMD pg313 §NoC Master Unit / §NoC Slave Unit 樣板）。每張 slide 是一段完整描述（bullets + 必要時 block diagram 或表格），speaker notes 放 PPT 的 speaker-notes 面板。
- **投影片數**：12 張。
- **drafting 狀態**：A5 wave 2026-05-08，進行中。

**Drafting 進度：**

| Batch | Slides | 狀態 |
|---|---|---|
| 1 | 1-6（Title / Components / NMU overview / Address Map / QoS / ECC） | ✓ drafted |
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

NMU 與 NSU 可獨立 enable。每個 tile 一個 NI。Tile 內若有多個 IP（CPU + DMA + accelerator），先經上游 AXI crossbar 多工後再進 NI。

**Visual asset：** Top-level block diagram — AXI master ↔ NMU ↔ router ↔ NSU ↔ AXI slave，附 response path 對稱；CSR + `irq_o` 在側邊。

**Speaker notes：** NI 是 host 端 AXI4 protocol 與 fabric 端 NoC flit protocol 的邊界。NI 內部有兩個功能上獨立的半邊 — NMU 與 NSU — 共用實體 NoC link 線（single-chimney）。兩半邊在 NI 內部不會直接溝通 — 唯一的耦合點是共用的 link。這個設計對齊 FlooNoC `floo_axi_chimney.sv` topology 與 AMD pg313 的 NMU + NSU 拆分。CSR file 是軟體看 NI 的窗口：QoS configuration、performance probes、error status（v0.4.0 共 3 個 RW1C event-class bits）、NMU 的 quiesce control、NSU 的 Exclusive Monitor clear trigger。`irq_o` 是單一 level-sensitive 線，當任何 unmasked error-status bit set 就 assert。

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

**Speaker notes：** 走一遍資料流。AW/AR 從左邊進來 → Address Map 解出目的 tile ID → Packetizing 組 flit payload → QoS Order Control 配上 per-flit qos 欄位 → ECC Gen 附上 per-hop routing parity bit 與 whole-flit SECDED syndrome → VC Mapping 選 egress virtual channel → flit 注入到 outbound request link。Response path 反過來：inbound response flit → ECC Check（single-bit 沉默修正、double-bit 紀錄）→ Read Re-Ordering 強制 per-AXI-ID response 順序 → De-packetizing 還原 B/R 給 AXI master。我們在 outstanding count 上比較保守（32 vs AMD 的 64），且不 chop bursts — 一張 wide flit 把一個 AXI message 端到端帶完。我們的 wide flit 格式讓 AMD 的 256-byte chopping 沒必要。

---

## Slide 4 — Address Map mechanism

**三種 routing mode** — 設計階段選定：

| Mode | 機制 | 適用情境 |
|---|---|---|
| **XY-routed**（預設） | 從 AXI `awaddr` / `araddr` bit 抽取 — 可配置 bit 欄位解碼成 (X, Y) mesh 座標 | 規則 2D mesh 部署 |
| **Source-routed** | 預先計算的 flit-header route path | 靜態 / 設計階段路徑最佳化 |
| **ID-table**（SAM） | System Address Map lookup — 把 address range 對到目的 tile ID | 多區段 address space；軟體定義 region 對應 |

**另：**

- 目的 tile ID = (X, Y) mesh 座標。
- SAM table 是設計階段參數 — v0.4.0 不支援 runtime 修改（要改就重 elaborate）。
- Address bit 抽取的 offset 每軸都可在設計階段配置。
- Address translation 與 packetizing 平行跑 — 不額外多 pipeline stage。

**Visual asset：** XY-routed mode 下 `awaddr` 的 bit 欄位分解圖 — 拆成（X 座標 bits | Y 座標 bits | local address bits）。

**Speaker notes：** 三種 routing mode 給整合者依不同 topology 彈性。XY-routed 是規則 mesh 的預設 — 快、簡單、deterministic。Source-routed 適合在 flit 構建時就知道 route 的 pre-computed 路徑。ID-table 透過設計階段 SAM lookup table 把 AXI address 區段對到目的 tile ID — 適用於 address space 被切成不同 tile 各自區段的情境。我們刻意選了統一的 3-mode selector，而不是 AMD Versal 的 Master-Specified ID + Re-mapping 雙機制（那綁 PL-interconnect 耦合）。

---

## Slide 5 — QoS Generator

**四種 mode** — 透過 CSR runtime 切換：

| Mode | 行為 |
|---|---|
| Bypass | 直接 pass through AXI awqos / arqos。 |
| Fixed | 每張 flit 都用 CSR 設定的固定值覆蓋。 |
| Bandwidth-limiter | 流量超過設定 bandwidth 時降 priority。 |
| Urgency-regulator | 對 bandwidth target 做 feedback control 升級 urgency。 |

**另：**

- 只有 NMU 有；NSU 透過 response-side metadata 繼承 per-flit qos（response path 不重新計算 QoS）。
- QoS 不會 preempt wormhole-locked W-bursts — 仲裁粒度是 per-packet（HEAD flit），不是 per-flit。

**Visual asset（選擇性）：** Limiter / Regulator mode 的 bandwidth-vs-priority 圖。

**Speaker notes：** 四種 QoS mode 涵蓋從純 passthrough（Bypass — 信任 AXI master 自己的 qos）到主動 shaping（Regulator — feedback loop）的光譜。Bypass 適合 AXI master 已經會給 well-behaved qos 的情境。Fixed 讓整合者能對某個 port 強制單一 priority。Bandwidth-limiter 把吵的 master 限流，避免它餓死同 fabric 的別人。Urgency-regulator 最自適應 — 觀察實際 response bandwidth，當 master 落後 target 時升級 urgency（拉高 effective qos）。AMD Versal NoC 用不同模型 — per-NPS Differentiated QoS 加 cycle-level scoring — 是 router 端、Versal 專屬的。Wormhole-lock 互動：W-burst 的 HEAD flit 一旦在某仲裁點獲准，整個 burst 就鎖定該 output port 直到 `wlast`。中途到的 higher-QoS packet 不能搶 — 必須等 lock 釋放。

---

## Slide 6 — ECC Scheme

**三層 integrity：**

1. **Per-hop routing parity** — 保護 routing 關鍵欄位（destination ID + last-flit indicator），每個 router 與目的 NI sink 都檢查。Mismatch flit 在偵測點直接 drop。
2. **End-to-end whole-flit SECDED ECC** — 涵蓋整張 flit。Source NI 產生，只在目的 NI sink 檢查。Router 不檢查也不重生。
3. **AXI host-side parity**（選用 sideband，預設啟用）— 在 AXI 邊界檢查；只 log 不 enforce 成 SLVERR。

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

**Visual asset：** 兩層 schematic — per-hop parity check 在每個 router；SECDED check 只在目的 NI；AXI parity check 在 host 邊界。

**Speaker notes：** 三層 integrity，每一層 scope 不同。Per-hop routing parity 是最便宜的 check — 對 destination-ID 與 last-flit-indicator 欄位做 1-bit XOR。Router 在每個 output port 都驗；parity 失敗就立刻 drop，避免誤送到錯的 tile。Whole-flit SECDED 是重量級 check — 涵蓋整張 flit（header + payload，不包 syndrome 自己）；source NI 產生、只在目的端檢查。Router 不檢查的原因是這樣會多大概一個 cycle / hop 加一個數量級的 gate count，比 parity bit 貴太多。AXI host-side parity 是選用的第三層在 AXI 邊界，per-byte 涵蓋 data 跟 address — 驗但不 escalate 成 SLVERR。v0.4.0 的 error reporting policy 是統一的：fabric event 一律不 synth SLVERR 給 AXI master。所有 fabric-side error 走 CSR + IRQ 路徑。AXI rresp / bresp 留給 end-to-end memory error（HBM / DDR endpoint ECC 經由自然 rresp 傳回 master）。這跟 AMD 對 uncorrectable ECC 的立場一致 — 並且我們把這個立場乾淨地推廣到所有 fabric event。
