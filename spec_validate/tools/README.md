# Spec-as-Code Codegen — `tools/codegen.py`

統一 codegen 工具：從 `generated/*.json` 與 `ni_function_blocks.json` 產出 C++ headers (`include/*.h`) 與 SystemVerilog packages (`rtl_pkg/*_pkg.sv`)，供 C model 與 RTL 共用同一份常數定義。

## 系統 dataflow

```
┌──────────────────────────────────────────────────────────────────┐
│  Source layer (human-edited)                                      │
│  - spec/ni/doc/*.md                                                │
│  - spec_validate/ni_function_blocks.json (hand-written)            │
└────────────────────────┬─────────────────────────────────────────┘
                         │ ni_spec.generator (Python; parse MD → JSON)
                         ▼
┌──────────────────────────────────────────────────────────────────┐
│  generated/*.json  ─  Single Source of Truth (do not hand-edit)   │
│  - ni_packet.json                                                  │
│  - ni_signals.json                                                 │
│  - ni_registers.json                                               │
│  - ni_protocol_rule_index.json                                     │
│  (plus ni_function_blocks.json above, lives outside generated/)    │
└────────────────────────┬─────────────────────────────────────────┘
                         │ ni_spec.constants (stable API; firewall)
                         ▼
┌──────────────────────────────────────────────────────────────────┐
│  tools/emit/cpp_*.py + tools/emit/sv_*.py  (4 + 4 emitters)        │
│  → each returns a string of C++ or SystemVerilog code              │
└────────────────────────┬─────────────────────────────────────────┘
                         │ tools/emit/common.py (provenance banner)
                         ▼
┌──────────────────────────────────────────────────────────────────┐
│  tools/codegen.py  (dispatcher)                                     │
│  → routes --target / --domain to right emitter                      │
│  → adds provenance banner                                           │
│  → writes file OR --check mode (diff vs committed; exit 0/1)        │
└────────────────────────┬─────────────────────────────────────────┘
                         │
            ┌────────────┴────────────┐
            ▼                         ▼
┌──────────────────────┐    ┌──────────────────────┐
│  include/*.h         │    │  rtl_pkg/*_pkg.sv     │
│  (C++ consumers)     │    │  (SV consumers)       │
│                      │    │                      │
│  4 files:            │    │  4 files:            │
│  - ni_flit_constants │    │  - ni_flit_pkg.sv    │
│  - ni_signals.h      │    │  - ni_signals_pkg.sv │
│  - ni_regs.h         │    │  - ni_regs_pkg.sv    │
│  - ni_blocks.h       │    │  - ni_blocks_pkg.sv  │
└──────────────────────┘    └──────────────────────┘
        │                            │
        ▼                            ▼
   C++ C model                  SystemVerilog RTL
   (examples/use_constants.cpp;  (DUT — future)
    future c_model/)
```

**核心理念**：source MD 為 single source of truth；generated JSON 是中間 artifact；codegen 透過 `ni_spec.constants` API（stable interface）把同樣常數投影到兩種語言。C++ 跟 SV 看到的命名一致、值一致 — 不靠人對齊。

## CLI usage — `tools/codegen.py`

### 產出一個 header / SV package

```
py -3 tools/codegen.py --target {cpp|sv} --domain {packet|signals|registers|blocks} [--out PATH]
```

`--out` 預設：
- `--target cpp`：`spec_validate/include/<auto-name>.h`
- `--target sv`：`spec_validate/rtl_pkg/<auto-name>_pkg.sv`

範例：
```bash
py -3 tools/codegen.py --target cpp --domain packet     # → include/ni_flit_constants.h
py -3 tools/codegen.py --target cpp --domain signals    # → include/ni_signals.h
py -3 tools/codegen.py --target cpp --domain registers  # → include/ni_regs.h
py -3 tools/codegen.py --target cpp --domain blocks     # → include/ni_blocks.h
py -3 tools/codegen.py --target sv  --domain packet     # → rtl_pkg/ni_flit_pkg.sv
py -3 tools/codegen.py --target sv  --domain signals    # → rtl_pkg/ni_signals_pkg.sv
py -3 tools/codegen.py --target sv  --domain registers  # → rtl_pkg/ni_regs_pkg.sv
py -3 tools/codegen.py --target sv  --domain blocks     # → rtl_pkg/ni_blocks_pkg.sv
```

### `--check` mode — drift detection (CI / pre-commit)

```bash
py -3 tools/codegen.py --check
```

Regen 全部 8 個 output 到 temp dir、跟 committed 版本 diff（timestamp 那行剔除）。任何 drift → exit 1。CI / pre-commit hook 用這條。

### `--lint-sv` mode — verilator lint smoke test

```bash
py -3 tools/codegen.py --lint-sv
```

若 verilator 在 PATH 中：跑 `verilator --lint-only --Wall rtl_pkg/*.sv`、報結果。
若不在：print `[skip] verilator not in PATH` 然後 exit 0（graceful skip，不擋 CI）。

## 每個 Python file 的角色

### Dispatcher：`tools/codegen.py` (140 LOC)

CLI 統一入口、不寫 emit 邏輯、只做 routing。讀 `--target` / `--domain` → 找對應 emitter → 呼叫 `emit()` → prepend banner → 寫檔 OR diff。`--check` / `--lint-sv` 兩個 sub-command 也在這裡。

### Per-domain emitters：`tools/emit/cpp_*.py` 與 `sv_*.py`（8 個檔）

每個 emitter 做一件事：讀對應 source（透過 `ni_spec.constants` API，**不直接 parse JSON**）→ return code string。Emitter 自己不知道輸出 path、不寫檔。

| 檔案 | 讀什麼 | 產出 |
|---|---|---|
| `cpp_packet.py` (60 LOC) | `generated/ni_packet.json` | `namespace ni::header::*` (bit positions) + `ni::payload::*` (channel widths) + `ni::width::*` (resolved widths) + 算術 `static_assert` |
| `cpp_signals.py` (58 LOC) | `generated/ni_signals.json` | `namespace ni::signals::*` — output signals 的 reset 初值 const（external_driven inputs skip） |
| `cpp_registers.py` (80 LOC) | `generated/ni_registers.json` | `namespace ni::regs::*` — offset const + field bit masks + per-register access enum + field width sum `static_assert` |
| `cpp_blocks.py` (94 LOC) | `ni_function_blocks.json` (手寫 source) | `namespace ni::blocks::*` — `enum class FunctionBlock` + per-feature mode enums + `constexpr` for compile_time_params |
| `sv_packet.py` (~70 LOC) | 同 cpp_packet | `package ni_flit_pkg` + `localparam int unsigned` 對應 cpp_packet 的 const |
| `sv_signals.py` (~30 LOC) | 同 cpp_signals | `package ni_signals_pkg` + `localparam int unsigned` reset constants |
| `sv_registers.py` (~60 LOC) | 同 cpp_registers | `package ni_regs_pkg` + `localparam int unsigned` offsets + masks |
| `sv_blocks.py` (~70 LOC) | 同 cpp_blocks | `package ni_blocks_pkg` + `typedef enum logic [N-1:0]` for FunctionBlock + mode enums + `localparam int unsigned` params |

**SV typing 注意**：integer constants 用 `localparam int unsigned`（不用 bare `parameter` — 會 silent truncate / signed-unsigned bug）。Enums 用 `typedef enum logic [N-1:0] { ... } foo_e;`（不用 bare parameter list — 等同 C++ `enum class` 的型別安全）。`N = $clog2(member_count)` 由 emitter 算出來填。

### 共用 helper：`tools/emit/common.py` (46 LOC)

只負責產 provenance banner。每個 .h / .sv 開頭都會有這 5 行：

```cpp
// AUTO-GENERATED by tools/codegen.py — DO NOT EDIT
// Source: noc-sim/spec_validate/generated/ni_packet.json
// Source SHA: 3a8f1e2d9b4c                              ← 第一 12 位 hex of SHA-256
// Generator version: v1.0.0
// Generated at: 2026-05-27T10:54:51Z
```

Debug 時可知道哪份檔案從哪個 JSON、什麼 hash、什麼時候生的。`--check` 比對時故意跳過 `Generated at:` 那行（每次 regen 都會變）、只比其他 4 行 + 內容 body。

### 套件 marker：`tools/emit/__init__.py` (empty)

讓 `from tools.emit import cpp_packet` 能 work、無實際內容。

### 過渡 wrapper：`tools/gen_cpp_header.py` (deprecated)

舊 entry point。內容被改成：印 deprecation warning 到 stderr，forward 到 `codegen.py --target cpp --domain packet`。下個 minor version 才會真刪。

避免直接刪檔讓既有 docs / scripts / muscle memory 立刻爆。**新 code 不要用、用 `codegen.py`**。

## Output 範例

### C++（`include/ni_flit_constants.h` 截錄）

```cpp
// AUTO-GENERATED by tools/codegen.py — DO NOT EDIT
// Source: noc-sim/spec_validate/generated/ni_packet.json
// Source SHA: 3a8f1e2d9b4c
// Generator version: v1.0.0
// Generated at: 2026-05-27T10:54:51Z

#pragma once
#include <cstdint>

namespace ni {
namespace header {
    constexpr int AXI_CH_LSB = 0;
    constexpr int AXI_CH_MSB = 2;
    constexpr int ROB_IDX_LSB = 23;
    constexpr int ROB_IDX_MSB = 29;
    // ... 12 fields
}
namespace payload {
    constexpr int AW_WIDTH = 350;
    constexpr int AR_WIDTH = 350;
    // ...
}

// Arithmetic invariants enforced at compile time
static_assert(HEADER_WIDTH + PAYLOAD_WIDTH == FLIT_WIDTH,
              "Flit width arithmetic inconsistent");
}
```

### SystemVerilog（`rtl_pkg/ni_blocks_pkg.sv` 截錄）

```systemverilog
// AUTO-GENERATED by tools/codegen.py — DO NOT EDIT
// Source: noc-sim/spec_validate/ni_function_blocks.json
// Source SHA: 9c2f7b1d4e8a
// Generator version: v1.0.0
// Generated at: 2026-05-27T11:10:33Z

`ifndef NI_BLOCKS_PKG_SVH
`define NI_BLOCKS_PKG_SVH

package ni_blocks_pkg;

  typedef enum logic [3:0] {
    FUNCTION_BLOCK_ROB         = 4'd0,
    FUNCTION_BLOCK_QOS         = 4'd1,
    FUNCTION_BLOCK_ECC         = 4'd2,
    // ...
  } function_block_e;

  typedef enum logic [1:0] {
    ROB_MODE_NORMAL = 2'd0,
    ROB_MODE_SIMPLE = 2'd1,
    ROB_MODE_NOROB  = 2'd2
  } rob_mode_e;

  localparam int unsigned ROB_DEPTH       = 32;
  localparam int unsigned ROB_ENTRY_WIDTH = 64;
  // ... 22 compile-time params

endpackage

`endif // NI_BLOCKS_PKG_SVH
```

C++ 跟 SV 命名對應規則（per design doc §6.2）：

| 概念 | C++ | SystemVerilog |
|---|---|---|
| 整數常數 | `constexpr int ni::header::ROB_IDX_LSB` | `localparam int unsigned ni_flit_pkg::ROB_IDX_LSB` |
| enum class | `enum class ROBMode { Normal, Simple, NoRoB }` | `typedef enum logic [1:0] { ROB_MODE_NORMAL, ROB_MODE_SIMPLE, ROB_MODE_NOROB } rob_mode_e` |
| Compile-time param | `constexpr int ni::blocks::ROB_DEPTH = 32` | `localparam int unsigned ni_blocks_pkg::ROB_DEPTH = 32` |

## `static_assert` — compile-time invariant 檢查

C++ emitter 在 `cpp_packet.py` 跟 `cpp_registers.py` 嵌入算術 `static_assert`、把 §6.4 規定的 arithmetic equality invariants 鎖在編譯時：

- **flit width 算術**：`HEADER_WIDTH + PAYLOAD_WIDTH == FLIT_WIDTH`
- **per-channel 算術**：每 channel 的 payload + header 對得起來
- **SECDED bound**：`2^parity_bits >= data_bits + parity_bits + 1`
- **per-register field sum**：每個 register 的 fields width 加總 ≤ data_width

Spec 改了、JSON 重新 gen、static_assert 自動更新。若有人改 generated header 想 bypass、編譯就會擋。但 **`static_assert` 不擋 drift**（手改 .h 後沒重 gen 也能編譯通過）— drift detection 要靠 `--check` mode。

## 一條龍指令

```bash
cd noc-sim/spec_validate

# 1. Spec validation (10 layers)
py -3 -m ni_spec ../spec/ni/doc

# 2. Codegen all 8 outputs
for target in cpp sv; do
  for domain in packet signals registers blocks; do
    py -3 tools/codegen.py --target $target --domain $domain
  done
done

# 3. Drift check
py -3 tools/codegen.py --check

# 4. SV lint (graceful skip if verilator not installed)
py -3 tools/codegen.py --lint-sv

# 5. C++ sample compile + run
g++ -std=c++17 -I include examples/use_constants.cpp -o use_constants.exe
./use_constants.exe   # expect: header[63:0] = 0x00000000F80902AA
```

PowerShell 版本（Windows native）：
```powershell
cd noc-sim\spec_validate
py -3 -m ni_spec ..\spec\ni\doc
py -3 tools\codegen.py --target cpp --domain packet
py -3 tools\codegen.py --target cpp --domain signals
py -3 tools\codegen.py --target cpp --domain registers
py -3 tools\codegen.py --target cpp --domain blocks
py -3 tools\codegen.py --target sv  --domain packet
py -3 tools\codegen.py --target sv  --domain signals
py -3 tools\codegen.py --target sv  --domain registers
py -3 tools\codegen.py --target sv  --domain blocks
py -3 tools\codegen.py --check
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"
g++ -std=c++17 -I include examples\use_constants.cpp -o use_constants.exe
.\use_constants.exe
```

## 怎麼修 spec / 加新 domain

### 改 spec 內容（packet field、signal、register、function block）

只改對應 `spec/ni/doc/*.md`（或 `ni_function_blocks.json`）→ 重跑一條龍指令、所有衍生品自動跟著刷新。

**絕對不要手改**：
- `generated/ni_*.json` ← 下次跑 generator 會洗掉
- `include/*.h` 或 `rtl_pkg/*.sv` ← 下次跑 codegen 會洗掉
- `generated/ni_*.schema.json` ← hand-maintained 但不該頻繁改、改前先確認 schema 與 data 還對得起來

### 加新 domain

例如要加 `interrupts` domain：
1. **Source**：新增 `spec/ni/doc/interrupts.md` + parse 函式 in `ni_spec/generator.py`
2. **Schema**：新增 `generated/ni_interrupts.schema.json` + Layer 1 / Layer 2 validators in `ni_spec/invariants.py`
3. **Constants API**：在 `ni_spec/constants.py` 加 `interrupts_*()` accessor 函式（stable API、firewall against schema changes）
4. **Emitters**：加 `tools/emit/cpp_interrupts.py` + `tools/emit/sv_interrupts.py`、都消費 `ni_spec.constants.interrupts_*`
5. **Dispatcher**：在 `tools/codegen.py` 的 domain map 加 `interrupts` entry
6. **Tests**：加對應 tests in `tests/test_*.py`

### 加新 emit target（例如 Python C model）

照同樣 pattern 在 `tools/emit/` 加 `py_*.py` 系列、`codegen.py` 加 `--target py`。

## 注意事項

### `include/` 跟 `rtl_pkg/` 是 git-ignored

避免 commit 衝突跟 stale artifacts。Codegen output 隨時可 regen — clone 後跑一條龍指令就有了。

例外：`include/ni_flit_constants.h` 之前因歷史原因被 tracked、`-f` 加進 git。新加的 `.h` 跟 `.sv` 都不 track。

### `--check` 跟 `--lint-sv` 是 advisory tools

CI 不強制（per `2026-05-26-spec-as-code-unified-design.md` §4.3）— 但 release 前建議手動跑一輪。

### `gen_cpp_header.py` 是 deprecated

新 code 不要 invoke。下個 minor version 會刪 wrapper。

## 修改 codegen 時

`tools/codegen.py` 是 dispatcher、emit 邏輯在 `tools/emit/{cpp,sv}_*.py`。所有「怎麼從 JSON 撈值」邏輯在 `ni_spec.constants`（stable API）—— 要加新常數時：

1. 先擴 `ni_spec.constants.py`（加 accessor function）
2. 再在 emitter 呼叫新 API
3. **不要**在 emitter 內直接 parse JSON — 那會繞過 firewall、之後 schema 改了 emitter 也會壞

## 修改 generator 時

`ni_spec/generator.py` 負責 MD → JSON。新加 spec section 或欄位：
1. 寫 parse 函式（參考既有 `parse_header_fields` / `parse_payload_channels` 模式）
2. 在對應的 `generate_ni_*_json` 組裝結構裡接上
3. 若新增區塊需要 schema 驗證，更新 `generated/*.schema.json`
4. 若新增不變量檢查，加進 `ni_spec/invariants.py`
5. 若新增 JSON 欄位要供 codegen 用，**先加 `ni_spec.constants` accessor**，再在 emitter 呼叫
