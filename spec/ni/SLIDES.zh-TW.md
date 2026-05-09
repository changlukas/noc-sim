# NI 投影片內容（繁中版）

本檔為唯一作業版本。技術名詞、訊號名、暫存器名、規則 ID、AMD pg313 verbatim 引用全部保留英文。

- **聽眾**：內部 review（熟悉 AXI4 + 一般 NoC 概念的工程師）。
- **風格**：Takeaway-first storyline + Arteris-style problem → mechanism → benefit + IP-datasheet 工程語氣。每張先寫 takeaway，通過「audience 帶走什麼」測試的才存在。階層式組織，無分號 narrative。
- **投影片數**：8 張。
- **drafting 狀態**：A5 wave 2026-05-08，storyline lock。

**Storyline structure**：

| # | Role | Title |
|---|---|---|
| 1 | Why | 為什麼要做 NI |
| 2 | What | 架構長這樣 |
| 3 | How #1 | Reorder Buffer |
| 4 | How #2 | Two-layer Integrity (ECC) |
| 5 | How #3 | QoS Generator |
| 6 | How #4 | Credit-Based Flow Control |
| 7 | Trade-off | v0.4.0 的 deliberate 簡化 |
| 8 | Status / Next | 進度與下一步 |

---

## Slide 1 — Why（為什麼要做 NI）

**Takeaway**：AXI4 是 point-to-point protocol，M 個 master 對 N 個 slave 的 crossbar 連線成 O(M·N)，過某個 SoC 規模就不可行。NoC 把連線壓到 O(M+N) 但不自動提供 AXI4 的 ordering / integrity / flow control 保證。NI 是這層 protocol boundary。

### Crossbar 撐不住的點

- AXI4 規定每個 master ↔ slave 對之間有專屬的 5 channel handshake（AW / W / AR / B / R）。
- M × N 全互連 crossbar 在 die area、routing congestion、power 三項同時撐不住。

### NoC 怎麼解

- Master 跟 slave 不直接連線。
- AXI transaction 切成 flit，在 mesh 上經 router 多 hop 路由。
- 連線複雜度 O(M+N)，與 master / slave 數量解耦。

### 但代價落在 protocol 契約

| AXI4 契約 | NoC 不自動保證 |
|---|---|
| Same-axi-id response 必須照 issue 順序回 | flit 走不同路徑、congestion 不同，response 抵達順序不確定 |
| Transaction 不損壞 | flit 過 N 個 router，每 hop 有 SEU / glitch 機率 |
| 多 master 共用資源不 starve | 沒有自動 fairness 機制 |

### NI 的角色

- 把 AXI transaction 翻成 flit 注入 NoC。
- 在 NoC 內部處理上述三層契約。
- 目的端還原回 AXI transaction。
- AXI master 看到的是標準 AXI4 slave。

---

## Slide 2 — What（架構長這樣）

**Takeaway**：NI = NMU + NSU + CSR file + irq_o，per-tile single-chimney（NMU 與 NSU 共用一對 NoC link）。

### NI 四個元件

- **NMU**（Network Manager Unit）— master 路徑的 ingress 與 response unpacking。
  - Receive `axi_req_i`（AW / W / AR）→ packetize 成 flit → 注入 `noc_req_o`。
  - Inbound `noc_rsp_i` flit → unpack 成 `axi_rsp_o`（B / R）→ driving AXI master。
- **NSU**（Network Subordinate Unit）— mirror。
  - Inbound `noc_req_i` flit → unpack 成 `axi_req_o` driving local AXI slave。
  - Slave 回 `axi_rsp_i` → packetize → 注入 `noc_rsp_o`。
- **CSR file**（aclk domain）— software-visible state。
  - QoS Generator config
  - Performance Probes
  - ERR_STATUS（3 RW1C bits）
  - QUIESCE_CTRL / STATUS
  - EXCLUSIVE_MONITOR_CTRL / STATUS
- **`irq_o`** — level-sensitive。任何 unmasked ERR_STATUS bit 為 1 時 assert。

### Single-chimney 拆分

- NMU 與 NSU 內部沒有 cross-coupling。
- 共用點是 NoC link pair。
- 設計來源：FlooNoC `floo_axi_chimney.sv` / AMD pg313 NMU + NSU 拆分慣例。

### Tile 內多 IP 處理

- CPU + DMA + accelerator 在上游 AXI crossbar 仲裁後進入 NI 的 `axi_*_i` port。
- Per-IP 識別透過 AXI ID。
- Flit header 不擴充欄位，保持精簡。

---

## Slide 3 — How #1：Reorder Buffer

**Takeaway**：AXI4 強制 same-axi-id response 順序，但 packet-switched NoC 不保證。NMU 端的 RoB 在 response 路徑做 reorder，把 OoO flit 還原成 in-order AXI response。三 mode 平衡面積與重排能力。

### 問題

- AXI4 ordering rule：same `axi_id` 的 read transaction，response 必須以 issue 順序回到 master。
- Direct crossbar 上 trivially 成立（slave 同條線回，順序不會錯）。
- Packet-switched NoC 不保證。
  - 同一 master 連續發 R0、R1（同 `axi_id`）。
  - R0、R1 走不同路徑、過不同 router、遇不同 congestion。
  - 抵達順序不確定。對 AXI master 是 protocol violation。

### 機制

- NMU 在 response 路徑配 RoB。
- 每筆 read issue 配一個 entry。
- Response flit 進來先 stage 到 entry。
- 等同 `axi_id` 前面所有 entry 都 received，才照 issue 順序 release 到 `axi_*_i.r`。

### 三 Mode 對映

| Mode | 結構 | 面積 | 適用 |
|---|---|---|---|
| `NoRoB`（預設） | 不配 entry | 0 | NoC 自己保證 same-source-same-dest 順序 / single-issue master |
| `SimpleRoB` | 1 個 release pointer，跨 ID 共用 | 小 | 接受 cross-ID HoL blocking |
| `NormalRoB` | per-axi-id linked-list + `prev_dest` adaptive bypass | 最大 | 跨 ID 完整 OoO，同 dest fast-path |

### B / R 獨立配置

- B channel 與 R channel 的 RoB mode 獨立。
- 典型 multi-destination 部署：R 用 NormalRoB、B 用 SimpleRoB（B 是 metadata-only，不需大儲存）。

### AMD pg313 對映

> *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."* — AMD pg313 §NoC Master Unit

NormalRoB 直接實作這個機制。

---

## Slide 4 — How #2：Two-layer Integrity (ECC)

**Takeaway**：Flit 從 source NI 到 dest NI 過 N 個 router，每 hop 都有 SEU 機率。三層分工：routing-critical 欄位 per-hop 驗、其餘端到端驗、AXI 邊界另外驗。Error reporting 統一不從 AXI rresp 合成 SLVERR。

### 設計選擇

三個選項：

- **(a) 端點才檢** — 省 gate，但 mis-route 到錯 NSU 不可偵測。
- **(b) 每 hop full ECC** — 可偵測，但 gate count 與 latency 隨 hop 線性放大。
- **(c) 分層** — routing-critical 欄位 per-hop 檢，其餘 end-to-end 檢。**← 我們選這個**。

### 三層結構

| Layer | 涵蓋範圍 | Gen 點 | Check 點 | 開銷 |
|---|---|---|---|---|
| **Layer 1** — Routing parity | dst_id + last-flit indicator | source NI 注入時 | 每個 router output **+** dest NI sink | 1 bit / flit + 1 XOR tree / hop |
| **Layer 2** — Whole-flit SECDED | header + payload（不含 syndrome 自身） | source NI 注入時 | dest NI sink only | 10-bit syndrome / flit |
| **Layer 3** — AXI host parity | data byte / address byte | AXI master、NSU output | NMU input、AXI slave input、NMU output | 1 bit / byte，optional sideband |

### Layer 3 的 gen / check pipeline（AMD pg313 §Parity verbatim 直接適用）

> *"1 bit per byte for Data."*
>
> *"1 bit per byte for AxAddress."*
>
> *"Parity is checked in the NMU/NSU pipeline when an AXI field is consumed (used by logic). When an AXI field is modified by NMU/NSU logic, parity is regenerated."*
>
> *"Data parity for read responses is generated as 1 bit per byte after the ECC check stage, when the data is converted from NPP to AXI protocol."*

### Error reporting policy

- 三層偵測到的 fabric error 都不透過 AXI rresp / bresp 回報。
- Corrupted flit 用 `OKAY` forward 到目的。
- 錯誤資訊透過 ERR_STATUS（3 RW1C bits：ecc_uncorr / route_par / axi_parity）加 `irq_o` 給軟體。
- AXI rresp / bresp 語意統一為「end-to-end memory error 唯一通道」（HBM / DDR endpoint ECC 經自然 rresp 傳回）。

### 跟 AMD 的關係

- AMD 對 uncorrectable ECC 是 fatal interrupt。
  > *"Uncorrectable ECC errors result in a fatal interrupt."* — AMD pg313 §Data Integrity
- 我們把這個立場一致地推廣到全部 fabric event。

---

## Slide 5 — How #3：QoS Generator

**Takeaway**：不同 master 的 traffic profile 與 SLA 需求不同。NMU ingress 的 QoS Generator 提供 4 mode，讓整合者依 master 類型選 shaping 策略。

### 為什麼需要

- NoC 上多 master 共用 fabric。
- 直接信任 master 提供的 `axi_*qos` 不可靠 — 不是每個 master 都會正確設定 qos。

### 四種 mode

| Mode | 機制 | 典型部署 |
|---|---|---|
| **Bypass** | 透傳 `axi_*qos` | well-behaved master（CPU） |
| **Fixed** | 每 flit 用 CSR 設定的 fixed qos 覆蓋 | 測試 / 單純優先級 master |
| **Bandwidth-limiter** | 觀測 flit injection bandwidth，超過 `BANDWIDTH_LIMIT` 時 qos 降到 `LOW_PRIORITY` | noisy master（DMA） |
| **Urgency-regulator** | 觀測 response bandwidth，落後 `BANDWIDTH_BUDGET` 時 urgency_level 升、effective qos = clamp(`BASE_QOS` + urgency, `SOCKET_QOS`, 15) | 即時 SLA master（accelerator） |

### 實作位置

- NMU ingress（FlitPack 後、Injection Buffer 前）。
- Mode 與所有 threshold 透過 CSR runtime 切換。
- NSU 不重算 QoS。Response flit 透過 MetaBuffer 從 request flit 繼承 `qos` field。

### 限制：QoS 不會 preempt wormhole-locked W-burst

- HEAD flit 一旦 grant，整個 burst 鎖定 output port 直到 `wlast`。
- 中途到的 higher-QoS packet 等待。
- 這是 wormhole-lock 與 QoS 的固有 trade-off — 保 burst 連續性，犧牲中途 preemption。

---

## Slide 6 — How #4：Credit-Based Flow Control

**Takeaway**：Flit 流量需要 backpressure 機制。Credit-based 是 AMD pg313 既有 NoC 標準做法，verbatim 採用。

### 三個 backpressure 設計選項

- **(a) Per-hop ready / valid handshake** — latency 隨 hop 累積、易 deadlock。
- **(b) Per-link send window with explicit ack** — 額外 ack 流量。
- **(c) Credit-based** — receiver 主動 grant 容量、source 持 counter、每送一張遞減、receiver 處理完回 1 credit。**← 我們選這個**。

### AMD pg313 §Credit-Based Flow Control 完整描述（直接適用）

> *"Each NMU, NPS, and NSU source needs to have credit before it can send data to the receiver. After a reset, every NoC component has its source-credit reset to zero. The source unit connects to the destination unit using a bi-directional ready signal that indicates credit exchange is ready. Components wait until both directions are ready before starting the credit exchange. The destination unit can send up to one credit per cycle, per virtual channel, to the source unit. The source unit can send up to one data transaction per cycle to the destination unit."*

### 實作對應

| 元件 | 角色 |
|---|---|
| `noc_*_credit_i[NUM_VC-1:0]` | source 端，receiver 回送的 credit |
| `noc_*_credit_o[NUM_VC-1:0]` | destination 端，source 回送的 credit |
| `noc_*_credit_init_ready_o / _i` | bi-directional handshake：reset 後雙方 ready 才開始 credit 交換 |
| Per-VC credit counter（source 端） | reset 後 = 0，handshake 完後 receiver 連送補 credit |

- Source 在 chosen VC 上 credit > 0 才能 assert `noc_*_valid_o`。
- 每 assert 一張 counter--。

### Failure mode

- Receiver 持續不回 credit（hang、stuck buffer）→ source 在那條 VC 上永久 stall。
- v0.4.0 不做自動 timeout escalation。
- Software detection path：`PENDING_R_COUNT` / `PENDING_W_COUNT` CSR 加 `irq_o`，由軟體決定 retry 或 reset NI。

---

## Slide 7 — Trade-off：v0.4.0 的 Deliberate 簡化

**Takeaway**：三個 deliberate 簡化降低 RTL 複雜度與 DV 規模。代價是把某些 corner case（fabric hang、跨 NI 同步）的處理責任推給軟體。Post-v1 在主架構穩定後重新評估 safety 機制。

### 簡化 #1：no chopping

對照表：

| 項目 | AMD Versal | 我們 |
|---|---|---|
| Flit payload 寬度 | 16 byte | 256 bit（default） |
| Chopping rule | ≥ 256 byte transaction 切成 256-byte 對齊 chunk | 一張 flit 對一筆 AXI message，不切 |
| Burst length | 端到端 chop / re-merge | 端到端保留 |

- **影響**：少 chop tracker、re-merge buffer、chop-aware ordering。DV coverage 規模降低。
- **Trade**：flit width 增加，fabric 線寬隨之上升。在我們 mesh 規模可接受。

### 簡化 #2：fabric error 不合成 AXI rresp

- 三種 fabric 故障路徑的 error 一律透過 ERR_STATUS 加 `irq_o` 通知，不從 AXI rresp / bresp 回報：
  - route_par drop
  - flit_ecc 雙位元錯
  - credit starvation
- **影響**：corrupted flit 用 `OKAY` forward。route_par drop 後 master 端 transaction hang 直到軟體偵測。AXI rresp 語意統一為「end-to-end memory error 唯一通道」。
- **Trade**：integrator 必須在軟體層處理 fabric hang recovery（reset NI、retry policy）。

### 簡化 #3：Exclusive Monitor 與 quiesce 限 single-NI

- NSU Exclusive Monitor 只追蹤本 NI 內部的 LDREX / STREX reservation。
- 跨 NI multi-master coherency 不做（需要 directory protocol 或 snoop bus，是另一層 design）。
- Quiesce 同樣 best-effort — 軟體 poll `quiesce_idle` 直到 set，沒有自動 liveness 上界。
- **影響**：single-NI 範圍內 LDREX / STREX 行為合規。multi-NI 場景下 coherency 由 software 或 system-level mechanism 保證。

---

## Slide 8 — Status / Next

**Takeaway**：Spec 在 architectural level 鎖定（136 rules、51 testpoints、UVM 1.2）。下一步集中在補功能（atomics、Protocol Library）跟回頭做 safety 機制。

### Spec 收斂狀態（A5 wave 結算）

| 項目 | 數量 / 範圍 |
|---|---|
| Protocol rules | 136（126 FAIL + 10 RECOMMEND） |
| Testpoints | 51 |
| ABV assertions | 126（FAIL severity rule 一對一 SVA assert property） |
| Coverage covergroups | 17 |
| FPV scope | RoB allocator state machine no-deadlock、ECC SECDED gen + check round-trip、IRQ function combinational invariant、CDC pointer correctness、reset entry sequencing |
| Framework | UVM 1.2（A5 designer-confirmed） |

Functional coverage 涵蓋：NMU、NSU、CSR、CDC、reset、QoS、ECC、Exclusive、credit、quiesce 各 sub-block。

### 下一步工作項目

#### 1. AXI4 ATOPs 支援

- 目前 sample-only，ATOP transaction 終止為 SLVERR。
- 正式支援估 3 週設計加 DV。
- 列入 v0.5.0 candidate。

#### 2. v0.5.0 Plugin-side Protocol Reference Library

- 標準 AXI4 / AXI4-Lite / APB rule 抽到共用 library。
- BFM spec 只描述特定擴充。
- 攤開規則維護成本，跨多個 BFM spec 共享。

#### 3. Debug / safety 機制

- Outstanding-tx Timeout、watchdog、error injection。
- 等主架構穩定（multi-NI integration 跑過、cross-coverage 收斂）後重啟評估。
- 預估 post-v1 發起。

### Q & A
