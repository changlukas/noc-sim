# Spec → C++ Codegen 工具

## Path B 架構（single source of truth）

```
spec/ni/doc/packet_format.md (你 ONLY edit this)
       │
       │ ni_spec.generator      ← parse MD 產 JSON
       ▼
spec_validate/generated/ni_packet.json (auto-gen，不准手改)
       │
       ├──→ Layer 1 schema 驗 generator 沒產壞 JSON
       ├──→ Layer 2 arithmetic 驗不變量
       └──→ tools/gen_cpp_header.py → include/ni_flit_constants.h → C++
```

對應 SystemRDL / Protocol Buffers 的常見 pattern：人寫 source，工具產衍生品，下游消費衍生品。

## 目錄定位

```
d:\03_CLAUDE\spec\
├── .venv\                          ← Python venv（不在 git，新機器自己建）
└── noc-sim\                        ← git repo
    ├── spec\ni\doc\*.md            ← spec source MD
    └── spec_validate\              ← validator + generator + codegen + samples
        ├── ni_spec\                ← Python module
        ├── generated\              ← auto-gen JSON + schema
        ├── tools\gen_cpp_header.py
        ├── examples\use_constants.cpp
        └── include\ni_flit_constants.h
```

## 一條龍指令（spec 改完一鍵驗證 + build）

```powershell
cd d:\03_CLAUDE\spec\noc-sim\spec_validate
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
& ..\..\.venv\Scripts\python.exe -m ni_spec ..\spec\ni\doc
& ..\..\.venv\Scripts\python.exe tools\gen_cpp_header.py --out include\ni_flit_constants.h
g++ -std=c++17 -I include examples\use_constants.cpp -o use_constants.exe
.\use_constants.exe
```

## 分解步驟

所有指令假設 cwd 是 `d:\03_CLAUDE\spec\noc-sim\spec_validate\`。

### 1. 校驗 spec（generator → Layer 1 → Layer 2）

```powershell
& ..\..\.venv\Scripts\python.exe -m ni_spec ..\spec\ni\doc
```

- 讀 `..\spec\ni\doc\packet_format.md` + `..\spec\ni\doc\signal_interface.md`
- 產 `generated\ni_packet.json` + `generated\ni_signals.json`
- 跑 Layer 1 (JSON Schema) + Layer 2 (semantic/arithmetic) 驗證

### 2. 跑 codegen 產 C++ header

```powershell
& ..\..\.venv\Scripts\python.exe tools\gen_cpp_header.py --out include\ni_flit_constants.h
```

`--spec-dir` 預設為 `spec_validate\generated\`。

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

`gen_cpp_header.py` 只負責「讀 JSON、組字串、印出來」。所有「怎麼從 JSON 撈值」的邏輯都在 `ni_spec.constants` 裡——要加新常數時，先擴充 `ni_spec.constants`，再在 codegen 裡叫新 API。

## 修改 generator 時

`ni_spec/generator.py` 負責 MD → JSON。新加 spec section 或欄位：
1. 寫 parse 函式（參考既有 `parse_header_fields` / `parse_payload_channels` 模式）
2. 在對應的 `generate_ni_*_json` 組裝結構裡接上
3. 若新增區塊需要 schema 驗證，更新 `generated/*.schema.json`
4. 若新增不變量檢查，加進 `ni_spec/invariants.py`
