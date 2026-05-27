# c_model Bootstrap — First-Round Design

> 2026-05-27 · prior context: `spec_validate/docs/plans/2026-05-26-spec-as-code-unified-design.md` §7

## Purpose

本輪不寫完整 c_model，寫的是 **spec validation harness**：用 codegen elaborated 出的常數寫最小 c_model class，逼出 spec 與 codegen 的漏洞，並為 Stage 2 cycle-accurate behavior 鋪 stable 邊界。

## Invariants

1. **Codegen 規範**：top-level pin interface（C++ struct + SV interface）、constants、mode enum、register。**Codegen 不規範**：c_model class shape / method signature — 那是 implementer 設計。
2. **c_model 不 hardcode 規格值** — 一律 reference codegen elaborated symbol。違反視同 drift bug。
3. 描述 codegen 動作用 **elaborate** / **elaborator**，不用 emit。
4. **Primary deliverable = Phase 0 codegen fixes**。Phase 1 兩個 class 是 secondary（驗證 codegen 修完真的可用）。
5. **OSS-first**：source / test 動工前 survey OSS，無 fit 時才手寫，rejection reason documented。

---

## Phase 0 — Codegen Prerequisites (~3 工程日)

| # | 動作 | 工程日 |
|---|---|---|
| 1 | `VC_ARBMode` collision fix：升級 `cpp_blocks` elaborator 加 block prefix（`NMU_*Mode` / `NSU_*Mode`）；validator L2 加 mode-enum-name unique check | 0.5 |
| 2 | Pin-level interface elaborator：升級 `cpp_signals` / `sv_signals` 出 `ni::pins::*Pins` struct + 對偶 SV `interface` + `modport`；對偶 check 進 `--check` mode | 2 |
| 3 | `tools/emit/` → `tools/elaborate/` rename：目錄、import、docs 同步 | 0.5 |

附加：`csr_policy` 從 `ni_registers.json` elaborate 為 C++ const，與項目 2 同 PR 處理（Phase 1 RegisterFile 依賴此）。

## Phase 1 — c_model Class Implementing (~5.5 工程日)

| 項目 | File | Owner | 工程日 |
|---|---|---|---|
| `Flit` | `c_model/include/flit.hpp` (header-only) | claude | 1.5 |
| `RegisterFile` | `c_model/include/register_file.hpp` + `src/register_file.cpp` | claude | 2.5 |
| GTest infra + `test_pins_smoke.cpp` | CMake FetchContent GoogleTest | claude | 1 |
| OSS survey logs (per Invariant 5) | implementing 期間累積 | claude | 0.5 |

### `Flit`

```cpp
namespace ni::cmodel {

class Flit {
public:
  static constexpr int WIDTH_BITS  = ni::FLIT_WIDTH;
  static constexpr int WIDTH_BYTES = (WIDTH_BITS + 7) / 8;

  Flit();
  explicit Flit(const std::array<uint8_t, WIDTH_BYTES>& raw);

  void     set_header_field(std::string_view name, uint64_t value);
  uint64_t get_header_field(std::string_view name) const;
  void                  set_payload_channel(std::string_view ch, std::vector<uint8_t> data);
  std::vector<uint8_t>  get_payload_channel(std::string_view ch) const;

  const std::array<uint8_t, WIDTH_BYTES>& raw() const noexcept;
  bool check_padding_is_zero() const;

private:
  std::array<uint8_t, WIDTH_BYTES> raw_{};
};

}
```

Field accessor 第一輪用 `string_view`；未來 codegen elaborate `HeaderField` enum 後改 type-safe。

### `RegisterFile`

```cpp
namespace ni::cmodel {

enum class AbiResult { Ok, DecErr, SlvErr };
struct AbiResponse { AbiResult status; uint32_t data; };

class RegisterFile {
public:
  RegisterFile();   // ctor 從 codegen csr_policy 常數初始化

  AbiResponse read32(uint32_t offset);
  AbiResponse write32(uint32_t offset, uint32_t value, uint8_t wstrb = 0b1111);

  uint32_t read_field(uint32_t offset, uint32_t mask) const;
  void     write_field(uint32_t offset, uint32_t mask, uint32_t value);

  void reset();
  bool last_write_triggered_irq() const;
  bool last_write_cleared_rw1c_field() const;

private:
  std::unordered_map<uint32_t, uint32_t> storage_;
};

}
```

兩條 access path：`read32/write32` 走 ABI policy（給未來 AXI4-Lite 入口用）；`read_field/write_field` 給 c_model 內部 feature unit 直接用（不走 policy）。

### Codegen elaborated pin bundles

由 Phase 0 §2 自動產出，第一輪 c_model 不手寫：

```cpp
namespace ni::pins {
  struct AxiSlavePins { /* per ni_signals.json */ void reset_outputs(); };
  struct CsrPins      { /* ... */ };
  struct NocReqPins   { /* ... */ };
  struct NocRspPins   { /* ... */ };
}
```

C++ struct 與 SV `interface` 同源、同 pin name / direction / width，由 `--check` mode 強制 paired。

---

## Data Flow

第一輪每個 class 都是 standalone test target，**無 inter-class call**。GTest harness ↔ class point-to-point。Stage 2 預期 Layer B feature unit 透過 `ni::pins::*` + `Flit` + `RegisterFile` 完成行為 —— 本輪 interface 必須容得下，但不寫 feature unit。

## Error Handling

- **`Flit`**：超出 field width 採 silent truncate（對齊 RTL），debug build 加 assert
- **`RegisterFile`**：ABI policy 從 codegen `csr_policy` const 取，不 hardcode：

| 條件 | spec 配置選項 |
|---|---|
| `unmapped_read` | `decerr` / `zero` |
| `misaligned` | `decerr` / `lower-aligned` |
| `sub_word_write` | `decerr` / `ignored` |
| `wo_read` | `zero` / `decerr` |

- **Codegen-side**：validator L2 加 `mode enum name unique` + `C++/SV interface paired` 兩條 invariant

## Testing

三層（從 c_model 動作前到動作後）：

| 層 | 工具 | 跑在 | 驗什麼 |
|---|---|---|---|
| L0 drift gate | `tools/codegen.py --check` | spec_validate | committed artifact ≡ current SSoT |
| L1 codegen structural | pytest | spec_validate | `ni::pins::*` C++ field name ≡ SV signal name；ni_signals.json 全 pin 進 struct |
| L2 c_model behavior | GoogleTest | c_model | `test_flit.cpp` (~7) / `test_register_file.cpp` (~11) / `test_pins_smoke.cpp` (~2) |

Build：CMake + FetchContent GoogleTest。`c_model_tests` 依賴 `--check` 跑過 → drift 直接 build fail。

**不做的 test**：AXI4 protocol compliance / outstanding-OoO behavior / cross-NMU-NSU / performance / random。

---

## Out of Scope

**Layer B**（下輪）：ROB、AddrTrans、VcMapping、VcArb、MetaBuffer、RBuffer、VcDemux、AXISlavePort handshake。

**Layer C**（optional, defer）：ECC、AxiParity、Probe、ErrorMon、Quiesce、ExclusiveMon、CDC、Csr handshake。

**永不做**：Cycle-accurate behavior 本身（留 Stage 2）、Router c_model（router spec 完成後另一輪）、NSU class、DPI-C bridge、co-sim。

**設計中考慮但廢除的 class**：

| Class | 廢除理由 |
|---|---|
| `NIPort` | 純 pin 集合 + trivial method，由 `ni::pins::*` 完全替代 |
| `AxiSlavePort` | outstanding/OoO 屬 AXI4 protocol + ROB feature，本輪剩 0 件實質工作 |
| `CsrPort` | handshake 是 Stage 2 的事，ABI policy 已併入 `RegisterFile` |
| `AxiTransaction` | internal unit 直接 reference `ni::pins::*` 即可 |

---

## Sufficiency Findings Policy

Implementing 期間發現的 spec / codegen 漏洞累積至 `c_model/SUFFICIENCY_FINDINGS.md`，**不寫進本 spec doc**。第一輪結束後另開 brainstorming session 逐條 disposition（修 codegen / 補 spec / accept as known limitation）。

## Next

- task #7 user review 本 doc
- task #8 invoke `writing-plans` skill 產 implementation plan（拆 Phase 0 + Phase 1 為可執行 task）
