# Pure AXI Subsystem — Phase C Design (Exclusive Access)

**Status**: Draft (2026-05-31)
**Scope**: Single-Stage extension to Phase A + B Pure AXI subsystem. Adds AXI4 exclusive access (AxLOCK + EXOKAY) per IHI 0022 §A7.
**Branch**: feature branch off master (149/149 sequential ctest baseline)
**Stage count**: 1 stage, ~3 commits, ~32 new tests → ~181 ctest target

---

## 1 Goal

Extend the c_model Pure AXI subsystem so the slave correctly models AXI4 exclusive access semantics. After Phase C:

- `ScenarioTransaction` accepts an optional `lock: exclusive | normal` field
- `AxiMaster` propagates lock to `AwBeat`/`ArBeat` wire signals (no master state)
- `AxiSlave` implements per-ID exclusive monitor: tag set by exclusive AR (after RLAST), invalidated by overlapping normal write, consumed by exclusive AW
- Successful exclusive write returns `EXOKAY` and commits to memory; failed exclusive write returns `OKAY` and does **not** commit (per IHI 0022 §A7.2.2)
- Memory `DECERR`/`SLVERR` overrides any EXOKAY/OKAY (response priority per IHI 0022 §A7.2.4)

Out of scope (deferred to a future phase):
- Multi-master simulation
- AXI3 LOCKED bit (AXI4 deprecates LOCKED, retains only EXCLUSIVE bit)
- Atomic operations (AXI5 ATOP)

---

## 2 Architecture

```
YAML scenario_txn (lock: exclusive)
    │
    ▼
ScenarioParser → ScenarioTransaction.lock = Exclusive
    │
    ▼
AxiMaster (pure wire-through, no monitor state)
    │   aw.lock = (txn.lock == Exclusive) ? 1 : 0
    │   ar.lock = (txn.lock == Exclusive) ? 1 : 0
    ▼
AxiSlave (per-ID exclusive monitor)
    ├─ AR lock=1 → tag = {addr range, len, size, burst, cache, prot, ready=false}
    ├─ R RLAST  → tag.ready = true
    ├─ AW lock=0 → for-each tag overlap with normal write range → erase
    ├─ AW lock=1 → match check (all attrs + ready) → erase tag → set burst-state flags
    └─ W submit → if (is_exclusive && !match) skip memory push; else push normally
    │
    ▼
BBeat.resp ← priority: memory_resp (if DECERR/SLVERR) > EXOKAY (if match) > OKAY
    │
    ▼
AxiMaster B drain → WriteResult.{resp, lock} → scoreboard
    │
    ▼
Scoreboard: skip expected_ update if (resp ∈ {DECERR, SLVERR}) OR (lock==Exclusive && resp==OKAY)
```

Hot-swap boundary unchanged (NoC flit link). Exclusive logic fully encapsulated in AxiSlave + Memory pair.

---

## 3 Data model

### types.hpp
```cpp
enum class LockType : uint8_t { Normal = 0, Exclusive = 1 };
```

AwBeat/ArBeat keep existing `uint8_t lock` field (wire fidelity). Canonical raw→enum conversion happens at AxiSlave admit step.

### scenario_parser.hpp
```cpp
struct ScenarioTransaction {
  // ... existing fields ...
  LockType lock = LockType::Normal;   // optional YAML, default Normal
};
```
YAML parsing: `"normal"` → Normal, `"exclusive"` → Exclusive, missing → default Normal, other → throw.

### axi_master.hpp
```cpp
struct WriteResult {
  // ... existing fields ...
  LockType lock = LockType::Normal;   // mirrors txn.lock; scoreboard uses to detect failed exclusive
};
```
ReadResult unchanged (lock not needed on read side).

### axi_slave.hpp
```cpp
struct ExclusiveTag {
  uint64_t addr_start, addr_end;  // half-open; WRAP uses [wrap_lower, wrap_upper)
  uint8_t  len, size;
  Burst    burst;
  uint8_t  cache, prot;
  bool     ready;                  // false until paired RLAST observed
};
std::map<uint8_t, ExclusiveTag> exclusive_tags_;

// WriteBurstState additions (existing struct from Phase B-5a)
bool is_exclusive    = false;
bool exclusive_match = false;
```

### protocol_rules.hpp — 6 new stateless helpers + 1 monitor helper

```cpp
// Stateless (asserted at AW/AR admit step)
inline bool check_lock_encoding(uint8_t raw);
  // raw ∈ {0, 1}

inline bool check_exclusive_total_bytes_le_max(LockType lock, uint8_t len, uint8_t size);
  // §A7.2.4: (len+1) * (1<<size) ≤ 128

inline bool check_exclusive_total_beats_le_max(LockType lock, uint8_t len);
  // §A7.2.4: total beats ≤ 16 → len ≤ 15

inline bool check_exclusive_total_pow2(LockType lock, uint8_t len);
  // §A7.2.4: (len+1) is power of 2 → len ∈ {0,1,3,7,15}

inline bool check_exclusive_addr_aligned_to_total(LockType lock, uint64_t addr, uint8_t len, uint8_t size);
  // §A7.2.4: addr aligned to total burst bytes

inline bool check_exclusive_burst_not_fixed(LockType lock, Burst burst);
  // §A7.2.4: FIXED burst cannot be exclusive

// Monitor (called by AxiSlave at AW lock=1 admit)
inline bool check_exclusive_write_matches_read_tag(const ExclusiveTag& tag, const AwBeat& aw);
  // All of: addr_start, addr_end, len, size, burst, cache, prot match + tag.ready==true
```

### scoreboard.hpp
```cpp
void handle_write_completed(const WriteResult& wr, ...) {
  if (wr.resp == Resp::DECERR || wr.resp == Resp::SLVERR) return;
  if (wr.lock == LockType::Exclusive && wr.resp == Resp::OKAY) return;  // failed exclusive
  // commit to expected_
}
```

---

## 4 AxiSlave monitor state machine

### Per-tick event order (fixed by Phase B AxiSlave::tick() steps)
1. **B drain** (E6)
2. **R drain** (E5)
3. **AR admit** (E1)
4. **AW admit** (E2 or E3)
5. **W submit** (E4)
6. forward to memory_port

This order ensures a normal AW in the same tick as an exclusive AR does not clear a tag that was just set (E1 happens after E2).

### 6 events

| Event | Trigger | Effect |
|---|---|---|
| **E1** | AR (lock=1) admit | run 5 stateless rules; `tag = {compute_range, len, size, burst, cache, prot, ready=false}`; `exclusive_tags_[ar.id] = tag` (overwrite if present); forward AR normally |
| **E2** | AW (lock=0) admit | compute normal write range; iterate `exclusive_tags_` with iterator-safe `it = erase(it)`; erase tags whose `[addr_start, addr_end)` overlaps normal write range |
| **E3** | AW (lock=1) admit | run 6 stateless rules; look up `exclusive_tags_[aw.id]`; compute `match = check_exclusive_write_matches_read_tag(tag, aw)`; set burst-state `is_exclusive=true, exclusive_match=match`; erase tag from map (regardless of match) |
| **E4** | W submit (per beat) | if `is_exclusive && !exclusive_match` → skip `memory_port.push_write` for this beat; else push normally |
| **E5** | R drain (per RLAST) | if R belongs to outstanding exclusive AR → `exclusive_tags_[ar.id].ready = true` |
| **E6** | B drain | resp priority: `memory_resp ∈ {DECERR, SLVERR} → memory_resp`; else if `is_exclusive` → `exclusive_match ? EXOKAY : OKAY`; else `memory_resp` (OKAY) |

### Tag range computation (INCR vs WRAP)
```cpp
static std::pair<uint64_t,uint64_t> compute_tag_range(const ArBeat& ar) {
  std::size_t bpb = 1ull << ar.size;
  std::size_t total = (ar.len + 1) * bpb;
  if (ar.burst == Burst::WRAP) {
    uint64_t wrap_lower = ar.addr & ~(static_cast<uint64_t>(total) - 1);
    return {wrap_lower, wrap_lower + total};
  }
  return {ar.addr, ar.addr + total};  // INCR; FIXED rejected by stateless rule
}
```

### Documented invariants
- Monitor granularity = byte-level half-open overlap (`a < tag.end && tag.start < b`); minimal model per IHI 0022 §A7.2.3 (implementation-defined)
- Same-ID consecutive exclusive ARs: second silently overwrites first (per §A7.2.1)
- Mismatched exclusive write: silent OKAY + memory suppression (no death-assert, no log)
- Orphaned tag (exclusive AR fired, paired AW never sent): persists until same-ID exclusive AR overwrites or overlapping normal write clears; not auto-collected

---

## 5 Test plan (~32 new tests)

### A. Stateless protocol rules (7 EXPECT_DEATH in test_protocol_rules.cpp)
1. `LockEncoding_RejectsRawTwo`
2. `ExclusiveTotalBytes_Rejects256`
3. `ExclusiveTotalBeats_Rejects32`
4. `ExclusivePow2_RejectsLen2`
5. `ExclusiveAlign_RejectsUnaligned`
6. `ExclusiveBurstFixed_Rejects`
7. Positive controls (INCR + WRAP valid configurations)

### B. AxiSlave monitor unit tests (13 in test_axi_slave.cpp)
1. `ExclusiveAR_SetsTag_NotReady`
2. `ExclusiveAR_RComplete_TagBecomesReady`
3. `NormalWrite_NoOverlap_TagSurvives`
4. `NormalWrite_Overlap_TagCleared`
5. `ExclusivePair_FullMatch_EXOKAY_CommitsMemory`
6. `ExclusiveWrite_NoPriorRead_OKAY_NoCommit`
7. `ExclusiveWrite_BeforeReady_OKAY_NoCommit` (Q1 fix)
8. `ExclusiveWrite_SizeMismatch_OKAY_NoCommit`
9. `ExclusiveWriteOnOob_DECERR` (Q5 fix: DECERR > EXOKAY)
10. `ExclusiveWRAP_TagRangeIsWrapWindow` (Q10b fix)
11. `MultiId_NormalWriteErasesMultipleTags_IteratorSafe` (Q6 fix)
12. `ExclusiveAR_SameId_SecondOverwritesFirst` (codex S4 Gap)
13. `DifferentId_ExclusiveAW_DoesNotAffectOtherTag` (codex S4 Gap)

### C. ScenarioParser tests (4 in test_axi_master.cpp ScenarioParser fixture)
1. `LockNormalAccepted`
2. `LockExclusiveAccepted`
3. `LockDefaultsToNormal`
4. `LockInvalidStringThrows`

### D. AxiMaster wire-through (2 in test_axi_master.cpp AxiMasterTest fixture)
1. `LockFieldPropagatesToAwLock` (Exclusive)
2. `LockDefaultsToZero_OnNormalTxn` (codex S4 Gap)

### E. Integration fixtures (4 in tests/axi/fixtures/)
1. `exclusive_pair_success.yaml` — match → EXOKAY + memory updated
2. `exclusive_intervening_write.yaml` — AR → normal AW overlap → exclusive AW → OKAY + memory has intervening value (verified by post-failed-write read)
3. `exclusive_no_prior_read.yaml` — only exclusive AW → OKAY + memory unchanged
4. `exclusive_wrap_pair_success.yaml` — WRAP exclusive full match → EXOKAY

### Test naming convention
- Phase C parser tests use unique tempfile names (per `testing::UnitTest::current_test_info()->name()`) to avoid worsening the pre-existing parallel-ctest tempfile flake from Phase A/B
- Sequential ctest target: ~181/181

### Phase A+B regression
- Phase A 78 + Phase B 71 = 149 tests must remain green at every commit
- WriteResult adds `lock` field: existing test_scoreboard.cpp ~5 aggregate-init callsites need `LockType::Normal` appended; existing fixtures with no `lock` field default Normal via parser

---

## 6 File impact

### Modified
- `c_model/include/axi/types.hpp` — `LockType` enum
- `c_model/include/axi/scenario_parser.hpp` — `ScenarioTransaction.lock` + YAML parse
- `c_model/include/axi/axi_master.hpp` — lock wire-through + `WriteResult.lock`
- `c_model/include/axi/axi_slave.hpp` — `exclusive_tags_` + `compute_tag_range` + E1–E6 in tick() + `WriteBurstState.is_exclusive/exclusive_match`
- `c_model/include/axi/scoreboard.hpp` — commit condition for failed exclusive
- `c_model/include/axi/protocol_rules.hpp` — 6 stateless + 1 monitor helper
- `c_model/include/axi/ATTRIBUTION.md` — note Phase C is independent design (no OSS port; AXIDOUBLE master-side reference only)
- `c_model/tests/axi/test_protocol_rules.cpp` — 7 death tests + positive controls
- `c_model/tests/axi/test_axi_slave.cpp` — 13 unit tests
- `c_model/tests/axi/test_axi_master.cpp` — 4 parser + 2 wire-through tests
- `c_model/tests/axi/test_scoreboard.cpp` — ~5 aggregate-init callsites
- `c_model/tests/axi/test_integration.cpp` — 4 new FixtureParam rows
- `c_model/NEXT_STEPS.md` — Phase C complete, known limitations, Phase D roadmap

### New
- `c_model/tests/axi/fixtures/exclusive_pair_success.yaml` + `_data.txt`
- `c_model/tests/axi/fixtures/exclusive_intervening_write.yaml` + `_data.txt`
- `c_model/tests/axi/fixtures/exclusive_no_prior_read.yaml` + `_data.txt`
- `c_model/tests/axi/fixtures/exclusive_wrap_pair_success.yaml` + `_data.txt`

---

## 7 Commit plan (3 commits)

1. **`feat(c_model): LockType + protocol_rules exclusive helpers + parser lock field`**
   - types.hpp `LockType` enum
   - scenario_parser.hpp `lock` field + YAML parse
   - protocol_rules.hpp 6 stateless + 1 monitor helper
   - 4 parser tests + 7 death tests + positive controls
   - Phase A+B 149 tests green

2. **`feat(c_model): AxiSlave exclusive monitor (E1-E6) + WriteResult.lock + Scoreboard`**
   - axi_master.hpp lock wire-through + WriteResult.lock
   - axi_slave.hpp exclusive_tags_ + E1-E6
   - scoreboard.hpp commit condition
   - 13 monitor unit tests + 2 wire-through tests
   - test_scoreboard.cpp aggregate-init callsites
   - ~174 tests green

3. **`test(c_model): integration fixtures + ATTRIBUTION + NEXT_STEPS`**
   - 4 integration fixtures + data files
   - test_integration.cpp 4 new FixtureParam
   - ATTRIBUTION.md note
   - NEXT_STEPS.md Phase C marker
   - ~181 tests green

---

## 8 Known limitations / future scope

- **Single-master only**: multi-master scenarios (multiple OSS exclusive monitor implementations exist for that case but require c_model master_id field). Deferred.
- **Orphaned tag**: exclusive AR without paired AW persists until overwrite/overlap. Acceptable for finite scenarios; not exposed via debug counter (could be added trivially).
- **Cache/prot match**: included in `check_exclusive_write_matches_read_tag` per §A7.2.4. The c_model `AwBeat`/`ArBeat.cache/prot` fields exist from Phase A but are not commonly populated by tests.
- **Monitor granularity**: byte-level half-open overlap (most conservative). IHI 0022 allows up to 128-byte page granularity. The conservative choice may cause some near-but-not-overlapping normal writes to incorrectly preserve a tag, but never the opposite (false EXOKAY).
- **Tick ordering**: relies on Phase B AxiSlave::tick() fixed step order; documented as design contract.

---

## 9 Drift gates (every commit)

```
cd c_model && cmake --build build && ctest --test-dir build  # ~181/181 sequential
cd ../spec_validate && py -3 -m pytest -q                     # 159 tests
py -3 tools/codegen.py --check                                # byte-identical
py -3 tools/gen_inventory.py --check                          # FEATURE_INVENTORY drift
```

Pre-existing `spec_validate/include/*.h` + `spec_validate/rtl_pkg/*.sv` drift remains unstaged (Phase A/B convention).

---

## 10 Process

- Single stage; not split. `IMPLEMENTATION_PLAN.md` may not be needed for one stage of ~3 commits; if used, removed at stage completion.
- Subagent-driven execution (mirrors Phase B pattern): implementer subagent → spec compliance review → code quality review per commit cluster.
- DO NOT use `--no-verify`. DO NOT touch `spec/ni/doc/*`.
- AXI4 protocol behavior follows IHI 0022 §A7; c_model does not redesign spec semantics.
