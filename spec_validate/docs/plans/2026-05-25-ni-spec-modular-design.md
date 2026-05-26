# NI Spec 模組化設計（2026-05-25）

> **架構大轉彎（2026-05-26）**：從原本「MD 跟 JSON 都人寫，cross-check 防漂走」
> 改成 **Path B：MD 是 single source of truth，JSON auto-generate**。對齊 SystemRDL /
> Protocol Buffers 業界做法。
>
> 變化：
> - `packet_format.md` 是**唯一**人寫規格
> - `generated/ni_packet.json` auto-gen 自 MD，不准手改
> - Layer 3 cross-check **刪除**（沒兩份要對拍）
> - `crosscheck.py` → `generator.py`（parser 從「對拍工具」變「生成工具」）
> - `parameters[]` 區塊**砍掉**（MD 沒帶 constraint 資訊，靠 MD review 把關）
>
> 新檔案佈局：
> - **Source**：`noc-sim/spec/ni/doc/packet_format.md`（你 ONLY edit this）
> - **Auto-gen**：`spec_validate/generated/ni_packet.json`
> - **Schema**（hand-maintained，驗 auto-gen）：`spec_validate/generated/ni_packet.schema.json`
> - **Generator + validator**：`spec_validate/ni_spec/`（含 generator.py / invariants.py / ...）
> - **Codegen**：`spec_validate/tools/gen_cpp_header.py` 讀 `generated/`，產 `include/ni_flit_constants.h`
> - **C++ sample**：`spec_validate/examples/use_constants.cpp`
> - **本文件**：`spec_validate/docs/plans/`

## 背景

`noc-sim/spec/ni/` 的規格目前以 Markdown 寫成（人類可讀），但主管要求改用 JSON/YAML 形式（machine-readable）+ 校驗器。已存在的 `ni_spec.json` + `validate.py` 是第一版實作。

主管的進階要求：「**校驗器是 C-model 原身**」。其含義為：spec 是 single source of truth，validator 與未來 C-model 共用同一份 spec、同一組常數、同一套不變量。Validator 的程式碼結構決定 C-model 是否能無痛接上。

專案章程 (`noc-sim/docs/PROJECT_GOALS.md`) 明定 C-model 為 RTL 的 cycle-accurate golden reference。但目前 C-model 尚未實作，RTL 也僅有 port shell。Spec + Validator 是目前唯一的軟體產物。

## 目標

化繁為簡：本次只處理 **Packet format**（高優先）。其餘 section（registers / protocol_rules / interface wire tables）暫移至 `deferred/`，不刪除、不擴張 scope。

具體交付：
1. 拆檔 — `ni_spec.json` 拆成多個聚焦檔案
2. 三層校驗 — Layer 1 結構 / Layer 2 算術 / Layer 3 跨文件 cross-check
3. Validator Python 模組化 — spec-loading 與常數萃取可被未來 C-model 直接 import
4. Block-level functionality 機器化 — `ni_nmu.json` / `ni_nsu.json` 列出每個 block 的功能，跨 reference 至 packet field

## 檔案配置

```
d:\03_CLAUDE\spec\
├── ni_packet.json              ← flit format + 相關 parameters
├── ni_packet.schema.json
├── ni_nmu.json                 ← NMU block functionality
├── ni_nmu.schema.json
├── ni_nsu.json                 ← NSU block functionality
├── ni_nsu.schema.json
├── deferred\                   ← 暫時不動，保留歷史
│   ├── ni_registers.json
│   ├── ni_protocol.json
│   ├── ni_interface.json       ← 目前只有 channel_tokens
│   ├── ni_other_params.json    ← flit 沒用到的 parameters
│   └── (對應 schema)
├── validate.py                 ← 改成薄殼，呼叫 ni_spec 模組
└── ni_spec\                    ← 新 Python 模組
    ├── __init__.py
    ├── loader.py
    ├── constants.py
    ├── invariants.py
    └── crosscheck.py
```

## ni_packet.json 內容

從原 `ni_spec.json` 抽出 `flit` 區塊，加上 flit 實際引用的 parameters 子集（約 8-12 個）。對 header field 補上 `encoding` 欄位以支援 §2.2 enum cross-check：

```json
{
  "name": "axi_ch",
  "width": 3, "lsb": 4, "msb": 6,
  "stage": "arbitration",
  "encoding": { "0": "AW", "1": "W", "2": "AR", "3": "B", "4": "R" },
  "reserved_values": ["5-7"]
}
```

## ni_nmu.json / ni_nsu.json 內容

```json
{
  "block": {
    "name": "NMU",
    "fullname": "Network Manager Unit",
    "role": "AXI-side injection / response sink"
  },
  "features": [
    {
      "id": "FEAT-NMU-ROB",
      "name": "Reorder Buffer",
      "summary": "Per-AXI-ID in-order response release。NoC 回 response 可亂序，RoB 還原 AXI ordering 合約。",
      "modes": ["Normal", "Simple", "NoRoB"],
      "uses_packet_fields": ["rob_req", "rob_idx"],
      "configured_by": ["ROB_MODE"],
      "related_features": [],
      "source_doc": "noc-sim/spec/ni/README.md §Features; doc/04_network_interface.md"
    }
  ]
}
```

跨 block features（例：end-to-end ECC）用 `related_features` 雙向引用。

## 三層校驗

| Layer | 範圍 | 工具 |
|---|---|---|
| L1 | 結構 / 型別 / 必填 / enum / regex | JSON Schema Draft 2020-12 |
| L2 | 算術不變量、bit tiling、SECDED bound、跨欄位一致性 | `ni_spec.invariants` |
| L3 | Markdown ↔ JSON cross-doc consistency；block features ↔ packet fields | `ni_spec.crosscheck` |

L3 包含：
- `packet_format.md` §2.1 header bit allocation ↔ `ni_packet.json` (auto)
- `packet_format.md` §2.2 sub-field enum tables ↔ `ni_packet.json.flit.*.encoding` (auto)
- `packet_format.md` §3 payload bit allocation ↔ `ni_packet.json` (auto)
- `ni_nmu/nsu.json` 的 `uses_packet_fields` 引用 ↔ `ni_packet.json` 真實存在的 field (auto)
- `ni_nmu/nsu.json` 的 `related_features` 雙向引用 (auto)
- `ni_nmu/nsu.json` 的 `configured_by` 暫不檢查（registers 在 deferred）

## ni_spec Python 模組 API

```python
# 1. 載入 bundle
from ni_spec import load_spec_bundle
bundle = load_spec_bundle("d:/03_CLAUDE/spec")
# bundle.packet, bundle.nmu, bundle.nsu

# 2. 全部檢查
from ni_spec import check_all, print_report
report = check_all(bundle, md_dir="d:/03_CLAUDE/spec/noc-sim/spec/ni/doc")
print_report(report)

# 3. 常數萃取（C-model 直接吃這層）
from ni_spec import constants
constants.flit_width(bundle.packet)              # 406
constants.header_field_pos(bundle.packet, "rob_idx")  # (29, 33)
constants.axi_channel_encoding(bundle.packet)    # {"AW":0, ...}
```

## C-model 接點

**Python C-model**：直接 `from ni_spec import constants` 使用。

**C++ C-model**：寫 `tools/gen_cpp_header.py` 從 `ni_spec.constants` 產 `ni_flit_constants.h`，C++ side `#include` 取得同一組常數。零手抄、零 desync。

## 實作步驟

1. 拆檔：抽出 `flit` 與相關 parameters 到 `ni_packet.json`；其餘移到 `deferred/`
2. 建立 `ni_packet.schema.json`（從現有 `ni_spec.schema.json` 萃取對應子集）
3. 建立 `ni_spec/` 模組骨架（loader / constants / invariants 三個檔案）
4. 重寫 `validate.py` 變薄殼
5. 驗證 step 1-4 跑起來行為等價於拆檔前
6. 新增 `ni_spec/crosscheck.py`（先做 §2.1 header bit allocation）
7. 擴充 crosscheck 至 §2.2 enum tables 與 §3 payload
8. 建立 `ni_nmu.json` / `ni_nsu.json`（從 README §Features 轉錄）
9. 驗證 `uses_packet_fields` / `related_features` 引用檢查

每步完成後跑 `validate.py` 確認無 regression。

## 不在本次範圍

- `ni_registers.json` 內容驗證或結構改寫
- `ni_protocol.json` 內容驗證或結構改寫
- Interface wire table 機器化（signal_interface.md §Wire table）
- C-model 真正實作
- RTL 修改

這些列為 Phase 2 候選。
