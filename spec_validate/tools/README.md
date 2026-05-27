# Spec Codegen 工具 (tools/codegen.py)

## Path B 架構（single source of truth）

```
spec/ni/doc/*.md  +  spec_validate/ni_function_blocks.json
       │
       │ ni_spec.generator      ← parse MD 產 JSON
       ▼
spec_validate/generated/*.json (auto-gen，不准手改)
       │
       ├──→ Layer 1 schema + Layer 2 invariants 驗證
       └──→ tools/codegen.py → include/*.h → C++ C model
```

`tools/codegen.py` 是統一入口，支援 4 個 domain、C++ target（SV 在 Task 8）。
`gen_cpp_header.py` 已 deprecated，保留為 wrapper。

## 目錄定位

```
noc-sim\
├── spec\ni\doc\*.md                   ← spec source MD
└── spec_validate\
    ├── ni_spec\                       ← Python validator module
    ├── generated\                     ← auto-gen JSON + schema
    ├── ni_function_blocks.json        ← hand-written function blocks source
    ├── tools\
    │   ├── codegen.py                 ← unified entry (NEW)
    │   ├── emit\                      ← per-domain C++ emitters (NEW)
    │   │   ├── common.py              ← provenance banner helper
    │   │   ├── cpp_packet.py
    │   │   ├── cpp_signals.py
    │   │   ├── cpp_registers.py
    │   │   └── cpp_blocks.py
    │   └── gen_cpp_header.py          ← deprecated wrapper
    ├── examples\use_constants.cpp
    └── include\                       ← codegen output (do not hand-edit)
        ├── ni_flit_constants.h
        ├── ni_signals.h
        ├── ni_regs.h
        └── ni_blocks.h
```

## 一條龍指令（spec 改完一鍵驗證 + codegen + build）

```powershell
cd noc-sim\spec_validate
py -3 -m ni_spec ..\spec\ni\doc
py -3 tools\codegen.py --target cpp --domain packet    --out include
py -3 tools\codegen.py --target cpp --domain signals   --out include
py -3 tools\codegen.py --target cpp --domain registers --out include
py -3 tools\codegen.py --target cpp --domain blocks    --out include
py -3 tools\codegen.py --check
g++ -std=c++17 -I include examples\use_constants.cpp -o use_constants.exe
.\use_constants.exe
```

## 分解步驟

所有指令假設 cwd 是 `noc-sim\spec_validate\`。

### 1. 校驗 spec（generator → Layer 1 → Layer 2）

```powershell
py -3 -m ni_spec ..\spec\ni\doc
```

### 2. 跑 codegen 產 C++ headers

```powershell
py -3 tools\codegen.py --target cpp --domain packet --out include
```

支援 `--domain {packet|signals|registers|blocks}`。`--out` 預設為 `spec_validate\include\`。

#### CI 漂移檢查

```powershell
py -3 tools\codegen.py --check
```

重新 regen 到暫存目錄，與已提交的 `include\*.h` 比對（剔除 timestamp 行）。不一致 exit 1。

### 3. Build + run C++ sample

**g++（MSYS2）— 必須把 mingw64/bin 整個放 PATH，不然 linker silent fail**：
```powershell
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
g++ -std=c++17 -I include examples\use_constants.cpp -o use_constants.exe
.\use_constants.exe
```

**MSVC（Developer Command Prompt 內）**：
```powershell
cl.exe /std:c++17 /EHsc /I include examples\use_constants.cpp /Fe:use_constants.exe
.\use_constants.exe
```

## 改 spec 怎麼辦

**唯一改 `spec/ni/doc/packet_format.md` (或對應 phase 的 source MD)**。重跑一條龍指令，所有衍生品（generated JSON / C++ header / binary）自動跟著刷新。

絕對不要手改：
- `generated/ni_*.json` ← 下次跑 generator 會洗掉
- `generated/ni_*.schema.json` ← 這是 schema，hand-maintained 但不該頻繁改
- `include/ni_flit_constants.h` ← 下次跑 codegen 會洗掉

## 修改 codegen 時

`tools/codegen.py` 是 dispatcher；實際 emit 邏輯在 `tools/emit/cpp_*.py`。
所有「怎麼從 JSON 撈值」的邏輯在 `ni_spec.constants`（stable API）——
要加新常數時，先擴充 `ni_spec.constants`，再在對應 emitter 呼叫新 API。

`gen_cpp_header.py` 是已 deprecated 的 wrapper，最終可刪。

## 修改 generator 時

`ni_spec/generator.py` 負責 MD → JSON。新加 spec section 或欄位：
1. 寫 parse 函式（參考既有 `parse_header_fields` / `parse_payload_channels` 模式）
2. 在對應的 `generate_ni_*_json` 組裝結構裡接上
3. 若新增區塊需要 schema 驗證，更新 `generated/*.schema.json`
4. 若新增不變量檢查，加進 `ni_spec/invariants.py`
