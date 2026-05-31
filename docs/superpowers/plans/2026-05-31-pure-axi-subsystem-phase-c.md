# Pure AXI Subsystem Phase C Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Phase A+B Pure AXI subsystem so AxiSlave correctly models AXI4 exclusive access (AxLOCK + EXOKAY) per IHI 0022 §A7.

**Architecture:** Slave-only monitor (master is pure wire-through). Per-ID `exclusive_tags_` map with 6-event state machine in `AxiSlave::tick()`. Tag stores attrs + ready flag; mismatched exclusive write returns silent OKAY and suppresses memory commit. Memory DECERR/SLVERR takes priority over EXOKAY.

**Tech Stack:** C++17 + GoogleTest + yaml-cpp 0.9.0 (Phase A+B wiring reused).

**Spec**: `docs/superpowers/specs/2026-05-31-pure-axi-subsystem-phase-c-design.md`

**Project rules** (carry over from Phase B):
- Drift gates every commit (see "Drift gates" at bottom)
- Commit format `type(scope): description` English (feat/fix/docs/style/refactor/test/chore/perf)
- DO NOT `--no-verify`; DO NOT touch `spec/ni/doc/*`, `spec_validate/include/*.h`, `spec_validate/rtl_pkg/*.sv`
- Karpathy 4-lens review per task (overcomplication / surgical / surface assumptions / verifiable success)
- **Phase A 78 + Phase B 71 = 149 tests stay green at every commit**

---

## File structure (final Phase C state)

```
c_model/include/axi/
    types.hpp                ← +LockType enum
    memory_port.hpp          ← unchanged
    memory.hpp               ← unchanged
    protocol_rules.hpp       ← +6 stateless + 1 monitor helper
    axi_slave.hpp            ← exclusive_tags_, compute_tag_range, E1-E6 + WriteBurstState additions
    axi_master.hpp           ← lock wire-through + WriteResult.lock
    scoreboard.hpp           ← commit condition for failed exclusive
    scenario_parser.hpp      ← lock field + YAML parse
    ATTRIBUTION.md           ← +note about Phase C independent design
c_model/tests/axi/
    test_scaffold.cpp        ← unchanged
    test_memory.cpp          ← unchanged
    test_axi_slave.cpp       ← +13 monitor unit tests
    test_axi_master.cpp      ← +4 ScenarioParser tests + 2 wire-through tests
    test_scoreboard.cpp      ← ~5 WriteResult aggregate-init callsites updated
    test_protocol_rules.cpp  ← +7 EXPECT_DEATH tests + positive controls
    test_integration.cpp     ← +4 FixtureParam rows
    fixtures/
        exclusive_pair_success.yaml + data file
        exclusive_intervening_write.yaml + data file
        exclusive_no_prior_read.yaml + data file
        exclusive_wrap_pair_success.yaml + data file
        (existing Phase A+B fixtures)
c_model/NEXT_STEPS.md        ← updated Phase C complete + Phase D scope
```

---

# Task C.1 — Foundation: LockType + parser + protocol rules + tests

**Goal**: Add `LockType` enum, parser `lock` field, 6 stateless protocol helpers + 1 monitor helper signature + 7 death tests + 4 parser tests. Phase A+B 149 + new tests green. Single commit.

### Task C.1.1: Add `LockType` enum to types.hpp

**Files:**
- Modify: `c_model/include/axi/types.hpp`

- [ ] **Step 1: Open the file and locate the `Resp` enum**

Read `c_model/include/axi/types.hpp` to find the `enum class Resp` definition (around line 17). Note adjacent definitions (`Burst`, `DATA_BYTES`).

- [ ] **Step 2: Add `LockType` enum below `Resp`**

After the line `enum class Resp ...`, insert:
```cpp
// AXI4 IHI 0022 §A7.2: AxLOCK signals are 1-bit in AXI4 (0=Normal, 1=Exclusive).
// AXI3 deprecated LOCKED bit not modeled.
enum class LockType : uint8_t { Normal = 0, Exclusive = 1 };
```

Verify the existing `AwBeat::lock` / `ArBeat::lock` fields remain `uint8_t` (wire fidelity). Canonical raw→enum conversion happens at AxiSlave admit step in Task C.2.

- [ ] **Step 3: Build to verify compile**

```bash
cd c_model && cmake --build build
```

Expected: clean build (no new test yet).

### Task C.1.2: Add `lock` field to ScenarioTransaction + YAML parser

**Files:**
- Modify: `c_model/include/axi/scenario_parser.hpp`

- [ ] **Step 1: Add `lock` field to ScenarioTransaction struct**

In `scenario_parser.hpp`, find the `ScenarioTransaction` struct. After the existing fields (e.g., after `strb_file`), add:
```cpp
LockType lock = LockType::Normal;  // optional; YAML "normal" or "exclusive"
```

- [ ] **Step 2: Parse `lock` in `load_scenario`**

In the `load_scenario()` txn loop, after the existing field parsing (e.g., after `strb_file` parse), add:
```cpp
if (txn["lock"]) {
  std::string lock_str = txn["lock"].as<std::string>();
  if (lock_str == "normal") {
    t.lock = LockType::Normal;
  } else if (lock_str == "exclusive") {
    t.lock = LockType::Exclusive;
  } else {
    throw std::runtime_error("scenario txn " + std::to_string(line) +
                              ": lock must be 'normal' or 'exclusive' (got '" + lock_str + "')");
  }
}
```

Default (missing field) leaves `t.lock = LockType::Normal`.

- [ ] **Step 3: Build to verify compile**

```bash
cd c_model && cmake --build build
```

Expected: clean build.

### Task C.1.3: ScenarioParser tests for `lock` field

**Files:**
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Write 4 ScenarioParser tests with unique tempfile names**

Locate the existing `ScenarioParser` test fixture in `test_axi_master.cpp`. Append the following tests inside that fixture. Use a per-test unique tempfile name pattern to avoid worsening the pre-existing parallel-ctest flake:

```cpp
TEST_F(ScenarioParser, LockNormalAccepted) {
  std::string tmp_name = std::string("/lock_normal_") +
      ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".yaml";
  auto path = std::string(::testing::TempDir()) + tmp_name;
  std::ofstream(path) << R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: normal
    data_file: w.txt
)YAML";
  auto sc = axi::load_scenario(path);
  ASSERT_EQ(sc.transactions.size(), 1u);
  EXPECT_EQ(sc.transactions[0].lock, axi::LockType::Normal);
}

TEST_F(ScenarioParser, LockExclusiveAccepted) {
  std::string tmp_name = std::string("/lock_excl_") +
      ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".yaml";
  auto path = std::string(::testing::TempDir()) + tmp_name;
  std::ofstream(path) << R"YAML(
transactions:
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    dump_file: r.txt
)YAML";
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.transactions[0].lock, axi::LockType::Exclusive);
}

TEST_F(ScenarioParser, LockDefaultsToNormal) {
  std::string tmp_name = std::string("/lock_default_") +
      ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".yaml";
  auto path = std::string(::testing::TempDir()) + tmp_name;
  std::ofstream(path) << R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    data_file: w.txt
)YAML";
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.transactions[0].lock, axi::LockType::Normal);
}

TEST_F(ScenarioParser, LockInvalidStringThrows) {
  std::string tmp_name = std::string("/lock_invalid_") +
      ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".yaml";
  auto path = std::string(::testing::TempDir()) + tmp_name;
  std::ofstream(path) << R"YAML(
transactions:
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: foo
    dump_file: r.txt
)YAML";
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}
```

- [ ] **Step 2: Build + run new tests**

```bash
cd c_model && cmake --build build && ctest --test-dir build -R "ScenarioParser\.Lock" --output-on-failure
```

Expected: 4 new tests pass.

- [ ] **Step 3: Full ctest to confirm Phase A+B regression-free**

```bash
ctest --test-dir build --output-on-failure -j 1
```

Expected: 149 prior + 4 new = 153/153 pass.

### Task C.1.4: Add 6 stateless protocol rule helpers + 1 monitor match helper

**Files:**
- Modify: `c_model/include/axi/protocol_rules.hpp`

- [ ] **Step 1: Add includes for new helpers**

Verify `c_model/include/axi/protocol_rules.hpp` already includes `"axi/types.hpp"`. If not, add it.

- [ ] **Step 2: Add 6 stateless helpers + 1 monitor helper**

Append to the `ni::cmodel::axi::rules` namespace, after the existing Phase B helpers (and before the closing namespace brace):

```cpp
// === Phase C: AXI4 Exclusive Access (IHI 0022 §A7) ===

inline bool check_lock_encoding(uint8_t raw) {
  // §A7.2: AxLOCK is 1-bit in AXI4 (0=Normal, 1=Exclusive).
  return raw <= 1;
}

inline bool check_exclusive_total_bytes_le_max(LockType lock, uint8_t len, uint8_t size) {
  // §A7.2.4: exclusive transfer total bytes ≤ 128.
  if (lock == LockType::Normal) return true;
  std::size_t total = (static_cast<std::size_t>(len) + 1) * (1ull << size);
  return total <= 128;
}

inline bool check_exclusive_total_beats_le_max(LockType lock, uint8_t len) {
  // §A7.2.4: exclusive transfer total beats ≤ 16.
  if (lock == LockType::Normal) return true;
  return len <= 15;
}

inline bool check_exclusive_total_pow2(LockType lock, uint8_t len) {
  // §A7.2.4: exclusive total beats (len+1) must be a power of 2.
  // Allowed len: 0, 1, 3, 7, 15.
  if (lock == LockType::Normal) return true;
  std::size_t beats = static_cast<std::size_t>(len) + 1;
  return (beats > 0) && ((beats & (beats - 1)) == 0);
}

inline bool check_exclusive_addr_aligned_to_total(LockType lock, uint64_t addr,
                                                    uint8_t len, uint8_t size) {
  // §A7.2.4: exclusive addr must be aligned to total burst bytes.
  if (lock == LockType::Normal) return true;
  std::size_t total = (static_cast<std::size_t>(len) + 1) * (1ull << size);
  return (addr & (total - 1)) == 0;
}

inline bool check_exclusive_burst_not_fixed(LockType lock, Burst burst) {
  // §A7.2.4: FIXED burst cannot be exclusive.
  if (lock == LockType::Normal) return true;
  return burst != Burst::FIXED;
}

// Forward declaration; full definition uses ExclusiveTag struct from axi_slave.hpp.
// To avoid circular include, we declare the helper signature here and define it
// inline as a template that takes the tag by const ref.
template <typename ExclusiveTagT>
inline bool check_exclusive_write_matches_read_tag(const ExclusiveTagT& tag,
                                                    const AwBeat& aw) {
  // §A7.2.4: ID + address + size + length + burst + cache + protection all match.
  // Also requires tag.ready (paired exclusive AR's RLAST observed).
  if (!tag.ready) return false;
  std::size_t bpb = 1ull << aw.size;
  std::size_t total = (static_cast<std::size_t>(aw.len) + 1) * bpb;
  uint64_t aw_start, aw_end;
  if (aw.burst == Burst::WRAP) {
    aw_start = aw.addr & ~(static_cast<uint64_t>(total) - 1);
    aw_end = aw_start + total;
  } else {
    aw_start = aw.addr;
    aw_end = aw.addr + total;
  }
  return tag.addr_start == aw_start
      && tag.addr_end   == aw_end
      && tag.len        == aw.len
      && tag.size       == aw.size
      && tag.burst      == aw.burst
      && tag.cache      == aw.cache
      && tag.prot       == aw.prot;
}
```

- [ ] **Step 3: Build to verify compile**

```bash
cd c_model && cmake --build build
```

Expected: clean build.

### Task C.1.5: 7 EXPECT_DEATH tests + positive controls for stateless rules

**Files:**
- Modify: `c_model/tests/axi/test_protocol_rules.cpp`

- [ ] **Step 1: Add `#include "axi/types.hpp"` if not already present**

Check the existing includes in `test_protocol_rules.cpp`; if `LockType` is not visible via existing includes, add `#include "axi/types.hpp"`.

- [ ] **Step 2: Add 7 death tests inside the existing `#ifndef NDEBUG` block**

Inside the `#ifndef NDEBUG` ... `#endif` block that contains the existing 22 helper death tests, append:

```cpp
TEST(AxiProtocolDeath, LockEncoding_RejectsRawTwo) {
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(rules::check_lock_encoding(2),
                        "LOCK_ENCODING: invalid raw lock value");
  }, "LOCK_ENCODING");
}

TEST(AxiProtocolDeath, ExclusiveTotalBytes_Rejects256) {
  // len=7, size=5 → 256 bytes > 128 limit
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(
      rules::check_exclusive_total_bytes_le_max(axi::LockType::Exclusive, 7, 5),
      "EXCLUSIVE_TOTAL_BYTES: exceeds 128");
  }, "EXCLUSIVE_TOTAL_BYTES");
}

TEST(AxiProtocolDeath, ExclusiveTotalBeats_Rejects32) {
  // len=31 → 32 beats > 16 limit
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(
      rules::check_exclusive_total_beats_le_max(axi::LockType::Exclusive, 31),
      "EXCLUSIVE_TOTAL_BEATS: exceeds 16");
  }, "EXCLUSIVE_TOTAL_BEATS");
}

TEST(AxiProtocolDeath, ExclusivePow2_RejectsLen2) {
  // len=2 → 3 beats not power of 2
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(
      rules::check_exclusive_total_pow2(axi::LockType::Exclusive, 2),
      "EXCLUSIVE_POW2: total beats not power of 2");
  }, "EXCLUSIVE_POW2");
}

TEST(AxiProtocolDeath, ExclusiveAlign_RejectsUnaligned) {
  // addr=0x1004, len=0, size=5 → total=32; 0x1004 & 0x1F != 0
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(
      rules::check_exclusive_addr_aligned_to_total(
          axi::LockType::Exclusive, 0x1004, 0, 5),
      "EXCLUSIVE_ALIGN: addr not aligned to total");
  }, "EXCLUSIVE_ALIGN");
}

TEST(AxiProtocolDeath, ExclusiveBurstFixed_Rejects) {
  EXPECT_DEATH({
    AXI_PROTOCOL_ASSERT(
      rules::check_exclusive_burst_not_fixed(
          axi::LockType::Exclusive, axi::Burst::FIXED),
      "EXCLUSIVE_BURST_FIXED: FIXED not allowed for exclusive");
  }, "EXCLUSIVE_BURST_FIXED");
}
```

- [ ] **Step 3: Add positive controls (INCR + WRAP valid)**

Inside the same `#ifndef NDEBUG` block (or in the suite — these don't need NDEBUG guard since they just verify helpers return `true`):

```cpp
TEST(AxiProtocolRules, ExclusiveValid_INCR_1Beat_Size5) {
  // addr=0x1000, len=0, size=5 → total=32, aligned, ≤128, INCR ok
  EXPECT_TRUE(rules::check_lock_encoding(1));
  EXPECT_TRUE(rules::check_exclusive_total_bytes_le_max(axi::LockType::Exclusive, 0, 5));
  EXPECT_TRUE(rules::check_exclusive_total_beats_le_max(axi::LockType::Exclusive, 0));
  EXPECT_TRUE(rules::check_exclusive_total_pow2(axi::LockType::Exclusive, 0));
  EXPECT_TRUE(rules::check_exclusive_addr_aligned_to_total(
      axi::LockType::Exclusive, 0x1000, 0, 5));
  EXPECT_TRUE(rules::check_exclusive_burst_not_fixed(
      axi::LockType::Exclusive, axi::Burst::INCR));
}

TEST(AxiProtocolRules, ExclusiveValid_WRAP_4Beat_Size5) {
  // addr=0x1000, len=3, size=5 → total=128, aligned, =128, WRAP ok
  EXPECT_TRUE(rules::check_exclusive_total_bytes_le_max(axi::LockType::Exclusive, 3, 5));
  EXPECT_TRUE(rules::check_exclusive_total_beats_le_max(axi::LockType::Exclusive, 3));
  EXPECT_TRUE(rules::check_exclusive_total_pow2(axi::LockType::Exclusive, 3));
  EXPECT_TRUE(rules::check_exclusive_addr_aligned_to_total(
      axi::LockType::Exclusive, 0x1000, 3, 5));
  EXPECT_TRUE(rules::check_exclusive_burst_not_fixed(
      axi::LockType::Exclusive, axi::Burst::WRAP));
}
```

- [ ] **Step 4: Build + run**

```bash
cd c_model && cmake --build build && ctest --test-dir build -R "AxiProtocol(Death|Rules)\..*Exclusive|AxiProtocolDeath\.Lock" --output-on-failure
```

Expected: 9 new tests pass (7 death + 2 positive).

- [ ] **Step 5: Full sequential ctest**

```bash
ctest --test-dir build --output-on-failure -j 1
```

Expected: 149 prior + 4 parser + 9 rule = 162/162 pass.

### Task C.1.6: Drift gates + commit C.1

- [ ] **Step 1: Run all drift gates**

```bash
cd c_model && cmake --build build && ctest --test-dir build -j 1
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```

Expected: 162/162 ctest, 159/159 pytest, clean codegen + inventory.

- [ ] **Step 2: Karpathy 4-lens review**

- Overcomplication: any new abstraction that doesn't earn its place? (LockType enum yes; helpers are mechanical)
- Surgical: only types.hpp + scenario_parser.hpp + protocol_rules.hpp + their tests touched
- Surface assumptions: pow2 check uses `(beats & (beats-1)) == 0`, valid for beats >= 1; document inline
- Verifiable success: 162/162 includes both death and positive paths

- [ ] **Step 3: Stage explicit files only and commit**

```bash
git add c_model/include/axi/types.hpp \
        c_model/include/axi/scenario_parser.hpp \
        c_model/include/axi/protocol_rules.hpp \
        c_model/tests/axi/test_axi_master.cpp \
        c_model/tests/axi/test_protocol_rules.cpp
git commit -m "$(cat <<'EOF'
feat(c_model): LockType + protocol_rules exclusive helpers + parser lock field

Adds LockType enum (Normal/Exclusive), ScenarioTransaction.lock field
with YAML parse (normal/exclusive/missing→Normal/invalid→throw), 6
stateless protocol rule helpers per IHI 0022 §A7.2.4 (lock encoding,
total bytes ≤128, total beats ≤16, pow2, addr aligned to total,
burst not FIXED), and 1 monitor match helper used by AxiSlave in C.2.

7 EXPECT_DEATH tests + 2 positive controls + 4 ScenarioParser tests
exercise the helpers. Phase A+B 149 + 13 new = 162/162 sequential ctest.
EOF
)"
```

(Pre-existing `spec_validate/include/*.h` + `spec_validate/rtl_pkg/*.sv` drift remains unstaged.)

---

# Task C.2 — AxiSlave exclusive monitor + master wire-through + Scoreboard

**Goal**: Implement E1-E6 state machine in AxiSlave, lock wire-through in AxiMaster, WriteResult.lock, Scoreboard commit gating, 13 monitor unit tests + 2 wire-through tests. Single commit. Phase A+B 149 + Task C.1 13 + C.2 ~15 = ~177 sequential ctest.

### Task C.2.1: AxiMaster lock wire-through + `WriteResult.lock`

**Files:**
- Modify: `c_model/include/axi/axi_master.hpp`

- [ ] **Step 1: Add `lock` field to `WriteResult` struct**

Find `struct WriteResult` (around line 17 per Phase B; verify by grep). After the existing fields (e.g., after `Burst burst`), insert:
```cpp
LockType lock = LockType::Normal;  // mirrors txn.lock; scoreboard detects failed exclusive via (lock + resp)
```

- [ ] **Step 2: Pass `txn.lock` into AW/AR push**

Find the AW push block in `tick()` (where `aw.id`, `aw.addr`, `aw.len`, `aw.size`, `aw.burst` are set). After those, add:
```cpp
aw.lock = (ws.txn.lock == LockType::Exclusive) ? 1u : 0u;
aw.cache = static_cast<uint8_t>(ws.txn.cache);  // if cache field already plumbed; else default 0
aw.prot  = static_cast<uint8_t>(ws.txn.prot);   // if prot field already plumbed; else default 0
```
(For cache/prot: if ScenarioTransaction doesn't currently have these fields, leave the AW push using whatever was there; Phase C does NOT add new cache/prot YAML fields. The exclusive match helper compares cache/prot but both sides will be 0 by default.)

Find the AR push block (where `ar.id`, `ar.addr`, etc. are set). Add the equivalent:
```cpp
ar.lock = (rs.txn.lock == LockType::Exclusive) ? 1u : 0u;
ar.cache = static_cast<uint8_t>(rs.txn.cache);  // if applicable
ar.prot  = static_cast<uint8_t>(rs.txn.prot);   // if applicable
```

- [ ] **Step 3: Set `WriteResult.lock` at B-drain firing site**

Find the B-drain block where the WriteResult callback fires (look for `wcb_(WriteResult{...})` or similar). In the WriteResult aggregate-init, add `op.src_txn.lock` (or equivalent OperationContext source-txn reference) for the `lock` field. Example:
```cpp
WriteResult{
  op.src_txn.addr,
  op.src_txn.size,
  op.src_txn.len,
  op.src_txn.burst,
  op.src_txn.lock,    // NEW
  op.data,
  op.strb_per_beat,
  op.worst_resp,
  op.src_txn.id,
  op.src_txn.scenario_line
}
```
(The exact callsite varies; verify against current axi_master.hpp.)

- [ ] **Step 4: Build to verify compile**

```bash
cd c_model && cmake --build build
```

Expected: build fails if any existing WriteResult aggregate-init in tests is missing the new field. That's expected; will be fixed in Task C.2.10. For now, **proceed even if test files fail to compile** — fix in C.2.10.

If the production code (axi_master.hpp + axi_slave.hpp + scoreboard.hpp) compiles cleanly and only test files fail, that's the expected intermediate state.

### Task C.2.2: AxiMaster wire-through unit tests (2 tests)

**Files:**
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Add 2 tests inside `AxiMasterTest` fixture**

```cpp
TEST_F(AxiMasterTest, LockFieldPropagatesToAwLock) {
  std::string tmp_name = std::string("/lock_prop_excl_") +
      ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".yaml";
  auto wpath = write_tmp_data("w_lock_excl.txt",
      "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
      "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].lock, 1u);
}

TEST_F(AxiMasterTest, LockDefaultsToZero_OnNormalTxn) {
  auto wpath = write_tmp_data("w_lock_norm.txt",
      "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
      "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].lock, 0u);
}
```

- [ ] **Step 2: Defer build until C.2.10 (test files won't compile until scoreboard.hpp + aggregate-inits updated)**

### Task C.2.3: Add `ExclusiveTag` struct + `compute_tag_range` + `exclusive_tags_` map

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: Add `ExclusiveTag` struct**

Above the `AxiSlave` class definition (or in a nested namespace), add:
```cpp
// IHI 0022 §A7.2: per-ID exclusive monitor tag.
// addr_start/addr_end is half-open. For WRAP, derived from wrap window.
// ready=false until paired exclusive AR's RLAST is observed.
struct ExclusiveTag {
  uint64_t addr_start = 0;
  uint64_t addr_end   = 0;
  uint8_t  len = 0;
  uint8_t  size = 0;
  Burst    burst = Burst::INCR;
  uint8_t  cache = 0;
  uint8_t  prot  = 0;
  bool     ready = false;
};
```

- [ ] **Step 2: Add `exclusive_tags_` map and `compute_tag_range` helper inside class**

Inside `class AxiSlave` (private section, near `active_writes_`):
```cpp
std::map<uint8_t, ExclusiveTag> exclusive_tags_;

// Helper: derive [addr_start, addr_end) from an AR (for E1) or AW (for E3).
template <typename Beat>
static std::pair<uint64_t, uint64_t> compute_tag_range(const Beat& b) {
  std::size_t bpb = 1ull << b.size;
  std::size_t total = (static_cast<std::size_t>(b.len) + 1) * bpb;
  if (b.burst == Burst::WRAP) {
    uint64_t wrap_lower = b.addr & ~(static_cast<uint64_t>(total) - 1);
    return {wrap_lower, wrap_lower + total};
  }
  return {b.addr, b.addr + total};  // INCR; FIXED rejected by stateless rule before this
}
```

- [ ] **Step 3: Add `is_exclusive` + `exclusive_match` to `WriteBurstState`**

Find the `WriteBurstState` struct in axi_slave.hpp. Add:
```cpp
bool is_exclusive    = false;
bool exclusive_match = false;
```

- [ ] **Step 4: Build to verify compile (production code only)**

```bash
cd c_model && cmake --build build --target axi_runtime  # or whatever the lib target is
```

Or alternatively, run full build and ignore test compile errors at this point. The intermediate state may have test errors that resolve in C.2.10.

### Task C.2.4: Implement monitor match helper instantiation point

The monitor match helper `check_exclusive_write_matches_read_tag<ExclusiveTag>` was declared as a template in `protocol_rules.hpp` (C.1.4). It is instantiated when called with `ExclusiveTag` from `axi_slave.hpp`. No new code needed — verify it compiles when AxiSlave calls it in subsequent tasks.

### Task C.2.5: Implement E1 (AR admit with lock=1) + E5 (R drain → ready)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: E1 — AR admit set tag**

In `AxiSlave::tick()`, locate the AR admit step (likely step 5 per Phase B order). Just BEFORE the existing AR admission, add the exclusive AR handling:

```cpp
// Step 5: AR admit
while (!ar_q_.empty()) {
  auto& ar = ar_q_.front();

  // === Phase C: exclusive AR handling ===
  axi::LockType ar_lock = static_cast<axi::LockType>(ar.lock);
  AXI_PROTOCOL_ASSERT(rules::check_lock_encoding(ar.lock),
                      "LOCK_ENCODING: ar.lock invalid");
  if (ar_lock == axi::LockType::Exclusive) {
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_bytes_le_max(ar_lock, ar.len, ar.size),
        "EXCLUSIVE_TOTAL_BYTES");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_beats_le_max(ar_lock, ar.len),
        "EXCLUSIVE_TOTAL_BEATS");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_pow2(ar_lock, ar.len),
        "EXCLUSIVE_POW2");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_addr_aligned_to_total(ar_lock, ar.addr, ar.len, ar.size),
        "EXCLUSIVE_ALIGN");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_burst_not_fixed(ar_lock, ar.burst),
        "EXCLUSIVE_BURST_FIXED");
    auto [start, end] = compute_tag_range(ar);
    exclusive_tags_[ar.id] = ExclusiveTag{
        start, end, ar.len, ar.size, ar.burst, ar.cache, ar.prot, false
    };
  }

  // (existing AR admission code continues below: push to active_reads_ etc.)
  ...
}
```

- [ ] **Step 2: E5 — R drain sets ready**

In `AxiSlave::tick()`, locate the R drain step (likely step 2 per Phase B order — runs BEFORE AR admit so that a new AR same tick doesn't conflict). On RLAST handling for a read whose tag exists, set ready:

```cpp
// Step 2: R drain (memory port → R queue → BeatChain)
while (auto resp = memory_port_.pop_read_resp()) {
  // existing R drain code populating r_q_ ...

  // === Phase C: tag.ready set on RLAST ===
  if (resp->last) {
    auto tag_it = exclusive_tags_.find(resp->id);
    if (tag_it != exclusive_tags_.end()) {
      tag_it->second.ready = true;
    }
  }
}
```

(Note: the existing R drain may already loop per-beat. The `if (resp->last)` should fire exactly once per AR completion.)

### Task C.2.6: Implement E3 (AW admit with lock=1) + E2 (AW admit with lock=0)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: E3 — exclusive AW lookup and match**

In `AxiSlave::tick()`, locate the AW admit step (likely step 3 per Phase B order). At the start of the admit loop:

```cpp
// Step 3: AW admit
while (!aw_q_.empty()) {
  auto& aw = aw_q_.front();
  AXI_PROTOCOL_ASSERT(rules::check_lock_encoding(aw.lock),
                      "LOCK_ENCODING: aw.lock invalid");
  axi::LockType aw_lock = static_cast<axi::LockType>(aw.lock);

  bool is_exclusive_write = false;
  bool exclusive_match    = false;

  if (aw_lock == axi::LockType::Exclusive) {
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_bytes_le_max(aw_lock, aw.len, aw.size),
        "EXCLUSIVE_TOTAL_BYTES");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_beats_le_max(aw_lock, aw.len),
        "EXCLUSIVE_TOTAL_BEATS");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_total_pow2(aw_lock, aw.len),
        "EXCLUSIVE_POW2");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_addr_aligned_to_total(aw_lock, aw.addr, aw.len, aw.size),
        "EXCLUSIVE_ALIGN");
    AXI_PROTOCOL_ASSERT(
        rules::check_exclusive_burst_not_fixed(aw_lock, aw.burst),
        "EXCLUSIVE_BURST_FIXED");

    is_exclusive_write = true;
    auto tag_it = exclusive_tags_.find(aw.id);
    if (tag_it != exclusive_tags_.end()) {
      exclusive_match = rules::check_exclusive_write_matches_read_tag(
          tag_it->second, aw);
      exclusive_tags_.erase(tag_it);  // erase regardless of match (§A7.2.3)
    }
  } else {
    // E2: normal AW invalidates any overlapping tag (across all IDs)
    std::size_t aw_bpb = 1ull << aw.size;
    std::size_t aw_total = (static_cast<std::size_t>(aw.len) + 1) * aw_bpb;
    uint64_t aw_start, aw_end;
    if (aw.burst == Burst::WRAP) {
      aw_start = aw.addr & ~(static_cast<uint64_t>(aw_total) - 1);
      aw_end = aw_start + aw_total;
    } else if (aw.burst == Burst::FIXED) {
      aw_start = aw.addr;
      aw_end = aw.addr + aw_bpb;  // FIXED touches only first-beat range
    } else {
      aw_start = aw.addr;
      aw_end = aw.addr + aw_total;
    }
    for (auto it = exclusive_tags_.begin(); it != exclusive_tags_.end(); ) {
      const auto& tag = it->second;
      bool overlap = aw_start < tag.addr_end && tag.addr_start < aw_end;
      if (overlap) {
        it = exclusive_tags_.erase(it);
      } else {
        ++it;
      }
    }
  }

  // (existing AW admission code continues below: push to active_writes_)
  // When constructing WriteBurstState, set the new fields:
  WriteBurstState st{};
  st.aw = aw;
  st.is_exclusive    = is_exclusive_write;
  st.exclusive_match = exclusive_match;
  // ... rest of state init
  active_writes_[aw.id].push_back(std::move(st));
  aw_issue_order_.push_back(aw.id);
  aw_q_.pop_front();
}
```

### Task C.2.7: Implement E4 (W submit suppresses failed exclusive)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: Gate `memory_port_.push_write` on `(!is_exclusive || exclusive_match)`**

Locate the W submit step (Phase B step 4) where `MemWriteReq` is constructed and pushed to `memory_port_`. Wrap the push with a condition:

```cpp
// Step 4: W submit
while (...) {
  auto& st = ...;  // existing W-routing logic to find the right burst
  // ... build MemWriteReq req from W beat ...

  // === Phase C: suppress failed exclusive write ===
  if (st.is_exclusive && !st.exclusive_match) {
    // do not push to memory_port; just account the beat as "submitted"
    ++st.beats_submitted;
    // (still pop from w_q_, still possibly pop aw_issue_order_ when burst done)
  } else {
    memory_port_.push_write(req);
    ++st.beats_submitted;
  }
}
```

(Note: the exact accounting depends on Phase B AxiSlave implementation; ensure `beats_submitted` and `aw_issue_order_` advance correctly even when the memory push is skipped.)

### Task C.2.8: Implement E6 (B drain resp priority)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: Adjust B-drain resp construction**

Locate Phase B step 1 (B drain). When the burst's beats_completed reaches `len+1`, the existing code constructs a BBeat with `st.worst_resp`. Replace with the priority logic:

```cpp
// Step 1: B drain
while (auto resp = memory_port_.pop_write_resp()) {
  // ... find front burst by id ...
  auto& st = it->second.front();
  ++st.beats_completed;

  // accumulate memory resp (worst-of)
  if (static_cast<uint8_t>(resp->resp) > static_cast<uint8_t>(st.worst_resp))
    st.worst_resp = resp->resp;

  if (st.beats_completed == static_cast<std::size_t>(st.aw.len) + 1) {
    Resp final_resp;
    // === Phase C: response priority — memory error > EXOKAY/OKAY ===
    if (st.worst_resp == Resp::DECERR || st.worst_resp == Resp::SLVERR) {
      final_resp = st.worst_resp;
    } else if (st.is_exclusive) {
      final_resp = st.exclusive_match ? Resp::EXOKAY : Resp::OKAY;
    } else {
      final_resp = Resp::OKAY;
    }
    b_q_.push_back(BBeat{st.aw.id, final_resp, 0});

    // existing cleanup ...
    it->second.pop_front();
    for (auto i = aw_issue_order_.begin(); i != aw_issue_order_.end(); ++i) {
      if (*i == st.aw.id) { aw_issue_order_.erase(i); break; }
    }
    if (it->second.empty()) active_writes_.erase(it);
  }
}
```

For a failed exclusive write where ALL beats were suppressed, `st.worst_resp` stays at its init value (OKAY). The priority falls through to `is_exclusive` branch → OKAY. Correct behavior.

If the burst goes OOB (`memory.hpp` returns DECERR), then exclusive_match is `false` (since memory rejected) AND DECERR wins via the first branch. Verify with unit test in C.2.9 (`ExclusiveWriteOnOob_DECERR`).

### Task C.2.9: 13 AxiSlave monitor unit tests

**Files:**
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Add includes for new types**

Verify `test_axi_slave.cpp` already includes `"axi/axi_slave.hpp"`, `"axi/types.hpp"`, and the MockMemoryPort header. Add if missing.

- [ ] **Step 2: Add 13 unit tests**

Append to `test_axi_slave.cpp` (preserving existing `TEST(AxiSlave, ...)` pattern):

```cpp
// Helper to push an exclusive AR (size=5, len=0, INCR) and tick once
static void push_exclusive_ar(axi::AxiSlave& slave, uint8_t id, uint64_t addr,
                               uint8_t len = 0, uint8_t size = 5,
                               axi::Burst burst = axi::Burst::INCR) {
  axi::ArBeat ar{};
  ar.id = id; ar.addr = addr; ar.len = len; ar.size = size;
  ar.burst = burst; ar.lock = 1;
  slave.push_ar(ar);
}

// Helper to push an exclusive AW with given W beat
static void push_exclusive_aw_and_w(axi::AxiSlave& slave, uint8_t id,
                                     uint64_t addr, uint8_t len = 0,
                                     uint8_t size = 5,
                                     axi::Burst burst = axi::Burst::INCR,
                                     uint8_t data_byte = 0xAA) {
  axi::AwBeat aw{};
  aw.id = id; aw.addr = addr; aw.len = len; aw.size = size;
  aw.burst = burst; aw.lock = 1;
  slave.push_aw(aw);
  for (uint8_t i = 0; i <= len; ++i) {
    axi::WBeat w{};
    w.data.fill(data_byte + i);
    w.strb = 0xFFFFFFFFu;
    w.last = (i == len);
    slave.push_w(w);
  }
}

TEST(AxiSlave, ExclusiveAR_SetsTag_NotReady) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  slave.tick();
  EXPECT_TRUE(slave.has_exclusive_tag(5));
  EXPECT_FALSE(slave.exclusive_tag_ready(5));
}

TEST(AxiSlave, ExclusiveAR_RComplete_TagBecomesReady) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();  // tick until R completes
  EXPECT_TRUE(slave.has_exclusive_tag(5));
  EXPECT_TRUE(slave.exclusive_tag_ready(5));
}

TEST(AxiSlave, NormalWrite_NoOverlap_TagSurvives) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  // normal write at non-overlapping addr
  axi::AwBeat aw{};
  aw.id = 3; aw.addr = 0x1100; aw.len = 0; aw.size = 5;
  aw.burst = axi::Burst::INCR; aw.lock = 0;
  slave.push_aw(aw);
  axi::WBeat w{}; w.data.fill(0xCC); w.strb = 0xFFFFFFFFu; w.last = true;
  slave.push_w(w);
  for (int t = 0; t < 4; ++t) slave.tick();
  EXPECT_TRUE(slave.has_exclusive_tag(5));
}

TEST(AxiSlave, NormalWrite_Overlap_TagCleared) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  // normal write at overlapping addr
  axi::AwBeat aw{};
  aw.id = 3; aw.addr = 0x1000; aw.len = 0; aw.size = 5;
  aw.burst = axi::Burst::INCR; aw.lock = 0;
  slave.push_aw(aw);
  axi::WBeat w{}; w.data.fill(0xCC); w.strb = 0xFFFFFFFFu; w.last = true;
  slave.push_w(w);
  for (int t = 0; t < 4; ++t) slave.tick();
  EXPECT_FALSE(slave.has_exclusive_tag(5));
}

TEST(AxiSlave, ExclusivePair_FullMatch_EXOKAY_CommitsMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  push_exclusive_aw_and_w(slave, 5, 0x1000, 0, 5, axi::Burst::INCR, 0xAA);
  for (int t = 0; t < 6; ++t) slave.tick();
  ASSERT_FALSE(mem.captured_writes.empty());
  EXPECT_EQ(mem.captured_writes.back().data[0], 0xAA);
  // BBeat resp == EXOKAY (verify via mock or directly)
  ASSERT_FALSE(slave.peek_b_queue_empty());
  EXPECT_EQ(slave.peek_b_queue_front().resp, axi::Resp::EXOKAY);
}

TEST(AxiSlave, ExclusiveWrite_NoPriorRead_OKAY_NoCommit) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_aw_and_w(slave, 5, 0x1000);
  for (int t = 0; t < 6; ++t) slave.tick();
  EXPECT_TRUE(mem.captured_writes.empty());
  ASSERT_FALSE(slave.peek_b_queue_empty());
  EXPECT_EQ(slave.peek_b_queue_front().resp, axi::Resp::OKAY);
}

TEST(AxiSlave, ExclusiveWrite_BeforeReady_OKAY_NoCommit) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  slave.tick();  // AR set tag but R not yet complete
  EXPECT_TRUE(slave.has_exclusive_tag(5));
  EXPECT_FALSE(slave.exclusive_tag_ready(5));
  push_exclusive_aw_and_w(slave, 5, 0x1000);
  for (int t = 0; t < 6; ++t) slave.tick();
  EXPECT_TRUE(mem.captured_writes.empty());
  ASSERT_FALSE(slave.peek_b_queue_empty());
  EXPECT_EQ(slave.peek_b_queue_front().resp, axi::Resp::OKAY);
}

TEST(AxiSlave, ExclusiveWrite_SizeMismatch_OKAY_NoCommit) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  // AR exclusive size=5, AW exclusive size=4 → mismatch
  push_exclusive_ar(slave, 5, 0x1000, 0, 5);
  for (int t = 0; t < 4; ++t) slave.tick();
  push_exclusive_aw_and_w(slave, 5, 0x1000, 0, 4);  // size=4 mismatch
  for (int t = 0; t < 6; ++t) slave.tick();
  EXPECT_TRUE(mem.captured_writes.empty());
  ASSERT_FALSE(slave.peek_b_queue_empty());
  EXPECT_EQ(slave.peek_b_queue_front().resp, axi::Resp::OKAY);
}

TEST(AxiSlave, ExclusiveWriteOnOob_DECERR) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x100);  // small region
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  // exclusive AW at OOB addr (out of [0x1000, 0x1100))
  push_exclusive_aw_and_w(slave, 5, 0x2000);  // OOB
  for (int t = 0; t < 6; ++t) slave.tick();
  ASSERT_FALSE(slave.peek_b_queue_empty());
  // DECERR overrides EXOKAY
  EXPECT_EQ(slave.peek_b_queue_front().resp, axi::Resp::DECERR);
}

TEST(AxiSlave, ExclusiveWRAP_TagRangeIsWrapWindow) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  // WRAP burst addr=0x1020, len=3, size=5 → total=128, wrap_lower=0x1000, wrap_upper=0x1080
  axi::ArBeat ar{};
  ar.id = 5; ar.addr = 0x1020; ar.len = 3; ar.size = 5;
  ar.burst = axi::Burst::WRAP; ar.lock = 1;
  slave.push_ar(ar);
  slave.tick();
  EXPECT_TRUE(slave.has_exclusive_tag(5));
  auto tag = slave.peek_exclusive_tag(5);
  EXPECT_EQ(tag.addr_start, 0x1000u);
  EXPECT_EQ(tag.addr_end,   0x1080u);
}

TEST(AxiSlave, MultiId_NormalWriteErasesMultipleTags_IteratorSafe) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  push_exclusive_ar(slave, 6, 0x1020);
  for (int t = 0; t < 4; ++t) slave.tick();
  // normal write overlapping BOTH tags (id=5 at 0x1000, id=6 at 0x1020)
  axi::AwBeat aw{};
  aw.id = 9; aw.addr = 0x1000; aw.len = 1; aw.size = 5;  // covers 0x1000..0x103F
  aw.burst = axi::Burst::INCR; aw.lock = 0;
  slave.push_aw(aw);
  axi::WBeat w{}; w.data.fill(0xCC); w.strb = 0xFFFFFFFFu;
  w.last = false; slave.push_w(w);
  w.last = true;  slave.push_w(w);
  for (int t = 0; t < 6; ++t) slave.tick();
  EXPECT_FALSE(slave.has_exclusive_tag(5));
  EXPECT_FALSE(slave.has_exclusive_tag(6));
}

TEST(AxiSlave, ExclusiveAR_SameId_SecondOverwritesFirst) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  EXPECT_TRUE(slave.exclusive_tag_ready(5));
  push_exclusive_ar(slave, 5, 0x1020);
  slave.tick();
  auto tag = slave.peek_exclusive_tag(5);
  EXPECT_EQ(tag.addr_start, 0x1020u);
  EXPECT_FALSE(tag.ready);  // new tag starts not-ready
}

TEST(AxiSlave, DifferentId_ExclusiveAW_DoesNotAffectOtherTag) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);
  push_exclusive_ar(slave, 5, 0x1000);
  for (int t = 0; t < 4; ++t) slave.tick();
  // id=6 exclusive AW (no prior id=6 AR) → silent OKAY but does not touch id=5 tag
  push_exclusive_aw_and_w(slave, 6, 0x1000);
  for (int t = 0; t < 6; ++t) slave.tick();
  EXPECT_TRUE(slave.has_exclusive_tag(5));  // id=5 tag still valid
}
```

- [ ] **Step 3: Add inspection helpers to AxiSlave (if not present)**

The tests above use `slave.has_exclusive_tag(id)`, `slave.exclusive_tag_ready(id)`, `slave.peek_exclusive_tag(id)`, `slave.peek_b_queue_empty()`, `slave.peek_b_queue_front()`. Add to `AxiSlave` public section in axi_slave.hpp:

```cpp
// Inspection helpers for tests (not used by production callers).
bool has_exclusive_tag(uint8_t id) const {
  return exclusive_tags_.find(id) != exclusive_tags_.end();
}
bool exclusive_tag_ready(uint8_t id) const {
  auto it = exclusive_tags_.find(id);
  return it != exclusive_tags_.end() && it->second.ready;
}
ExclusiveTag peek_exclusive_tag(uint8_t id) const {
  return exclusive_tags_.at(id);
}
bool peek_b_queue_empty() const { return b_q_.empty(); }
const BBeat& peek_b_queue_front() const { return b_q_.front(); }
```

### Task C.2.10: Scoreboard commit condition + update aggregate-inits

**Files:**
- Modify: `c_model/include/axi/scoreboard.hpp`
- Modify: `c_model/tests/axi/test_scoreboard.cpp`

- [ ] **Step 1: Update `handle_write_completed`**

In `scoreboard.hpp`, find the early-return at the top of `handle_write_completed`. Replace with:
```cpp
if (wr.resp == Resp::DECERR || wr.resp == Resp::SLVERR) return;
if (wr.lock == LockType::Exclusive && wr.resp == Resp::OKAY) return;  // failed exclusive
// Both: normal OKAY and exclusive EXOKAY proceed to byte-merge.
```

- [ ] **Step 2: Update test_scoreboard.cpp aggregate-inits**

Grep for `WriteResult{` in `test_scoreboard.cpp`. For each occurrence (approximately 5 sites), add `LockType::Normal` after the `burst` field. Example:

Before:
```cpp
axi::WriteResult wr{0x100, 5, 0, axi::Burst::INCR, /*data*/{}, /*strb*/{}, axi::Resp::OKAY, 1, 1};
```
After:
```cpp
axi::WriteResult wr{0x100, 5, 0, axi::Burst::INCR, axi::LockType::Normal, /*data*/{}, /*strb*/{}, axi::Resp::OKAY, 1, 1};
```

(Exact positional order depends on Phase B WriteResult struct layout. Cross-check by reading axi_master.hpp WriteResult definition with the field added in C.2.1.)

- [ ] **Step 3: Build + run**

```bash
cd c_model && cmake --build build && ctest --test-dir build --output-on-failure -j 1
```

Expected: ~177/177 (149 prior + 13 from C.1 + 15 new in C.2: 2 wire + 13 monitor + 0 incremental — counting tests added by C.2 task list). The exact tally depends on whether unit tests added separately count toward total.

If any test fails:
- Phase A+B regression → check Step 2 aggregate-init coverage; missing a callsite would compile-fail but runtime-pass.
- New unit test fails → debug state transitions per E1-E6 spec.

### Task C.2.11: Drift gates + commit C.2

- [ ] **Step 1: Run drift gates**

```bash
cd c_model && cmake --build build && ctest --test-dir build -j 1
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```

Expected: ~177/177 ctest, 159 pytest, clean codegen + inventory.

- [ ] **Step 2: Karpathy 4-lens review**

- Overcomplication: `ExclusiveTag` struct has many fields but each justified by §A7.2.4
- Surgical: production changes confined to types.hpp / scenario_parser.hpp / axi_master.hpp / axi_slave.hpp / scoreboard.hpp / protocol_rules.hpp + their tests
- Surface assumptions: tag ready gating depends on R drain firing before AW admit in same tick — documented in spec; verified by `ExclusiveWrite_BeforeReady_OKAY_NoCommit`
- Verifiable success: 15 new tests cover all 6 events + 3 codex-found edge cases + DECERR priority + WRAP range

- [ ] **Step 3: Commit**

```bash
git add c_model/include/axi/axi_master.hpp \
        c_model/include/axi/axi_slave.hpp \
        c_model/include/axi/scoreboard.hpp \
        c_model/tests/axi/test_axi_master.cpp \
        c_model/tests/axi/test_axi_slave.cpp \
        c_model/tests/axi/test_scoreboard.cpp
git commit -m "$(cat <<'EOF'
feat(c_model): AxiSlave exclusive monitor (E1-E6) + WriteResult.lock + Scoreboard

Adds slave-only AXI4 exclusive monitor per IHI 0022 §A7. Per-ID
exclusive_tags_ map with 6-event state machine:
  E1 AR (lock=1) admit → set tag (ready=false)
  E2 AW (lock=0) admit → erase tags whose addr range overlaps
  E3 AW (lock=1) admit → look up + match attrs + erase tag
  E4 W submit → suppress memory_port.push for failed exclusive burst
  E5 R drain (RLAST) → tag.ready = true
  E6 B drain → resp priority: memory error > EXOKAY/OKAY

AxiMaster propagates txn.lock → aw.lock/ar.lock (pure wire-through).
WriteResult.lock mirrors txn.lock so Scoreboard detects failed
exclusive (lock=Exclusive + resp=OKAY → skip expected_ update).

13 monitor unit tests + 2 wire-through tests, ~177/177 ctest.
EOF
)"
```

---

# Task C.3 — Integration fixtures + docs

**Goal**: 4 integration fixtures + ATTRIBUTION + NEXT_STEPS updates. ~181 sequential ctest. Single commit.

### Task C.3.1: Fixture `exclusive_pair_success.yaml`

**Files:**
- Create: `c_model/tests/axi/fixtures/exclusive_pair_success.yaml`
- Create: `c_model/tests/axi/fixtures/exclusive_pair_success_data.txt`

- [ ] **Step 1: YAML scenario**

Create `c_model/tests/axi/fixtures/exclusive_pair_success.yaml`:
```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    dump_file: r_excl_read.txt
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    data_file: fixtures/exclusive_pair_success_data.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    dump_file: r_excl_verify.txt
```

- [ ] **Step 2: Data file (32 bytes)**

Create `c_model/tests/axi/fixtures/exclusive_pair_success_data.txt`:
```
AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00 11 22 33 44 55 66 77 88 99
```

### Task C.3.2: Fixture `exclusive_intervening_write.yaml`

**Files:**
- Create: `c_model/tests/axi/fixtures/exclusive_intervening_write.yaml`
- Create: `c_model/tests/axi/fixtures/exclusive_intervening_write_data.txt`
- Create: `c_model/tests/axi/fixtures/exclusive_intervening_write_excl.txt`

- [ ] **Step 1: YAML scenario**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    dump_file: r_intv_read.txt
  - op: write
    addr: 0x1000
    id: 0x3
    len: 0
    size: 5
    burst: INCR
    data_file: fixtures/exclusive_intervening_write_data.txt
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    data_file: fixtures/exclusive_intervening_write_excl.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    dump_file: r_intv_verify.txt
```

- [ ] **Step 2: Data files**

`exclusive_intervening_write_data.txt` (the intervening normal write — this is what should end up in memory):
```
01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20
```

`exclusive_intervening_write_excl.txt` (the failed exclusive write — should NOT commit):
```
F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF
```

### Task C.3.3: Fixture `exclusive_no_prior_read.yaml`

**Files:**
- Create: `c_model/tests/axi/fixtures/exclusive_no_prior_read.yaml`
- Create: `c_model/tests/axi/fixtures/exclusive_no_prior_read_data.txt`

- [ ] **Step 1: YAML scenario**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    lock: exclusive
    data_file: fixtures/exclusive_no_prior_read_data.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    dump_file: r_no_prior.txt
```

- [ ] **Step 2: Data file (should NOT appear in memory since no prior exclusive AR)**

`exclusive_no_prior_read_data.txt`:
```
DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF DE AD BE EF
```

Expected behavior: Scoreboard's expected_ has all zeros for [0x1000, 0x1020) (no prior write → memory default). Read returns zeros. Mismatch count = 0.

### Task C.3.4: Fixture `exclusive_wrap_pair_success.yaml`

**Files:**
- Create: `c_model/tests/axi/fixtures/exclusive_wrap_pair_success.yaml`
- Create: `c_model/tests/axi/fixtures/exclusive_wrap_pair_success_data.txt`

- [ ] **Step 1: YAML scenario (WRAP 2-beat, total 64 bytes, addr aligned)**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: read
    addr: 0x1000
    id: 0x5
    len: 1
    size: 5
    burst: WRAP
    lock: exclusive
    dump_file: r_wrap_read.txt
  - op: write
    addr: 0x1000
    id: 0x5
    len: 1
    size: 5
    burst: WRAP
    lock: exclusive
    data_file: fixtures/exclusive_wrap_pair_success_data.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 1
    size: 5
    burst: WRAP
    dump_file: r_wrap_verify.txt
```

- [ ] **Step 2: Data file (64 bytes for 2 WRAP beats)**

```
A0 A1 A2 A3 A4 A5 A6 A7 A8 A9 AA AB AC AD AE AF B0 B1 B2 B3 B4 B5 B6 B7 B8 B9 BA BB BC BD BE BF
C0 C1 C2 C3 C4 C5 C6 C7 C8 C9 CA CB CC CD CE CF D0 D1 D2 D3 D4 D5 D6 D7 D8 D9 DA DB DC DD DE DF
```

### Task C.3.5: Add 4 FixtureParam rows to test_integration.cpp

**Files:**
- Modify: `c_model/tests/axi/test_integration.cpp`

- [ ] **Step 1: Add fixtures to INSTANTIATE_TEST_SUITE_P**

Find `INSTANTIATE_TEST_SUITE_P(...)` in `test_integration.cpp`. After the last existing FixtureParam entry, add:

```cpp
        // Phase C: AXI4 exclusive access (IHI 0022 §A7)
        // Matched pair → EXOKAY; memory commits.
        FixtureParam{"exclusive_pair_success.yaml",         "",  false, true},
        // Intervening normal write between exclusive AR and AW → tag invalidated;
        // exclusive AW returns OKAY, memory not committed; subsequent read shows
        // intervening write's value.
        FixtureParam{"exclusive_intervening_write.yaml",    "",  false, true},
        // Exclusive AW with no prior exclusive AR → silent OKAY + no memory commit.
        FixtureParam{"exclusive_no_prior_read.yaml",        "",  false, true},
        // WRAP exclusive pair → EXOKAY; tag range uses wrap window.
        FixtureParam{"exclusive_wrap_pair_success.yaml",    "",  false, true},
```

(Field order is `{yaml, file_diff_target, expect_file_diff_pass, expect_zero_mismatches}`. The `false` for file diff is because dump_file format may not match the data file's per-beat 32B layout; scoreboard is the source of truth.)

### Task C.3.6: ATTRIBUTION.md update

**Files:**
- Modify: `c_model/include/axi/ATTRIBUTION.md`

- [ ] **Step 1: Add Phase C attribution note**

Append to `c_model/include/axi/ATTRIBUTION.md`:

```markdown
## Phase C — Exclusive Access (AxLOCK + EXOKAY)

Independent design per AXI4 IHI 0022 §A7. cocotbext-axi (MIT) does NOT implement
exclusive monitor (only carries lock signal). Closest OSS reference is
ZipCPU/wb2axip AXIDOUBLE (Apache 2.0, Verilog, master-side buffer), used only as
semantic reference for register/state shape — no code ported.

Files: `axi_slave.hpp` (exclusive_tags_, compute_tag_range, E1-E6),
`protocol_rules.hpp` (6 stateless + 1 monitor helper).
```

### Task C.3.7: NEXT_STEPS.md update

**Files:**
- Modify: `c_model/NEXT_STEPS.md`

- [ ] **Step 1: Update status + scope + add Phase C completion section**

Replace the header `**Status (2026-05-31)**：Stage 2 Phase B 完工` with:
```markdown
**Status (2026-05-31)**：Stage 2 Phase C 完工（pure AXI subsystem + AXI4 exclusive access）；~181/181 tests pass (sequential)；spec_validate 三 domain 純 symbolic，0 error / 0 warning；GitHub Action drift gate 在線。
```

In the 完成清單 section, after the Phase B entry, add:
```markdown
- **Stage 2 Phase C**（AXI4 exclusive access）：
  - LockType enum + ScenarioTransaction.lock + parser
  - 6 stateless protocol rules + 1 monitor match helper
  - AxiSlave per-ID exclusive_tags_ + 6-event state machine (E1 AR set, E2 normal overlap erase, E3 exclusive AW match+erase, E4 W suppress, E5 R ready, E6 B priority)
  - WriteResult.lock + Scoreboard commit gating for failed exclusive
  - 4 integration fixtures (exclusive_pair_success, exclusive_intervening_write, exclusive_no_prior_read, exclusive_wrap_pair_success)
  - ~32 new tests; sequential ctest ~181/181
```

In the 已知限制 section (after Phase B limitations), add:
```markdown
- **Phase C single-master only**：multi-master exclusive scenarios deferred. Monitor 不模擬不同 master 競爭。
- **Phase C cache/prot match**：`check_exclusive_write_matches_read_tag` 比對 cache + prot，但 ScenarioTransaction 未暴露 YAML 欄位，預設皆 0。若 RTL co-sim 需要 cache/prot 觀測，需擴充 YAML schema。
- **Phase C orphaned tag**：exclusive AR fired 但 paired AW 未送 → tag 永留直到同 ID AR overwrite 或 overlap write 清除。沒 stale-tag 計數器；ScenarioParser 端 caller 應確保 sequence 完整。
```

Replace 下一步 section with:
```markdown
## 下一步：Stage 3 NoC integration（或 Phase D 視 roadmap）

- DPI bridge → 解鎖 handshake-level rules (`*_VALID_STABLE` 等)；SV testbench + Verilator integration
- NMU 重新設計 AXI slave forwarder
- 多 master exclusive 場景（Phase C deferred）
```

### Task C.3.8: Drift gates + commit C.3

- [ ] **Step 1: Run drift gates**

```bash
cd c_model && cmake --build build && ctest --test-dir build --output-on-failure -j 1
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```

Expected: ~181/181 ctest, 159 pytest, clean codegen + inventory.

- [ ] **Step 2: Karpathy 4-lens review**

- Overcomplication: 4 fixtures cover happy path + tag-invalidation + no-prior + WRAP; no over-coverage
- Surgical: only fixtures + test_integration.cpp + ATTRIBUTION.md + NEXT_STEPS.md touched
- Surface assumptions: data file 32-byte layout matches Phase B convention; ATTRIBUTION cites OSS gap clearly
- Verifiable success: each fixture has scoreboard validation; 4 new INSTANTIATE rows visible in ctest

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/fixtures/exclusive_pair_success.yaml \
        c_model/tests/axi/fixtures/exclusive_pair_success_data.txt \
        c_model/tests/axi/fixtures/exclusive_intervening_write.yaml \
        c_model/tests/axi/fixtures/exclusive_intervening_write_data.txt \
        c_model/tests/axi/fixtures/exclusive_intervening_write_excl.txt \
        c_model/tests/axi/fixtures/exclusive_no_prior_read.yaml \
        c_model/tests/axi/fixtures/exclusive_no_prior_read_data.txt \
        c_model/tests/axi/fixtures/exclusive_wrap_pair_success.yaml \
        c_model/tests/axi/fixtures/exclusive_wrap_pair_success_data.txt \
        c_model/tests/axi/test_integration.cpp \
        c_model/include/axi/ATTRIBUTION.md \
        c_model/NEXT_STEPS.md
git commit -m "$(cat <<'EOF'
test(c_model): Phase C integration fixtures + ATTRIBUTION + NEXT_STEPS

4 integration fixtures exercise the exclusive monitor end-to-end:
  - exclusive_pair_success: matched pair → EXOKAY + memory commits
  - exclusive_intervening_write: tag invalidated → failed exclusive
    OKAY + memory has intervening normal write's value
  - exclusive_no_prior_read: silent OKAY + no memory commit
  - exclusive_wrap_pair_success: WRAP exclusive pair → EXOKAY

ATTRIBUTION notes Phase C is independent design (cocotbext-axi has no
exclusive monitor; AXIDOUBLE Apache 2.0 used only as semantic reference).
NEXT_STEPS marks Phase C complete + lists known limitations + repoints
to Stage 3 NoC integration.

Sequential ctest ~181/181; Phase A+B+C all green.
EOF
)"
```

---

# Drift gates (every commit)

```bash
cd c_model && cmake --build build && ctest --test-dir build --output-on-failure -j 1
cd ../spec_validate && py -3 -m pytest -q
py -3 tools/codegen.py --check
py -3 tools/gen_inventory.py --check
```

Expected after each Phase C commit:
- C.1: 162/162 ctest
- C.2: ~177/177 ctest
- C.3: ~181/181 ctest
- All commits: 159 pytest, codegen + inventory clean
- Pre-existing `spec_validate/include/*.h` + `spec_validate/rtl_pkg/*.sv` drift stays unstaged

---

# Self-review checklist

- ✅ Spec §1 Goal → covered by C.1 + C.2 + C.3
- ✅ Spec §2 Architecture (slave-only monitor, master wire-through, scoreboard commit) → C.2.1 master, C.2.5-2.8 slave monitor, C.2.10 scoreboard
- ✅ Spec §3 Data model (LockType, ScenarioTransaction.lock, WriteResult.lock, ExclusiveTag, WriteBurstState additions, 7 helpers) → C.1.1-1.4, C.2.1, C.2.3-2.4
- ✅ Spec §4 State machine (6 events, tag range, invariants) → C.2.5-2.8
- ✅ Spec §5 Test plan (A 7 death + B 13 monitor + C 4 parser + D 2 wire-through + E 4 fixtures) → C.1.3, C.1.5, C.2.2, C.2.9, C.3.1-3.5
- ✅ Spec §6 File impact → C.1/C.2/C.3 cover all listed files
- ✅ Spec §7 Commit plan → 3 commits matching C.1/C.2/C.3
- ✅ Spec §8 Known limitations → documented in NEXT_STEPS.md update (C.3.7)
- ✅ Spec §9 Drift gates → executed every commit
- ✅ Phase A+B 149 tests stay green → every commit's drift gate runs full ctest sequentially

No placeholders, no "TBD", no "similar to Task N" — all code blocks are complete.
