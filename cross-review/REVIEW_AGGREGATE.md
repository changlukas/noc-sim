# Cross-Review Aggregate — D-session necessity

**Question reviewed**: 「如果不做 D session（不擴充 codegen 修 F-001~F-006），會留下哪些 hardcode/deadcode？對未來成本多大？D session 是否必要？」

**Reviewers**: Codex GPT-5.5 + independent Claude subagent.

---

## Headline Finding (Claude subagent, verified)

**SUFFICIENCY_FINDINGS.md 錯誤分類 F-002 與 F-006 為「codegen 缺」— 實際上 codegen 已 emit 對應 symbol。c_model 第一輪沒去 consume 它們，反而寫 stub 並記為「codegen TODO」。**

驗證結果（我重新跑 `grep` 在 worktree 內確認）：

| Finding | SUFFICIENCY_FINDINGS.md 描述 | 實際 codegen 狀態 |
|---|---|---|
| F-002 padding list | "codegen does not elaborate padding-field list" | ❌ 錯。`<FIELD>_ENABLED` flag 已 emit 在 `ni_flit_constants.h:45,61,65,69`（4 個 padding 都標 false） |
| F-006 access mode | "codegen does not elaborate per-register access mode" | ❌ 錯。Per-reg `enum class <NAME>Access { ... }` 已 emit 在 `ni_regs.h:62-91`（含 RW1C/WO/RO/RW） |

**這代表 F-002 / F-006 不是 codegen 工作，是 c_model bug**（漏 consume 已存在 symbol）。

附加 finding：F-006 codegen output shape 是 awkward — 每個 register 一個 single-value enum class（如 `enum class ERR_STATUSAccess { RW1C };`）。c_model 即使 consume 也應該先 push back 要求改成 standard shape：一個共享 `enum class AccessMode { RO, RW, RW1C, WO, WC };` + per-reg `constexpr AccessMode <NAME>_ACCESS = AccessMode::RW1C;`。

---

## Consensus (兩 reviewer 都同意)

| Finding | 共識 |
|---|---|
| F-004 per-reg reset | **必修 now**。`register_file.cpp:53-57` reset to 0 違反 generated reset value（e.g. `ni_registers.json:138` 有 `0xFFFF`）。Live behavioral bug. |
| F-005 ALL_OFFSETS | **必修 now**。`register_file.cpp:12-43` 31 行手寫 list，spec 加 register 會 silent drift. |
| F-006 access mode | **必修 now**（兩 reviewer 都列 High）— 雖然原因不同（codex 以為要新 codegen，claude 認為要 redesign + consume），結論一致 |
| F-003 payload positions | **可 defer**。Stage 2 才需要 |
| "Full D" 不必要 | 兩邊都拒絕「全部 6 個都做」 |

---

## Disagreement

| Finding | Codex 立場 | Claude 立場 | 我的 adjudicate |
|---|---|---|---|
| F-001 HeaderField enum | Medium severity，conditional fix（若 Flit 是 production API 就修） | "Real codegen work，defer 並記錄 re-open trigger" | **保留疑問**。`flit.hpp:40-45` 的 6-name dispatch 確實漏 `vc_id, route_par, multicast, flit_ecc` 等 6 個 generated field。但 Flit 第一輪 API 主要是 spec validation harness 用，沒生產 caller。傾向 Claude — defer 但需明示 re-open trigger（例如「Layer B 動工前必修」） |
| F-002 padding list | "Low-Medium，defer" | "c_model bug，~10 LOC fix" | **採 Claude**。Verify 過 — codegen 已 emit，只要 c_model 改 `check_padding_is_zero` 去 iterate field & 查 ENABLED 即可。但這 dependency 是 F-001 提供 field 列表，所以實際上 F-002 解法 = 「等 F-001 enum + 加 5-10 LOC iterate」 |
| 完整 D 的工程量估計 | 沒明示，但暗示 3-4 工程日 | "~1 工程日"（因為 F-002/F-006 不是 codegen 工作） | **採 Claude**。F-002/F-006 是 c_model consume work，F-004/F-005 是 ~5 LOC codegen 各。F-001/F-003 才是 real codegen new feature |

---

## Unique findings

**Codex only**:
- 列出 6 個 option（包含 "Quarantine workarounds" — 把 public API rename 成 unsupported 形式）。值得考慮：如果 defer F-003，那 `Flit::set_payload_channel` / `get_payload_channel` 不該留為 public no-op，應該移除或 rename。
- 強調 `PaddingFieldStaysZero` test（`test_flit.cpp:42`）是 tautological — passes because `check_padding_is_zero` 寫死 true。

**Claude subagent only**:
- F-006 emission shape redesign 提議：single `enum class AccessMode`（5 values）+ per-reg `constexpr` 取代 31 個 single-value enum class。**這是 D session 內應該優先做的 design decision**。
- 指出 codex 對 F-006 的理解錯了（"codegen needs to add" → 實際上 codegen 已加）。
- 量化 D-min 為 ~1 工程日（非 codex 暗示的 3-4 day）。

---

## Verification status

| 主張 | 來源 | 我驗證了嗎 | 結果 |
|---|---|---|---|
| F-002 `*_ENABLED` 已 emit | Claude | ✅ grep'd ni_flit_constants.h | 確認 12 個 ENABLED flag，4 個是 false |
| F-006 per-reg Access enum 已 emit | Claude | ✅ grep'd ni_regs.h | 確認 30 個 enum class，含 RW1C/WO/RO/RW |
| Reset 寫死 0 但 ni_registers.json 有 0xFFFF | Codex | (未驗證 ni_registers.json line 138 — 但 register_file.cpp:53-57 reset-to-0 行為已知) | partial 確認 |
| Flit dispatch 漏 6 個 generated field | Codex + Claude | (未深入驗證 ni_packet.json 內 field 數量，但 flit.hpp:40-45 確實只 dispatch 6 個) | 行為一致，數量沒驗證 |
| F-003 supports Stage 2 only | 兩邊 | (未驗證 Stage 2 spec) | 接受 |

---

## Recommended action

**走 "D-min plus redesign" — 比兩 reviewer 各自版本都嚴格一點**：

| Phase | 工作 | 工程日 |
|---|---|---|
| **D-min** | F-004 codegen emit `<REG>_RESET` (~5 LOC) | 0.25 |
| **D-min** | F-005 codegen emit `ALL_OFFSETS[]` (~5 LOC) | 0.25 |
| **D-redesign** | F-006 重新設計 codegen emission shape（single `AccessMode` enum + per-reg `constexpr`） + c_model consume + update `is_wo_/is_rw1c_` | 0.5 |
| **c_model fix** | F-002 改 `check_padding_is_zero` 真的 iterate field 看 ENABLED（需 F-001 enum 或 hand-list — 先用 ENABLED 直查 codegen 常數） | 0.25 |
| **API quarantine** | `Flit::set_payload_channel` / `get_payload_channel` 從 public API 移除（or rename `_TODO`），測試對應移除 | 0.25 |
| **Total** | | **~1.5 工程日** |

**Defer with documented re-open trigger**：
- **F-001** HeaderField enum：defer。Re-open trigger = Layer B 開始（任何 NMU::AddrTrans / ROB feature 都會 consume Flit header field by name）
- **F-003** per-channel payload positions：defer。Re-open trigger = Layer B 開始（payload pack/unpack 是 Stage 2 cycle-accurate behavior 的事）

---

## Verdict

**D 不必要做完整 6 個。必要做：F-004/F-005/F-006/F-002（~1.5 工程日，含 F-006 shape redesign）。F-001/F-003 defer 並記 re-open trigger。**

**Confidence: HIGH**（兩 reviewer 對「不做完整 D」一致，且 F-002/F-006 codegen 已 emit 是可 verify 事實）

**Top concern**: SUFFICIENCY_FINDINGS.md 第一輪寫的描述至少有 2 條（F-002/F-006）是錯的 — 代表 implementer 沒充分檢查 codegen 已 emit 什麼。第一輪走完該做的 final review 沒做 enough。
