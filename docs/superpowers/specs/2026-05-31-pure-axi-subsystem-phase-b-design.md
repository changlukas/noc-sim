# Pure AXI Subsystem — Phase B Design (Stage 2 Phase B)

> 2026-05-31 · extends Phase A spec `docs/superpowers/specs/2026-05-29-pure-axi-subsystem-design.md`
>
> 7 features in 1 PR, 6 stages, ~400-500 lines new code, +10 fixtures. Phase A architecture preserved.

---

## Purpose

Extend Phase A pure AXI subsystem (4 c_model classes, INCR aligned full-WSTRB only) to handle:
- Full AXI4 burst types (INCR / WRAP / FIXED)
- Unaligned start addresses
- Narrow transfers (AxSIZE < log2(DATA_BYTES))
- Sparse WSTRB byte-merge
- 4KB cross detection + auto-segmentation (master-side)
- Runtime protocol validation (22/27 c_model-reachable AXI4 rules, debug-only via `AXI_PROTOCOL_ASSERT` macro)

---

## Scope

**In scope (Phase B)**:
- 7 features above
- Per-ID FIFO state model refactor (AxiSlave `active_writes_`/`active_reads_` becomes `std::map<uint8_t, std::deque<...>>`)
- `OperationContext` layer in AxiMaster (manages multi-sub-burst aggregation per scenario txn)
- `axi/protocol_rules.hpp` new file (centralized inline validation helpers + `AXI_PROTOCOL_ASSERT` macro)
- YAML `strb_file` optional field; parser relaxes burst != INCR / unaligned / 4KB cross / narrow
- 10 new integration fixtures + ~35 unit tests + 22 death tests

**Out of scope**:
- DPI-C bridge (Stage 3+; unlocks handshake-level rules `*_VALID_STABLE` etc.)
- Exclusive access AxLOCK / EXOKAY (Phase C; needs `ExclusiveMonitor` stateful class)
- ATOP / WCONTINUE (AXI4 extensions; not on roadmap)
- Handshake-level rules (10 in spec_validate; require DPI)
- `*_RESET` rules (5 in spec_validate; c_model untimed has no reset)

---

## Architecture

**Sustained from Phase A** — same namespace `ni::cmodel::axi`, same tick model (same-cycle mailbox), same file layout. Phase B is **extension**, not redesign.

**Modified files**:
- `axi/scenario_parser.hpp` — strb_file field, relaxed validation
- `axi/axi_master.hpp` — OperationContext, 4KB split, narrow byte-lane, sparse strb load
- `axi/axi_slave.hpp` — per-ID FIFO refactor, WRAP/FIXED addr calc, narrow interpret, inline asserts
- `axi/scoreboard.hpp` — sparse strb byte-merge update
- `axi/types.hpp` — add `constexpr std::size_t k4KBytes = 0x1000;`

**New file**:
- `axi/protocol_rules.hpp` — `AXI_PROTOCOL_ASSERT` macro + 22 stateless `inline bool check_*()` helpers (state passed in by caller)

**Unchanged**:
- `axi/memory.hpp` — WSTRB byte mask already supported
- `axi/memory_port.hpp` — interface stable
- Tick contract — same-cycle mailbox preserved
- 78 Phase A tests stay green

**Stage split (6 stages, dependency-flow)**:

| Stage | Content | Commits |
|-------|---------|---------|
| **B-1** | Sparse infra: strb_file YAML + master load + Scoreboard sparse update | ~1-2 |
| **B-2** | Unaligned start: master first-beat WSTRB + slave handle | ~1-2 |
| **B-3** | Narrow transfer: master byte-lane placement + slave interpret | ~1-2 |
| **B-4** | WRAP + FIXED burst: slave addr formula + parser relax + WRAP len check | ~2 |
| **B-5a** | 4KB cross + OperationContext + per-ID FIFO state refactor | ~2-3 |
| **B-5b** | Runtime validation: protocol_rules.hpp + 22 inline asserts + 22 death tests | ~2 |

**Total**: ~10-13 commits, ~400-500 LOC.

---

## Components

### `types.hpp`
Add `constexpr std::size_t k4KBytes = 0x1000;` for 4KB boundary math.

### `protocol_rules.hpp` (NEW)
```cpp
// (Independent design; validation patterns inspired by cocotbext-axi)
#pragma once
#include "axi/types.hpp"
#include <cassert>

namespace ni::cmodel::axi::rules {

#ifdef NDEBUG
  #define AXI_PROTOCOL_ASSERT(cond, msg) ((void)0)
#else
  #define AXI_PROTOCOL_ASSERT(cond, msg) assert((cond) && (msg))
#endif

// 5 stateless field checks (echoed at runtime; parser also enforces)
inline bool check_burst_encoding(Burst b);
inline bool check_size_bound(uint8_t size);
inline bool check_wrap_len(Burst b, uint8_t len);
inline bool check_wrap_align(Burst b, uint64_t addr, uint8_t size);
inline bool check_resp_encoding(Resp r);

// 8 stateful intra-burst (state passed in by caller)
inline bool check_w_beat_count_within(std::size_t submitted, uint8_t len);
inline bool check_w_last_timing(bool last, std::size_t beat_idx, uint8_t len);
inline bool check_strb_sparse_legal(uint32_t strb, uint8_t size,
                                     uint64_t beat_addr, std::size_t beat_idx,
                                     uint8_t len, Burst burst);
inline bool check_r_beat_count_within(std::size_t returned, uint8_t len);
inline bool check_r_last_timing(bool last, std::size_t beat_idx, uint8_t len);
inline bool check_b_one_response_per_write(std::size_t b_count_so_far);
inline bool check_w_no_interleave(uint8_t expected_id, uint8_t actual_id);
inline bool check_strb_valid_bits(uint32_t strb);  // upper bits zero per WSTRB_WIDTH

// 7 cross-channel ordering (state passed in by caller / tracked by master/slave)
inline bool check_b_id_match_outstanding(uint8_t b_id, const auto& outstanding);
inline bool check_r_id_match_outstanding(uint8_t r_id, const auto& outstanding);
inline bool check_same_id_w_order(uint8_t id, const auto& w_queue_order);
inline bool check_same_id_r_order(uint8_t id, const auto& r_queue_order);
inline bool check_diff_id_interleave_allowed();  // tautology; documents rule
inline bool check_w_before_b(bool all_w_done, bool b_ready);
inline bool check_aw_w_independence();  // tautology

// 2 extras
// (no extra helpers; B_ONE_RESPONSE_PER_WRITE in 8 above; W_NO_INTERLEAVE in 8 above)

}  // namespace ni::cmodel::axi::rules
```

Total 22 helpers. **Stateless** = no internal state; caller provides context.

### `scenario_parser.hpp`
**Additions**:
- Parse optional `strb_file` field per write txn.
- Reject WRAP burst len ∉ {1,3,7,15}.
- Reject WRAP burst addr unaligned to `(1<<size)`.
- Reject WRAP + unaligned addr combination.

**Relaxed (Phase B accepts)**:
- INCR burst with unaligned start addr.
- INCR with narrow size (size < 5).
- INCR with 4KB cross (master handles).
- FIXED burst.

### `axi_master.hpp`
**New `OperationContext`**:
```cpp
struct BurstSpec { uint64_t addr; uint8_t len, size; Burst burst; };

struct OperationContext {
  ScenarioTransaction src_txn;
  std::vector<BurstSpec> sub_bursts;   // 4KB cross may split into N
  std::size_t completed_count = 0;
  Resp worst_resp = Resp::OKAY;
  std::vector<uint8_t> aggregate_data;     // for read accumulation
  std::vector<uint32_t> strb_per_beat;     // loaded from strb_file (or default)
};
std::deque<OperationContext> active_ops_;
```

**4KB split algorithm**:
```
Input: scenario txn (addr, len, size, burst)
If burst == FIXED or burst == WRAP: 1 sub-burst (no split)
If burst == INCR:
    total_bytes = (len+1) * (1<<size)
    while bytes_remaining > 0:
        bytes_to_4kb_boundary = k4KBytes - (cur_addr & (k4KBytes-1))
        beats_in_this_subburst = min(bytes_to_4kb_boundary / (1<<size), 256, beats_remaining)
        emit sub-burst (cur_addr, beats_in_this_subburst - 1, size, INCR)
        cur_addr += beats_in_this_subburst * (1<<size)
        beats_remaining -= beats_in_this_subburst
```

(Also respects AXI4 INCR max 256-beat constraint per Codex review.)

**Narrow byte-lane placement**:
```cpp
// For each W beat:
//   byte_lane = (beat_addr) & (DATA_BYTES - 1)
//   data_bytes_this_beat = 1 << size
//   wbeat.data[byte_lane .. byte_lane + data_bytes_this_beat - 1] = user_data[...]
//   wbeat.strb = ((1u << data_bytes_this_beat) - 1) << byte_lane
```

**Unaligned first-beat strb**:
```cpp
// First beat WSTRB: bytes below (addr - aligned_addr) are 0
//   first_lane = addr & (DATA_BYTES - 1)
//   wbeat.strb = full_strb & ~((1u << first_lane) - 1)
```

**Sparse strb_file load**: master ctor opens strb_file if present; verifies line count matches data_file; per-beat WSTRB applied to `wbeat.strb`.

**On B response**: increment `ctx.completed_count`. When `completed_count == sub_bursts.size()` → fire 1 `WriteResult` with aggregate.

### `axi_slave.hpp`
**State model refactor** (CRITICAL — Codex Issue 2 fix):
```cpp
// Phase A:
std::map<uint8_t, WriteBurstState> active_writes_;
// Phase B:
std::map<uint8_t, std::deque<WriteBurstState>> active_writes_;
```

Each ID may have multiple in-flight bursts (FIFO order). Admission check becomes `if (active_writes_[id].size() >= max_per_id) break;`.

**WRAP addr formula**:
```cpp
// burst_byte_total = (len+1) * (1<<size)
// wrap_lower = base_addr & ~(burst_byte_total - 1)
// wrap_upper = wrap_lower + burst_byte_total
// beat_addr[i] = base_addr + i*(1<<size); if exceeds wrap_upper, wrap back to wrap_lower
```

**FIXED addr**: `beat_addr[i] = base_addr` for all i.

**Narrow interpret**: AxiSlave honors WSTRB from master; forwards to memory; memory writes only enabled bytes.

**Inline asserts** (per Codex Section 3 review, ~22 placements):
- Step 1 (drain B): B_ID_MATCH, RESP_ENCODING, B_ONE_RESPONSE_PER_WRITE
- Step 1 (drain R): R_ID_MATCH, RESP_ENCODING, R_LAST_TIMING, R_BEAT_COUNT_OVERFLOW
- Step 3 (start AW): BURST_ENCODING, SIZE_BOUND, WRAP_LEN_ENCODING, WRAP_ALIGN
- Step 4 (submit W): W_BEAT_COUNT_OVERFLOW, W_LAST_TIMING, STRB_SPARSE_LEGAL, STRB_VALID_BITS, W_NO_INTERLEAVE
- Step 5 (start AR): BURST_ENCODING, SIZE_BOUND, WRAP_LEN_ENCODING, WRAP_ALIGN
- Ordering invariants: SAME_ID_W_ORDER, SAME_ID_R_ORDER, W_BEFORE_B (where applicable)

### `scoreboard.hpp`
**Signature change**:
```cpp
void handle_write_completed(const WriteResult& wr,
                            const std::vector<uint8_t>& data,
                            const std::vector<uint32_t>& strb_per_beat);
```
**Behavior**: for each beat, walk byte 0..DATA_BYTES-1, update `byte_map[addr+i]` only if `strb_per_beat[beat_idx]` bit `i` is 1.

Phase A callsite migration: pass `std::vector<uint32_t>(beat_count, 0xFFFF'FFFFu)` for backward compat in Phase A fixtures.

### YAML schema extend
```yaml
config:
  # (Phase A fields unchanged)
transactions:
  - op: write
    addr: 0x1003            # ← Phase B 接受 unaligned (INCR only)
    id: 0x5
    len: 3
    size: 2                 # ← Phase B 接受 narrow (size < 5)
    burst: WRAP             # ← Phase B 接受 WRAP / FIXED
    data_file: foo_data.txt
    strb_file: foo_strb.txt # ← Phase B 新 optional field
```

**strb_file 格式**: 每行 1 個 8-hex-digit 表示 32-bit WSTRB（line count 必須 == data_file line count）。

---

## Data flow

### 4KB cross auto-split flow
```
scenario.yaml txn (addr=0x0FE0, len=7, size=5, INCR, id=5)
    ↓
AxiMaster::tick():
    sub_bursts = split_at_4kb(...)
    → sub0: addr=0x0FE0, len=0   (1 beat, 32B, ends at 0x0FFF)
    → sub1: addr=0x1000, len=6   (7 beats, 224B, ends at 0x10DF)
    OperationContext{ src_txn, sub_bursts=[sub0,sub1], completed=0, worst=OKAY }
    active_ops_.push_back(ctx)
    push AW0 (id=5, addr=0x0FE0, len=0) + W0 (last=1)
    push AW1 (id=5, addr=0x1000, len=6) + W1..W7 (W7 last=1)
    ↓
AxiSlave (active_writes_[5] has 2 entries in FIFO):
    process sub0 → memory writes → B(id=5, OKAY)
    process sub1 → memory writes × 7 → B(id=5, OKAY)
    ↓
AxiMaster on each B:
    ctx.completed_count++
    if completed == sub_bursts.size():
        fire on_write_completed(WriteResult{
            addr=0x0FE0, data=256B aggregate, strb_per_beat=full,
            resp=OKAY, id=5, scenario_line=N
        })
        active_ops_.pop_front()
```

### Unaligned start flow
```
scenario.yaml: addr=0x1005, 1 beat, size=5
    ↓
AxiMaster: align to 0x1000, set first_beat_strb=0xFFFFFFE0 (bytes 5-31 valid)
    Place user data at WBeat.data[5..31]
    push AW(addr=0x1000) + W(strb=0xFFFFFFE0, data lanes 5-31)
    ↓
AxiSlave forward to memory with strb
    Memory writes only lanes 5-31; lanes 0-4 keep fill_byte
    ↓
Scoreboard handle_write_completed:
    For lane 5-31: byte_map[0x1005..0x101F] = user data
    For lane 0-4: byte_map unchanged (defaults to 0)
```

### Narrow transfer flow
```
scenario.yaml: addr=0x1004, len=1, size=2 (4B/beat), 2 beats
    ↓
AxiMaster:
    beat 0: addr=0x1004, byte_lane=4, strb=0x000000F0, data[4..7]=user[0..3]
    beat 1: addr=0x1008, byte_lane=8, strb=0x00000F00, data[8..11]=user[4..7]
    push AW + W × 2
    ↓
AxiSlave forwards each beat to memory with correct strb
Memory stores 4B per beat at correct addresses
```

### WRAP burst flow (real-wrap example)
```
scenario.yaml: addr=0x1060, len=3, size=5, WRAP
    burst_byte_total = 4 * 32 = 128 = 0x80
    wrap_lower = 0x1060 & ~0x7F = 0x1000
    wrap_upper = 0x1080
    ↓
AxiSlave addr sequence:
    beat 0: 0x1060
    beat 1: 0x1060 + 32 = 0x1080 ≥ wrap_upper → wrap to 0x1000
    beat 2: 0x1020
    beat 3: 0x1040
```

### FIXED burst flow
```
scenario.yaml: addr=0x1000, len=3, size=5, FIXED
    All 4 beats → addr=0x1000
    Later beats overwrite earlier (memory only retains last write per byte)
```

### Tick loop (unchanged from Phase A)
```cpp
while (!master.done()) {
  master.tick();
  slave.tick();
  mem.tick();
  if (++cycle > kMaxCycles) FAIL("watchdog");
}
```

---

## Error handling

**Principle (sustained from Phase A)**: user input → throw (all builds); c_model invariant → `AXI_PROTOCOL_ASSERT` (debug only).

| Trigger | Behavior | All builds? |
|---------|----------|------------|
| YAML unknown field | throw | yes |
| WRAP burst len ∉ {1,3,7,15} | throw at parser | yes |
| WRAP burst unaligned addr | throw at parser | yes |
| WRAP + unaligned combo | throw at parser | yes |
| INCR + unaligned | accept | — |
| INCR + narrow | accept | — |
| INCR + 4KB cross | accept (master splits) | — |
| FIXED burst | accept | — |
| strb_file file open fail | throw at master ctor | yes |
| strb_file line count ≠ data_file | throw at master ctor | yes |
| strb_file invalid hex token | throw at master ctor | yes |
| data_file total bytes inconsistent with burst spec | throw at master ctor | yes |
| YAML numeric out of width (addr/len/size/id) | throw at parser | yes |
| Memory OOB | DECERR via normal path | RTL-faithful |
| 22 AXI4 invariants | `AXI_PROTOCOL_ASSERT(..., "<RULE_NAME>: <desc>")` | debug only |
| OperationContext over-completion | `AXI_PROTOCOL_ASSERT` | debug only |
| Watchdog timeout | `FAIL` | yes |

**Macro design**:
```cpp
#ifdef NDEBUG
  #define AXI_PROTOCOL_ASSERT(cond, msg) ((void)0)
#else
  #define AXI_PROTOCOL_ASSERT(cond, msg) assert((cond) && (msg))
#endif
```

**Assert message format**: `"<RULE_ID>: <human description>"` (e.g., `"W_LAST_TIMING: WLAST must be set only on last beat (beat N / total M)"`).

---

## Testing

### Test patterns (no new framework; carry over from Phase A)

- `TEST()` / `TEST_F()` / `TEST_P()` + `INSTANTIATE_TEST_SUITE_P` (Phase A established)
- `EXPECT_DEATH()` for `AXI_PROTOCOL_ASSERT` debug-build verification, gated by `#ifndef NDEBUG`; release build uses `GTEST_SKIP()`
- Mock-based isolation via `MockMemoryPort` / `MockSlave`

### Scenarios per stage

| Stage | Unit tests | Integration fixtures |
|-------|-----------|---------------------|
| B-1 | `test_scenario_parser`: strb_file parse (empty / valid / line-count mismatch → throw / missing → master throw / invalid hex → throw); `test_scoreboard`: sparse strb byte-merge (fill 0xFF, write 0x00 strb 0x000F → bytes 0-3 = 0x00, bytes 4-31 = 0xFF) | (none — infra only) |
| B-2 | `test_axi_master`: first-beat WSTRB calc (addr=0x1003 size=2 → strb=0xF8); `test_axi_slave`: receives sparse strb → forward correctly | `unaligned_start.yaml` |
| B-3 | `test_axi_master`: byte-lane placement at all narrow sizes 0-4; `test_axi_slave`: interprets narrow beats per WSTRB | `narrow_transfer_size2.yaml`, `narrow_transfer_size0.yaml` |
| B-4 | `test_axi_slave`: WRAP addr calc at all valid lens 1/3/7/15; FIXED addr; `test_scenario_parser`: WRAP len ∉ {1,3,7,15} throw; WRAP + unaligned throw | `wrap_burst_aligned.yaml`, `wrap_burst_actual_wrap.yaml`, `fixed_burst.yaml` |
| B-5a | `test_axi_master`: 4KB split algorithm (split point, max-beat constraint); `test_axi_slave`: per-ID FIFO (same-ID multi-burst); `test_axi_master`: OperationContext aggregation | `cross_4kb_auto_split.yaml` |
| B-5b | 22 parameterized death tests under `AxiProtocolDeath/RuleP` with readable names (e.g. `InvalidWrapLen`, `Cross4KBNoSplit`) | (validation exercised by all integration fixtures) |
| (combined) | (cross-feature coverage) | `narrow_unaligned.yaml`, `sparse_multibeat.yaml` |

**Total Phase B**: ~10 integration fixtures + ~35 unit tests + 22 death tests + 1 parser-negative test for WRAP+unaligned = **~68 new tests**.

**Total Phase A + B**: ~146 tests.

### Drift gates (every commit, unchanged from Phase A)
```
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```

### OSS attribution
- Per-file header `// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md` on extended axi/* hpps.
- `ATTRIBUTION.md` add new file: `protocol_rules.hpp → (independent design, patterns inspired by cocotbext-axi inline asserts)`.
- `strb_file` format = local fixture format, not cocotbext-axi-derived (cocotbext-axi accepts API data and generates WSTRB internally).
- `4KB cross 1-result-per-scenario semantic` = cocotbext-axi aligned (operation-level completion).

---

## Future roadmap

### Phase C
- Exclusive access (AxLOCK + EXOKAY): `ExclusiveMonitor` stateful class (per-ID + addr range tracking)
- YAML `lock` field

### Stage 3+ NoC integration / DPI bridge
- DPI shim wraps c_model axi/* for RTL co-sim
- Handshake-level rules (`*_VALID_STABLE`, `*_VALID_NO_WAIT`) become reachable via pin-level DPI monitor
- SV testbench + Verilator/VCS integration

---

## [TBD]

- `protocol_rules.hpp` template parameters for ordering helpers — `auto&` deduction may not work in all GCC versions; verify or use concrete types
- `cross_4kb_auto_split.yaml` data_file size needs to match aggregate 256B carefully — generator script (`gen_write_data.py`) reused from Phase A
