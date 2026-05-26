<original_task>
NI (Network Interface) spec 全面 JSON 化計畫，把 `d:\03_CLAUDE\spec\noc-sim\spec\ni\doc\` 下的 Markdown 規格自動轉成機器可讀的 JSON，加 schema validation 跟 C++ codegen。對齊業界 single-source-of-truth 模式 (SystemRDL / Protocol Buffers / IP-XACT)。

主要動機：主管要求工程嚴謹、化繁為簡。Spec 唯一改 MD，所有衍生品（JSON / C++ header / C-model 常數）自動同步。

完整計畫存於：`C:\Users\user\.claude\plans\floofy-churning-snail.md`
</original_task>

<work_completed>

## 整體架構（Path B，已確定）

```
noc-sim/spec/ni/doc/*.md (人寫 source of truth)
     │
     │ ni_spec.generator (Python)
     ▼
spec_validate/generated/*.json (auto-gen，不准手改)
     │
     ├──→ Layer 1 (JSON Schema 驗結構)
     ├──→ Layer 2 (語意/算術不變量)
     └──→ tools/gen_cpp_header.py → include/ni_flit_constants.h → C++
```

## Phase 1: packet_format.md → ni_packet.json ✓ COMPLETED

(本 session 前完成；前置 session 完成 Path A → Path B 轉換)

- `generated/ni_packet.json` auto-generated from `packet_format.md`
- 含 flit/header_fields (12)、payload_channels (5)、field_widths、derived
- Schema、Layer 1+2 validator 全通
- C++ codegen 產 `include/ni_flit_constants.h`
- C++ sample `examples/use_constants.cpp` 編譯 + 跑出 `header[63:0] = 0x00000000F80902AA`

## Phase 2: signal_interface.md → ni_signals.json ✓ COMPLETED (含多次迭代)

### 結構演進
1. **初版**：134 個個別 wire 全列出 → 使用者裁定太複雜
2. **化繁為簡**：只列 §Per-block interface summary 的 7 個 top-level interface
3. **AXI per-channel direction**：每個 AXI interface 加 `channels[5]` (AW/W/B/AR/R) 含 direction/carrier
4. **AXI per-signal breakdown**：每個 channel 內展 `signals[]`，名稱用 channel 前綴 (`AW_ADDR`, `AR_ID` 等)，每 signal 標 `width_param` + `default`
5. **NoC per-signal breakdown**：NoC link 也展 `signals[]` (valid/flit/credit/credit-init handshake)，含 direction

### Terminology renames（已套用 7 個 MD + 對應 JSON）
- `Manager`/`manager` → `Master`/`master` (24 處)
- `Subordinate`/`subordinate` → `Slave`/`slave`
- `Network Manager Unit` → `Network Master Unit`
- `Network Subordinate Unit` → `Network Slave Unit`
- `EN_MGR_PORT` → `EN_MST_PORT` (3 MD, 28 處)
- `EN_SBR_PORT` → `EN_SLV_PORT`

### Bug fixes during Phase 2
- `_section_slice` 用 heading-level 偵測（不被深層 heading 干擾切錯母 section）
- `_split_table_row` 處理 escaped pipe `\|`（不切錯 markdown cell）
- AXI parameter 補完：從 packet_format.md §1.2 Group 3 補抓 AXI_LEN/SIZE/BURST/CACHE 等 widths
- Sanity check：generator 跑時 raise ValueError 如果 INTERFACE_PARAMS 引用的 param 不存在 spec source

### Final shape of ni_signals.json
```
$schema_version
meta (含 auto_generated_from / generator / do_not_edit)
block_enables [2]  (EN_MST_PORT, EN_SLV_PORT)
interfaces [7]
  ├── NMU AXI slave port (host)  + channels[5].signals[~11 each] + port_parameters[1]
  ├── NMU NoC request out        + signals[5] + parameters[2]
  ├── NMU NoC response in        + signals[3] + parameters[2]
  ├── NMU CSR                    + channels[5].signals[~11 each]
  ├── NSU NoC request in         + signals[3] + parameters[2]
  ├── NSU AXI master port (host) + channels[5].signals[~11 each] + port_parameters[1]
  └── NSU NoC response out       + signals[5] + parameters[2]
```

### AXI per-channel signal mapping (in `ni_spec/generator.py :: _AXI_CHANNEL_SIGNALS`)
- AW: ID, ADDR, LEN, SIZE, BURST, CACHE, LOCK, PROT, REGION, USER, QOS
- W:  DATA, STRB, LAST, USER
- B:  ID, RESP, USER
- AR: 同 AW
- R:  ID, DATA, RESP, LAST, USER

ID 寬度依 port type：slave→`IN_ID_WIDTH`、master→`OUT_ID_WIDTH`。

### NoC per-signal mapping (in `_NOC_INTERFACE_SIGNALS`)
- "NoC request out": req_valid_o, req_flit_o, req_credit_i, req_credit_init_ready_o, req_credit_init_ready_i
- "NoC response in": rsp_valid_i, rsp_flit_i, rsp_credit_o
- "NoC request in":  req_valid_i, req_flit_i, req_credit_o
- "NoC response out": rsp_valid_o, rsp_flit_o, rsp_credit_i, rsp_credit_init_ready_o, rsp_credit_init_ready_i

Credit init handshake 只在 `_o` interface 出現（per signal_interface.md §Channel grouping 的非對稱 bundle 設計）。

## 計畫文件新增 (function_blocks.md)

Phase 6 是計畫中唯一需要先 author 新 MD 的 phase。`function_blocks.md` 還沒寫 — 是 NMU/NSU 的高層 function inventory（RoB / QoS / address mapping / ECC 等）。

</work_completed>

<work_remaining>

## Phase 3: registers.md → ni_registers.json (pending)

Source: `noc-sim/spec/ni/doc/registers.md` (210 行)

預期 generated JSON：
```
ni_registers.json
├── meta
├── csr_addr_width (從 MD 抓)
├── csr_data_width (32)
├── access_policy (sub_word / unmapped / misaligned / write_only_read 行為)
├── registers[14] (BASE_QOS / QOS_MODE / ERR_STATUS / IRQ_ENABLE / ...)
│   └── 每個 reg: name, offset, access (RW/RO/RW1C/WO), reset, fields[]
└── err_irq_map (對應表)
```

新增 parsers：
- `parse_csr_policy(md_text)`
- `parse_register_map(md_text)` (§Register map 主表)
- `parse_register_fields(md_text)` (每個 §<REG> Register Field Layout)
- `parse_err_irq_map(md_text)`
- `generate_ni_registers_json(md_dir)` composer

新增 schema `generated/ni_registers.schema.json`

新增 validator (in invariants.py)：
- `check_csr_offset_alignment` — offset % 4 == 0
- `check_csr_offset_unique` — 不撞 offset
- `check_field_bit_tiling` — 每個 reg 內 field 做 bit tiling 檢查
- `check_reset_in_data_width` — reset < 2^csr_data_width

Update `__main__.py` 多跑一個 generator + Layer 1+2 驗 ni_registers.json。

驗證：跟 `deferred/ni_registers.json` (legacy 手寫版) 對拍結構基本一致。

## Phase 4: protocol_rules.md → ni_protocol_rules.json (pending)

Source: `noc-sim/spec/ni/doc/protocol_rules.md` (312 行, 24 條規則)

**Phase 4a 必做**：lift-and-shift 結構化欄位 + condition 留 prose
```
ni_protocol_rules.json
├── meta
├── channel_tokens
├── rules[24]
│   └── { id, proto (AXI4/AXI4LITE/NOC/NI), role, channels, severity, condition_text, source }
```

**Phase 4b 選做**（可延後）：condition 升級成 structured `{kind, predicate}` mini-DSL。

新增 parsers / schema / validator。Validator: check_rule_id_unique, check_rule_channels_exist (引用要在 ni_signals.bundles 找得到)。

## Phase 5: pin_level_reset.md 併入 ni_signals.json (pending)

不獨立生 JSON，而是把每根 wire 補 `reset_behavior` 欄位到 `ni_signals.json` 對應 entry。

注意：目前 Phase 2 簡化版只列 top-level interface，沒列個別 wire。Phase 5 要決定：
- (a) 把 wire-level detail 加回來（部分還原 Phase 2 的初版）
- (b) reset_behavior 只放在 interface level（粗粒度）
- (c) Skip Phase 5（pin_level_reset 留純 MD）

需問 user。

## Phase 6: function_blocks.md (新寫) → ni_function_blocks.json (pending)

**Step 6.1**: 先 author `noc-sim/spec/ni/doc/function_blocks.md`。建議內容在 plan 文件裡。

```markdown
# Function Blocks

## NMU (Network Master Unit)
| Function | Summary |
|---|---|
| AXI Slave Port | 收 local AXI master 的 AW/W/AR transaction |
| AXI-to-flit Packetize | ... |
| QoS Generator | ... |
| Reorder Buffer (RoB) | ... |
| End-to-end ECC Gen | ... |
| Address Mapping | ... |
| Credit-based Flow Control | ... |
| CDC | ... |
| CSR Interface | ... |

## NSU (Network Slave Unit)
| Function | Summary |
|---|---|
| ... |
```

**Step 6.2**: Parser/schema/validator (極簡):
- `parse_function_blocks(md_text)` → `blocks[].functions[]`
- Schema: blocks[role=NMU/NSU].functions[name, summary]
- Validator: role uniqueness, function name uniqueness per role

</work_remaining>

<attempted_approaches>

## 第一版 Phase 2 太細 (134 wires)

走全部 wire 列出的路線：每張 AXI/NoC/CSR sub-section 都 parse、NSU 部分從 slave 反向產生。**使用者裁定太複雜**。改成只列 §Per-block interface summary 的 7 個 top-level entry。

教訓：spec_validate JSON 是給人快速掃讀 + 給 C-model 抓常數用的。Wire 級細節屬於另一層需求（可能等 Phase 5 reset_behavior 時再決定如何回頭加）。

## "AXI parameter 列表" 第一版漏抓

只從 signal_interface.md §Parameters 撈，結果只有 5 個 AXI param (ADDR/USER/IN_ID/NOC_DATA/ENABLE_AXI_PARITY)。**使用者指出 AXI 訊號遠不止這些**。原因：

- signal_interface.md 的 wire table 對 axi_*len/*size/*burst/*cache/*lock/*prot/*region/*resp/*last 用 literal width (8/3/2/4/1/3/4/2/1)
- packet_format.md §1.2 Group 3 才有對應的 AXI_LEN_WIDTH/AXI_SIZE_WIDTH/... 命名

修法：generator 同時讀兩個 MD，packet_format 的 field_widths 補足 namespace。加 sanity check：`INTERFACE_PARAMS` 引用的 name 必須在 namespace 中存在，否則 raise ValueError。

## "name 該分清 channel" — 從 flat params 改 per-channel signals

使用者進一步要求 AXI 訊號名要看出哪個 channel (例 `AW_ADDR` 而非 `AXI_ADDR_WIDTH`)。

從一個 flat `parameters[15]` 改成 `channels[5].signals[]` 結構。每個 channel 自帶 signals 列表，signal name 是 `<CHANNEL>_<FIELD>` (AW_ID, AW_ADDR, ...)，每 signal 標 width_param + default。

## NoC 也要對應處理

NoC 沒 AXI 那種 5-channel 結構，所以 NoC interface 用 interface-level `signals[]` (不在 channels 內)。Schema 加 interface-level signals 允許。

## _section_slice bug

最早 _section_slice 用 `^#{1,4}\s` 找 next heading，意思是任何 1-4 個 # 都算 boundary。問題：parse `### AXI4 Slave port` 時，會被 `#### AW channel` (4 個 #) 截斷，導致 slave section 變空。

修法：parser 偵測 matched heading 的 # 個數，next-boundary 只找同層或更淺。

## Escaped pipe bug

Markdown table cell 內 `EN_SLV_PORT \|\| EN_MST_PORT` 的 `\|` (escaped pipe) 被 my `split("|")` 誤切，導致 EN_SBR_PORT/EN_MGR_PORT row 的 constraint/description 錯位。

修法：`_split_table_row` 用 placeholder 保護 `\|` 再 split，最後還原。

</attempted_approaches>

<critical_context>

## 環境設定

**OS**: Windows 11，工作目錄 `d:\03_CLAUDE\spec`，主要在 `d:\03_CLAUDE\spec\noc-sim\spec_validate`

**Python 環境**: venv at `d:\03_CLAUDE\spec\.venv` (Python 3.13 標準 CPython，非 MinGW)
- `.venv\bin\` 是 junction to `.venv\Scripts\` (Unix-style 相容)
- 已裝 `jsonschema`

**C++ 編譯器**: MSYS2 g++ 在 `C:\msys64\mingw64\bin`
- **PowerShell 用法必須先設 PATH**：`$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"`
- 不然 g++ 找不到 linker 會 silent fail (exit 1 無輸出)

## 一條龍指令

```powershell
cd d:\03_CLAUDE\spec\noc-sim\spec_validate
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
& ..\..\.venv\Scripts\python.exe -m ni_spec ..\spec\ni\doc
& ..\..\.venv\Scripts\python.exe tools\gen_cpp_header.py --out include\ni_flit_constants.h
g++ -std=c++17 -I include examples\use_constants.cpp -o use_constants.exe
.\use_constants.exe
```

預期：兩個 generator OK、Layer 1+2 PASS、C++ binary 印出 `header[63:0] = 0x00000000F80902AA`

## 檔案結構

```
d:\03_CLAUDE\spec\
├── .venv\                         ← Python venv (不在 git)
├── ni_spec.json + .schema.json    ← legacy，可隨時刪
├── validate.py                    ← legacy
└── noc-sim\                       ← git repo (含 source MD + spec_validate)
    ├── spec\ni\doc\               ← 唯一 spec source (人寫)
    │   ├── packet_format.md       ← 已 JSON 化 (Phase 1)
    │   ├── signal_interface.md    ← 已 JSON 化 (Phase 2)
    │   ├── registers.md           ← Phase 3 待做
    │   ├── protocol_rules.md      ← Phase 4 待做
    │   ├── pin_level_reset.md     ← Phase 5 待併入
    │   ├── (其他 5 個 MD 永遠保留 prose: channel_handshake / active_passive_mode /
    │   │     transaction_api / channel_api / theory_of_operation)
    │   └── function_blocks.md     ← Phase 6 待新寫
    └── spec_validate\             ← validator + generator + codegen + samples
        ├── ni_spec\               ← Python module
        │   ├── generator.py       ← 主要 parser/composer (Phase 3-6 都會擴充)
        │   ├── invariants.py      ← Layer 1+2 checks
        │   ├── loader.py, constants.py, report.py, __init__.py, __main__.py
        ├── generated\             ← auto-gen，不准手改
        │   ├── ni_packet.json + .schema.json
        │   ├── ni_signals.json + .schema.json
        │   └── (Phase 3-6 會加 ni_registers / ni_protocol_rules / ni_function_blocks)
        ├── tools\gen_cpp_header.py
        ├── examples\use_constants.cpp
        ├── include\ni_flit_constants.h (auto-gen, in .gitignore? No — committed for convenience)
        ├── docs\plans\2026-05-25-ni-spec-modular-design.md
        ├── whats-next.md          ← 本檔
        ├── .gitignore             ← 排除 __pycache__/, *.exe
        └── deferred\              ← legacy auto-gen，Phase 3-4 完成後可刪
            ├── ni_registers.json
            ├── ni_protocol.json
            ├── ni_interface.json
            └── ni_other_params.json
```

## Memory files (跨 session 自動載入)

位於 `C:\Users\user\.claude\projects\D--03-CLAUDE-spec\memory\`：

- `MEMORY.md` (index)
- `user_role.md` — IC/SoC engineer，NoC NI spec 寫作
- `project_ni_spec.md` — 專案結構 (Path B 後)
- `project_implementation_state.md` — C-model 跟 RTL 都還沒寫
- `project_modular_design_path.md` — Path B 架構決策
- `feedback_manager_rigor.md` — 主管要工程嚴謹、不要冗餘
- `feedback_naming_review.md` — 從 ni_spec.json 搬內容前先做命名審查
- `feedback_no_silent_additions.md` — 不擅自加使用者沒要求的檔案/抽象層

**新機器要載入這些 memory**：複製 `C:\Users\user\.claude\projects\D--03-CLAUDE-spec\memory\` 整個目錄到新機器同位置。

## 關鍵設計決策（已敲定）

1. **Path B**：MD 是唯一 source of truth，JSON auto-generate from MD。**絕對不能手改 generated/**。
2. **每個 domain 一個 JSON**：packet / signals / registers / protocol_rules / function_blocks 各自一份。
3. **不全 JSON 化**：channel_handshake / active_passive_mode / transaction_api / channel_api / theory_of_operation 永遠保留 prose（過度 JSON 化是 over-engineering）。
4. **Timing 三層**：L1 數字常數進 JSON、L2 protocol rules 進 ni_protocol_rules.json、L3 cycle-accurate 行為留 C++ code。
5. **AXI signal 命名用 channel 前綴**：`AW_ADDR`/`AR_ID` 而非 `AXI_ADDR_WIDTH`。
6. **Terminology**：Master/Slave (不用 Manager/Subordinate)。

## 使用者偏好/工作習慣

- 重視「化繁為簡」、不喜歡 over-engineering
- 不要擅自加檔案/抽象層 — 要先 propose 再做
- 命名要用業界標準（JSON / Web / Hardware spec）
- 主管對 AI 產出敏感，要工程紮實感
- 從 ni_spec.json 搬內容前先做命名審查
- 認真追究：發現問題會直接點出（如 "ENABLE_AXI_PARITY 那邊不對"、"NoC 怎沒處理"）
- Auto mode 開著時可以直接動手，但不要超出 scope

## 第三方參考（已在 plan 引用）

- SystemRDL / Accellera — register description language
- IP-XACT (IEEE 1685) — IP component metadata
- Protocol Buffers — `.proto` source + `protoc` 產 code
- ARM AMBA AXI4 (IHI 0022) — AXI 訊號規格
- AMD Versal NoC (pg313) — Spec 對齊參考
- FlooNoC — chimney 概念對齊

## 開放問題

1. **Phase 5 pin_level_reset 怎麼整合**：Phase 2 簡化版只列 top-level interface，沒列個別 wire。要決定 (a) wire-level 加回來、(b) reset_behavior 只放 interface level、(c) skip。需問 user。

2. **Phase 4b mini-DSL**：是否要把 protocol_rules.condition 升級成 structured predicate。可延後到 C-model 真要用時再做。

3. **deferred/ 清理**：完成對應 Phase 後可刪。目前還留作 reference。

4. **noc-sim git commit**：rename + cross-ref 改動還沒 commit/push 上 GitHub。User 自己決定時機。

</critical_context>

<current_state>

## 整體進度

- ✅ Phase 1 (packet_format.md → ni_packet.json) — 完整
- ✅ Phase 2 (signal_interface.md → ni_signals.json) — 完整含多次迭代
- ⏳ Phase 3 (registers) — 未開始
- ⏳ Phase 4 (protocol_rules) — 未開始
- ⏳ Phase 5 (pin_level_reset 併入 signals) — 未開始
- ⏳ Phase 6 (function_blocks 新寫 + JSON 化) — MD 未寫

## 目前可跑的 demo（全 PASS）

```powershell
cd d:\03_CLAUDE\spec\noc-sim\spec_validate
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
& ..\..\.venv\Scripts\python.exe -m ni_spec ..\spec\ni\doc
# 輸出:
# == NI Spec Validator ==  target: ni_packet.json + ni_signals.json
# --------------------------------------------------------------------
#   Generator (MD -> JSON)      : OK (ni_packet.json + ni_signals.json)
#   Layer 1 (JSON Schema, both) : PASS
#   Layer 2 (packet arithmetic) : PASS
# --------------------------------------------------------------------
#   總計: 0 error, 0 warning
#   結果: 規格通過校驗 ✓
```

C++ chain output:
```
header[63:0] = 0x00000000F80902AA   ← 不變，驗證 packet 部分沒退步
```

## 任務 list (在 TaskCreate system)

```
#17 [completed] Phase 2: signal_interface.md → ni_signals.json
#18 [pending]   Phase 3: registers.md → ni_registers.json
#19 [pending]   Phase 4a: protocol_rules.md → ni_protocol_rules.json (prose condition)
#20 [pending]   Phase 5: pin_level_reset.md 併入 ni_signals.json
#21 [pending]   Phase 6: function_blocks.md (新寫) → ni_function_blocks.json
```

(早期 task #1-16 都 completed 或 deleted)

## noc-sim git 狀態 ✓ 已 commit + push (2026-05-26)

Commit `5d0161f` 推上 origin/master：
- `02_flit.md` rename → `packet_format.md` (97% similarity，git 自動偵測)
- 15 個 cross-ref MD 更新 (spec/ni/ + docs/design/ 內部連結)
- 24 處 Manager/Subordinate → Master/Slave (含 NMU/NSU 展開)
- 28 處 EN_MGR_PORT → EN_MST_PORT, EN_SBR_PORT → EN_SLV_PORT
- `packet_format.md` §1.2 Group 2 漏改 QOS_WIDTH → NOC_QOS_WIDTH 修正
- `packet_format.md` §2.1 / §2.2.1 noc_qos rename + placeholder 描述

22 files changed, 123/123 insertions/deletions。

**新機器拿 noc-sim 直接 clone 即可**：
```
git clone https://github.com/changlukas/noc-sim d:\03_CLAUDE\spec\noc-sim
```

## 換機器 setup checklist

1. **Clone source（含 spec_validate）**：
   - `git clone https://github.com/changlukas/noc-sim d:\03_CLAUDE\spec\noc-sim` ← 自從 commit `60c2916` (2026-05-26) 起，spec_validate/ 也在 repo 內，clone 完整套就到位

2. **Python 環境**：
   ```
   py -3.13 -m venv d:\03_CLAUDE\spec\.venv
   d:\03_CLAUDE\spec\.venv\Scripts\python.exe -m pip install jsonschema
   ```

3. **Junction (optional, 為了 Unix-style 路徑)**：
   ```
   cmd /c mklink /J d:\03_CLAUDE\spec\.venv\bin d:\03_CLAUDE\spec\.venv\Scripts
   ```

4. **C++ 編譯器**：裝 MSYS2 → mingw64 → g++ (路徑要對齊或調整 PATH 命令)

5. **Memory**：複製 `C:\Users\user\.claude\projects\D--03-CLAUDE-spec\memory\` 整個資料夾

6. **驗證**：跑一條龍指令，預期 Layer 1+2 PASS + `0xF80902AA`

7. **計畫文件**：複製 `C:\Users\user\.claude\plans\floofy-churning-snail.md`

## 下一步 (新機器繼續工作)

最直接：開 Phase 3 (registers.md → ni_registers.json)。

Plan 文件 §Phase 3 已詳述：parser、schema、validator 該長怎樣。可以直接依該規格動手。

User 在離開前最後一個動作是：問「NoC 也對應處理可以嗎？」(已完成)、然後說要換機器。沒有未回答的問題或未處理的請求。

</current_state>
