# NI 投影片內容（繁中版）

本檔為 `SLIDES.md` 的繁體中文對映版本，與英文版同步維護（參照 plugin README 雙語慣例）。技術名詞、訊號名、暫存器名、規則 ID、AMD pg313 verbatim 引用全部保留英文。

- **聽眾**：內部 review（熟悉 AXI4 + 一般 NoC 概念的工程師；非 Versal 專案背景）。
- **投影片數**：15 張，章節順序對齊 AMD pg313 NoC Architecture。
- **drafting 狀態**：A5 wave 2026-05-08，進行中。

**Drafting 進度：**

| Batch | Slides | 狀態 |
|---|---|---|
| A | 1–2（Title + Components） | ✓ drafted |
| B | 3–8（NMU） | ✓ drafted |
| C | 9–10（NSU） | pending |
| D | 11–12（NPS context + Credit） | pending |
| E | 13–14（Communication + Data Integrity） | pending |
| F | 15（Closing） | pending |

---

## 投影片格式約定

每張 slide 區塊包含：

1. **On-slide content（投影片畫面內容）** — 實際顯示在 slide 上的 bullet / 表格 / 圖檔位。簡短，AMD verbatim 直接放這。
2. **Speaker notes（口述稿）** — 講者口頭講的內容，敘述較完整。
3. **Visual asset（視覺素材）** — 主圖檔路徑 / 來源。
4. **AMD verbatim quotes（AMD 直接引用）** — 預先排版好的 quote block，附 attribution。

跨檔引用以 `spec/ni/` 為相對路徑根。

---

## Slide 1 — Title + Scope

### On-slide content

> **AXI4-over-NoC Network Interface**
>
> Design + spec walkthrough — v0.4.0, A5 wave 2026-05-08

- 每個 tile 一個 NI、單一 chimney（NMU + NSU 共用一對 NoC link）。
- 本投影片只涵蓋 NI 範圍；router (NPS) 為另一份 spec。
- 章節順序：AMD pg313 NoC Architecture。

### Speaker notes

本 deck 走過 AXI4-over-NoC Network Interface 這個 block。NI 是 per-tile 的 chimney，把 AXI4 流量轉換進 NoC flit、反向亦同 — manager-side ingress (NMU) 與 subordinate-side egress (NSU) 都住在同一個 NI 內。我們依 AMD pg313 章節順序排列，方便熟悉 Versal NoC 的 reader 直接 cross-reference；我們設計跟 AMD 不同的地方都明確標出來，是 deliberate design choice。Router（AMD 用 NPS 一詞）是相鄰的基礎建設，spec 另立。

### Visual asset

無 — title slide。可選擇在角落放小張 tile 縮圖。

### AMD verbatim quotes

無。

---

## Slide 2 — NoC Components Overview

### On-slide content

**NI 內部組成：**

- **NMU**（Network Manager Unit）— AXI master → flit injection。
- **NSU**（Network Subordinate Unit）— flit reception → AXI slave。
- **CSR file** — runtime control（QoS / Probes / Errors / Quiesce / Exclusive）。
- `irq_o` — level-sensitive 中斷。

**Single-chimney per tile** — NMU + NSU 共用一對 NoC link。

| Link 方向 | NMU | NSU |
|---|---|---|
| `noc_req_o` | drives | — |
| `noc_req_i` | — | samples |
| `noc_rsp_o` | — | drives |
| `noc_rsp_i` | samples | — |

- 半邊 enable：`EN_MGR_PORT` / `EN_SBR_PORT`。

### Speaker notes

NI 是 AXI4 protocol 與 NoC flit protocol 的邊界。NI 內部有兩個功能上獨立的半邊 — NMU 與 NSU — 在每個 tile 共用一對 NoC link 實體連線。NMU 負責本地 AXI master 發出的 outbound request 加上 inbound response；NSU 處理 inbound request 對應到本地 AXI slave 加上 outbound response。兩半邊在 NI 內部不會直接溝通 — 它們唯一的耦合點是共用的 NoC link pair（single-chimney pattern）。這個設計對齊 FlooNoC `floo_axi_chimney.sv` topology 與 AMD pg313 NMU + NSU 拆分。Tile 內如果有多個 IP（CPU + DMA + accelerator），會經由上游 AXI crossbar 多工進到 NI；per-IP identification 靠 AXI ID。CSR file 是 software 看 NI 的窗口：載 QoS configuration、Performance Probes、四種 error event class 的 ERR_STATUS、NMU runtime quiesce control、NSU Exclusive Monitor clear trigger。`irq_o` 是單一 level-sensitive 線，當任何 unmasked ERR_STATUS bit set 就 assert。

### Visual asset

**選定 Option B — NMU + NSU diagram side-by-side 並排顯示。**

- NMU diagram 來源：`images/NMU_block_diagram.md`（mermaid）。
- NSU diagram 來源：`images/NSU_block_diagram.md`（mermaid）。
- 預先 render：用 mermaid CLI 輸出 SVG 後再貼到投影片。
- **⚠ 前置條件：** 兩張 diagram 都有已知 drift items（見 `PRESENTATION_OUTLINE.md` §Open issues #1 — `VC Arbiter` → `VC Mapping`、`route_par` 9-bit cover、`port_id` removal、A4.5 stamp、NMU header text "Master" → "Manager"）。drift fix 必須先做，否則 slide 會顯示過期內容。

### AMD verbatim quotes

- ✓ AMD 對 NMU/NSU/NPS 角色的 high-level 描述夠通用，可用。
- ✗ Versal-specific 的 component 名稱（PL master、AI Engine、CIPS coupling）— 不要放。

---

## Slide 3 — NMU Architecture overview

### On-slide content

**功能（AMD pg313）：** *"asynchronous clock domain crossing and rate matching between the AXI master and the NoC."*

**10 個 sub-block（AXI ← → NoC）：**

AddrTrans · QoSGen · FlitPack · ECC Gen · Injection Buffer · VC Mapping · ECC Check · RoB · FlitUnpack · Outstanding-tx Timeout。

**vs AMD NMU 額外加的：** two-layer ECC · Outstanding-tx Timeout · CSR + `irq_o`。

### Speaker notes

走一遍資料流方向。AW/AR 從左邊進來 → AddrTrans 解出 `dst_id` → QoSGen 計算 flit `qos` → FlitPack 組 flit payload → ECC Gen 附上 `route_par` + `flit_ecc` → Injection Buffer 依 VC 排隊 → VC Mapping 選 egress VC → flit 從 `noc_req_o` 注入。Response 路徑反過來：`noc_rsp_i` → ECC Check → RoB 確保 per-AXI-ID 順序 → FlitUnpack 還原 B/R → 給 AXI master。Outstanding-tx Timeout 是安全網 — 任何 allocated tracker 在 `TXN_TIMEOUT` cycle 內沒看到 response 就強制以 SLVERR 收掉。各 sub-block 細節分散到 slides 4 (burst)、6 (RoB)、7 (timeout)、8 (errors)、12 (credit)、14 (data integrity) 這幾張。Layout 慣例：AXI 在左、NoC 在右（AMD pg313 標準）。

### Visual asset

`images/NMU_block_diagram.md`（mermaid）→ 用 mermaid CLI render 成 SVG。

**⚠ 前置條件：** `PRESENTATION_OUTLINE.md` §Open issue #1 列的 drift fix（D1 `VC Arbiter`→`VC Mapping`、D3 `route_par` 9-bit cover、D5 `port_id` removal、D9 A4.5 stamp、D11 NMU header `Master`→`Manager`）必須先做完才能 render。

### AMD verbatim quotes

> "Asynchronous clock domain crossing and rate matching between the AXI master and the NoC."
>
> *— AMD pg313 §NoC Master Unit*

- ✗ skip：NMU Versions（NMU512 / HBM_NMU / NMU128）、RROB 64-entry × 32-byte、512B Write Buffer。

---

## Slide 4 — AXI Memory Mapped Support + Burst handling

### On-slide content

- 5 個 AXI4 channel（AW / W / AR / B / R）— ARM IHI 0022。
- Burst type：FIXED / INCR / WRAP。`AWLEN ≤ 255`。
- INCR 4KB-boundary check。
- **不做 chopping** — 每筆 AXI message 對應一個 wide flit。**跟 AMD pg313 的 256-byte chopping 不同。**
- Narrow transfer：`wstrb` 重新產生 + over-fetch。

### Speaker notes

AXI master 看到的就是標準 AXI4 protocol — wire 上沒有意外。`AWLEN ≤ 255` 是 AXI4 max length；WRAP boundary 是 AXI4 自動限定的（一定在 4KB 內）。INCR 4KB-cross check 是 AXI4 spec 強制（`AXI4_SLV_AW_BURST_4KB_BOUNDARY`）。NMU 內部，當 master 比 NoC 窄時，FlitPack 會把 W beat 累積進一個 wide flit（Upsize block）。`wstrb` 每張 wide flit 都會重新產生，下游 slave 只 commit master 真的有寫的 byte。我們刻意不在任何 boundary chop bursts — 跟 AMD Versal 256-byte chopping 不同。我們的 wide flit 格式讓 chopping 不必要。

### Visual asset

概念示意圖：AXI master 窄 W beat（例如 4 × 64-bit）→ 累積成一個 wide W flit（256-bit payload）放在 `noc_req_o` 上。PPT 手繪；沒有 mermaid 來源可重用。

### AMD verbatim quotes

- ⚠ rephrase：AMD burst-conversion 概念對得上；具體不同（不 chop）。包裝為「我們的 wider flit 讓 chopping 不必要」。
- ✗ skip：AMD「256-byte aligned segments」chopping rule、AMD 特定的 512B Master-write-buffer。

---

## Slide 5 — NMU Addressing + SAM + Destination ID

### On-slide content

**3 種 routing mode**（`ROUTE_ALGO`）：`XYRouting`（預設）· `SourceRouting` · `IDRouting`。

`dst_id = (X, Y)` — 預設 4+4 = 8 bits，從 awaddr/araddr 中以 `XY_ADDR_OFFSET_X/Y` 抽出。

`Sam` table — compile-time parameter；**v0.4.0 沒有 runtime CSR**（要改就重 elaborate）。

### Speaker notes

三種 routing mode 給整合者依不同 topology 選用。XYRouting 是規則 mesh 的預設 — 快、簡、deterministic。SourceRouting 適合在 flit 構建時就知道 route 的 pre-computed 路徑。IDRouting 直接把 AXI ID 透過 SAM lookup table 對到 `dst_id`。AddrTrans 是 combinational lookup + registered output。AMD Versal NoC 自己的 Master-Specified ID 與 Re-mapping 機制是綁 PL-interconnect 耦合 — 那是 Versal-specific，不是一般 AXI4-NoC 概念的一部分。

### Visual asset

地址 bit 分解圖，標出 `axi_awaddr_i` 哪幾位給 X/Y coordinate（依 `XY_ADDR_OFFSET_X` / `XY_ADDR_OFFSET_Y`）。PPT 手繪或簡單的 ASCII bit-field 圖。

### AMD verbatim quotes

- ⚠ rephrase：address remap 概念對得上；我們的 `Sam` rule 結構跟 AMD address-map 不一樣。
- ✗ skip：Master-Specified ID、Re-mapping、7-bit address parity bit map — Versal-specific。

---

## Slide 6 — NMU Read Reorder Buffer

### On-slide content

**AMD pg313：** *"Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."*

| Mode | 成本 | 適用 |
|---|---|---|
| `NoRoB`（預設） | 最低 | network 自己保證順序 |
| `SimpleRoB` | 小 | naive FIFO；可接受 cross-ID HoL |
| `NormalRoB` | 最大 | per-AXI-ID linked-list + `prev_dest` bypass |

- B / R mode 獨立：`B_ROB_TYPE`、`R_ROB_TYPE`。
- Designer-confirmed (2026-05-08)：lowest-index-first；同時 ready 的 entry 以較低 `rob_idx` 先 release。

### Speaker notes

RoB 大小是 NMU 面積主導因素。最大 config `R_ROB_TYPE=NormalRoB, MAX_TXNS=32, DATA_WIDTH=256, MAX_BURST_LEN=256` 時 R-RoB 最壞情況 2 Mbits。預設 `MAX_BURST_LEN=16` 同樣 NormalRoB 降到 128 Kbits — 典型部署數字。NormalRoB 的 `prev_dest` adaptive bypass 是 performance optimization：當連續同 AXI ID 的 request 打到同一個目的 NSU，NormalRoB 跳過 per-ID linked-list overhead 走 fast-path。典型多目的部署：`R_ROB_TYPE = NormalRoB`、`B_ROB_TYPE = SimpleRoB`（B 是 metadata-only，靠 `ONLY_METADATA_B=true`）。AMD Versal NoC RROB 是 64-entry × 32-byte（HBM 變體 64×64）；我們預設較保守，MAX_TXNS=32。

### Visual asset

RoB state machine：`FREE → ALLOCATED → RESPONSE_RECEIVED → READY_TO_RELEASE → FREE`。PPT 手繪或 mermaid `stateDiagram` block。

### AMD verbatim quotes

> "Read re-tagging via linked-list RROB structure to maintain AXI ordering compliance."
>
> *— AMD pg313 §NoC Master Unit §Read Reorder Buffer*

- ⚠ rephrase：AMD RROB 64-entry × 32-byte 預設；引用我們的 `MAX_TXNS=32`。

---

## Slide 7 — Outstanding Tx + Write Response Tracker + Write Ordering

### On-slide content

**AMD pg313：** *"32 outstanding read and 32 outstanding write transactions."* → 我們預設 `MAX_TXNS = 32`。

- **Timeout**：`TXN_TIMEOUT = 10 000 aclk_i` cycles → `SLVERR` + `ERR_STATUS[1]` + `ERR_COUNT++`。
- **fabric error 唯一 AXI-rresp 合成路徑**。涵蓋 slave-no-response / flit-loss / route_par-drop。
- **Write ordering**：W-burst wormhole-locked（`NOC_MST_WORMHOLE_LOCK`）。W burst 在送、AR 不能注入。
- 合成目標（代表性）：1.2 GHz `noc_clk_i` / 800 MHz `aclk_i`。

### Speaker notes

Outstanding-tx tracking 是安全網。NoC error path 在 wire 上產生三種無法區分的徵狀 — 永遠沒回應 — timeout 把每種轉成 deterministic AXI SLVERR，1 GHz `aclk_i` 下 10 µs 內完成。ISR 透過 per-class counter（`ROUTE_PAR_ERR_CNT`、`ECC_UNCORR_ERR_CNT`）加 `LAST_ERR_INFO` 來判斷實際 hit 哪個 case。`MAX_TXNS_PER_ID = 32` per AXI ID。NMU 輸出端的 wormhole-lock 把 W-burst beat 序列化在 `noc_req_o` 上 — 讓 burst 連續到 NSU、方便 reassembly。Reference implementation：FlooNoC `rr_arb_tree LockIn=1`，只在 `last & ready` 才 release。1.2 GHz / 800 MHz 是代表性 target；整合者依實際部署調整（designer-confirmed 2026-05-08）。

### Visual asset

時序圖：AW handshake → flit 注入 → 多個 cycle 沒回應 → `TXN_TIMEOUT` counter expire → SLVERR 送回 AXI master。再附一個小視窗顯示 wormhole-locked W-burst 序列（W beat 在 `noc_req_o` 上連續）。

### AMD verbatim quotes

> "32 outstanding read and 32 outstanding write transactions."
>
> *— AMD pg313 §NoC Master Unit §Outstanding Transaction Support*

- ✗ skip：AMD 的「32-entry interleaved read tracker and 32-entry chop-merge write tracker」— chop-merge tracker N/A（我們不 chop）。

---

## Slide 8 — NMU Error Conditions + IRQ

### On-slide content

| Bit | Event | Counter |
|---|---|---|
| `[0]` | ecc_uncorr_err | `ECC_UNCORR_ERR_CNT` |
| `[1]` | timeout_err | `ERR_COUNT` |
| `[2]` | route_par_err | `ROUTE_PAR_ERR_CNT` |
| `[3]` | axi_parity_err | `AXI_PARITY_ERR_CNT` |

- **RW1C**：寫 1 → bit + counter 同時 atomically clear。
- `irq_o = OR(ERR_STATUS & IRQ_ENABLE)` — level-sensitive。
- **AMD pg313：** *"By default, all interrupts are masked."*
- `LAST_ERR_INFO` **sticky** — 第一個未 clear 的 error 留著。
- **(B)-philosophy：** *"Uncorrectable ECC errors result in a fatal interrupt"*（AMD pg313）— 不從 ECC 路徑合成 SLVERR。

### Speaker notes

四個 event class 共用同一 pattern：每個 `ERR_STATUS` bit 配一個 saturating counter，由 RW1C write 同時 atomically clear。`irq_o` level-sensitive — software ISR 看 bit pattern、讀 `LAST_ERR_INFO` 取得 offending-transaction context（`err_axi_id`、`err_src_id`、`err_dst_id`）、再看 per-class counter 取得累積數。(B)-philosophy 的決策是刻意的：fabric-level ECC error 不升級成 AXI SLVERR — 而是把 corrupted flit 用 `bresp/rresp = OKAY` forward 到目的端，靠 application-level integrity check（HBM ECC 在 endpoint、software CRC）做 recovery。這跟 AMD Versal NoC 一致 — 他們也是把 uncorrectable ECC 升到 fatal interrupt，不去合成 rresp。

### Visual asset

Register layout view：`ERR_STATUS[3:0]` + `IRQ_ENABLE[3:0]` + `LAST_ERR_INFO` field map 並排顯示。PPT 手繪。

### AMD verbatim quotes

> "Uncorrectable ECC errors result in a fatal interrupt."
>
> *— AMD pg313 §Data Integrity*

> "By default, all interrupts are masked."
>
> *— AMD pg313 §Data Integrity*

- ✗ skip：Versal-specific「fatal interrupt」exception escalation policy。
