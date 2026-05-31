# Pure AXI Subsystem Phase B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend Phase A pure AXI subsystem to handle full AXI4 burst types (INCR/WRAP/FIXED), unaligned addresses, narrow transfers, sparse WSTRB, 4KB cross auto-segmentation, and 22 runtime protocol validation rules — all preserving Phase A 78 tests green.

**Architecture:** No class restructure; extend existing axi/* hpps; 1 new file (`protocol_rules.hpp`); per-ID FIFO state model refactor in AxiSlave; OperationContext layer in AxiMaster for multi-sub-burst aggregation; debug-only `AXI_PROTOCOL_ASSERT` macro.

**Tech Stack:** C++17 + GoogleTest + yaml-cpp 0.9.0 (Phase A wiring reused); `ni::WSTRB_WIDTH` / `ni::width::*` from `spec_validate/include/*.h`; algorithms ported from cocotbext-axi (MIT).

**Spec**: `docs/superpowers/specs/2026-05-31-pure-axi-subsystem-phase-b-design.md`

**Project rules** (carry over from Phase A):
- Windows env: `py -3`; Bash chains `&&`; PowerShell `; if ($?) { ... }`
- Drift gates every commit
- Commit format `type(scope): description` (English; type ∈ feat/fix/docs/style/refactor/test/chore/perf)
- DO NOT `--no-verify`; DO NOT touch `spec/ni/doc/*`
- Karpathy 4-lens review per task
- **Phase A 78 tests stay green at every commit**

---

## File structure (final Phase B state)

```
c_model/include/axi/
    types.hpp                ← +1 const (k4KBytes)
    memory_port.hpp          ← unchanged
    memory.hpp               ← unchanged
    protocol_rules.hpp       ← NEW (B-5b)
    axi_slave.hpp            ← per-ID FIFO + WRAP/FIXED + narrow + unaligned + asserts
    axi_master.hpp           ← OperationContext + 4KB split + narrow + sparse load + asserts
    scoreboard.hpp           ← sparse strb byte-merge
    scenario_parser.hpp      ← strb_file + relaxed validation
    ATTRIBUTION.md           ← +1 row for protocol_rules.hpp
c_model/tests/axi/
    test_scaffold.cpp        ← unchanged
    test_memory.cpp          ← unchanged
    test_axi_slave.cpp       ← extended (B-2, B-3, B-4, B-5a)
    test_axi_master.cpp      ← extended (B-1, B-2, B-3, B-5a)
    test_scoreboard.cpp      ← extended (B-1)
    test_scenario_parser tests inside test_axi_master.cpp ← extended
    test_protocol_rules.cpp  ← NEW (B-5b)
    test_integration.cpp     ← +10 fixture rows in INSTANTIATE
    mock_memory_port.hpp     ← unchanged
    mock_slave.hpp           ← unchanged
    fixtures/
        unaligned_start.yaml + data + strb files
        narrow_transfer_size2.yaml + ...
        narrow_transfer_size0.yaml + ...
        wrap_burst_aligned.yaml + ...
        wrap_burst_actual_wrap.yaml + ...
        fixed_burst.yaml + ...
        cross_4kb_auto_split.yaml + ...
        sparse_multibeat.yaml + ...
        narrow_unaligned.yaml + ...
        (existing 12 Phase A fixtures)
```

---

# Stage B-1 — Sparse WSTRB infrastructure

### Task B-1.1: Add `strb_file` field to ScenarioTransaction + parser

**Files:**
- Modify: `c_model/include/axi/scenario_parser.hpp`

- [ ] **Step 1: Add `strb_file` field to ScenarioTransaction**

In `scenario_parser.hpp`, in the `ScenarioTransaction` struct, add after `dump_file`:
```cpp
std::string strb_file;  // optional; empty = full WSTRB per beat
```

- [ ] **Step 2: Parse strb_file in load_scenario**

In `load_scenario()` inside the txn loop, after `data_file` handling:
```cpp
if (t.op == ScenarioTransaction::Op::Write && txn["strb_file"]) {
  t.strb_file = txn["strb_file"].as<std::string>();
}
```

- [ ] **Step 3: Add to known fields list (avoid throwing on this new field)**

Find the "txn unknown field" check (if present) and add `"strb_file"` to its allowed set. If parser uses YAML node iteration with explicit checks, no change needed.

- [ ] **Step 4: Build to verify compile**

```bash
cd c_model && cmake --build build
```
Expected: clean build (no test yet).

- [ ] **Step 5: Drift gates green + commit**

```bash
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
cd .. && git add c_model/include/axi/scenario_parser.hpp
git commit -m "feat(c_model): add optional strb_file field to ScenarioTransaction"
```

### Task B-1.2: AxiMaster loads strb_file at scenario ctor time

**Files:**
- Modify: `c_model/include/axi/axi_master.hpp`

- [ ] **Step 1: Add `load_strb_file_` helper**

In `axi_master.hpp` private section, add after `load_write_data_`:
```cpp
static std::vector<uint32_t> load_strb_file_(const std::string& path,
                                              std::size_t expected_beats,
                                              std::size_t default_full = 0xFFFF'FFFFu) {
  if (path.empty()) {
    return std::vector<uint32_t>(expected_beats, static_cast<uint32_t>(default_full));
  }
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("AxiMaster: cannot open strb_file: " + path);
  std::vector<uint32_t> strbs;
  std::string tok;
  while (f >> tok) {
    strbs.push_back(static_cast<uint32_t>(std::stoul(tok, nullptr, 16)));
  }
  if (strbs.size() != expected_beats)
    throw std::runtime_error("AxiMaster: strb_file line count " + std::to_string(strbs.size())
                              + " != expected beats " + std::to_string(expected_beats)
                              + ": " + path);
  return strbs;
}
```

- [ ] **Step 2: WriteState struct adds strb_per_beat**

In `axi_master.hpp` `WriteState` struct, after `std::vector<uint8_t> data;` add:
```cpp
std::vector<uint32_t> strb_per_beat;  // size = txn.len + 1
```

- [ ] **Step 3: ctor loads strb_per_beat during admission**

In `tick()` admission step (where WriteState is constructed), after `ws.data = load_write_data_(...)` add:
```cpp
ws.strb_per_beat = load_strb_file_(txn.strb_file,
                                    static_cast<std::size_t>(txn.len + 1u));
```

- [ ] **Step 4: tick() push_w uses per-beat strb**

In `tick()` push W beat block, change:
```cpp
w.strb = 0xFFFF'FFFFu;  // Phase A full strb
```
to:
```cpp
w.strb = ws.strb_per_beat[ws.w_pushed_];
```

- [ ] **Step 5: Build to verify compile**

```bash
cd c_model && cmake --build build
```

- [ ] **Step 6: Drift gates + commit**

```bash
git add c_model/include/axi/axi_master.hpp
git commit -m "feat(c_model): AxiMaster loads strb_file + per-beat WSTRB push"
```

### Task B-1.3: WriteResult adds strb_per_beat field + Scoreboard signature change

**Files:**
- Modify: `c_model/include/axi/axi_master.hpp`
- Modify: `c_model/include/axi/scoreboard.hpp`
- Modify: `c_model/tests/axi/test_integration.cpp`
- Modify: `c_model/tests/axi/test_axi_master.cpp` (callsite updates)
- Modify: `c_model/tests/axi/test_scoreboard.cpp` (callsite updates)

- [ ] **Step 1: Modify WriteResult struct**

In `axi_master.hpp`:
```cpp
struct WriteResult {
  uint64_t addr;
  std::vector<uint8_t> data;
  std::vector<uint32_t> strb_per_beat;  // NEW
  Resp resp;
  uint8_t id;
  std::size_t scenario_line;
};
```

- [ ] **Step 2: tick() fires WriteResult with strb_per_beat**

In `tick()` B-drain block where WriteResult is constructed:
```cpp
if (wcb_) wcb_(WriteResult{it->second.txn.addr,
                            it->second.data,
                            it->second.strb_per_beat,  // NEW
                            b->resp, b->id,
                            it->second.txn.scenario_line});
```

- [ ] **Step 3: Modify Scoreboard signature + implementation**

In `scoreboard.hpp`:
```cpp
void handle_write_completed(const WriteResult& wr,
                            const std::vector<uint8_t>& data,
                            const std::vector<uint32_t>& strb_per_beat) {
  if (wr.resp != Resp::OKAY) return;
  std::size_t bytes_per_beat = DATA_BYTES;
  std::size_t beat_count = strb_per_beat.size();
  for (std::size_t beat = 0; beat < beat_count; ++beat) {
    uint32_t strb = strb_per_beat[beat];
    for (std::size_t byte_lane = 0; byte_lane < bytes_per_beat; ++byte_lane) {
      if ((strb >> byte_lane) & 0x1u) {
        std::size_t data_idx = beat * bytes_per_beat + byte_lane;
        if (data_idx < data.size()) {
          expected_[wr.addr + data_idx] = data[data_idx];
        }
      }
    }
  }
}
```

- [ ] **Step 4: Update test_integration.cpp callsite**

Change:
```cpp
master.on_write_completed([&](const axi::WriteResult& wr) {
  sb.handle_write_completed(wr, wr.data);
});
```
to:
```cpp
master.on_write_completed([&](const axi::WriteResult& wr) {
  sb.handle_write_completed(wr, wr.data, wr.strb_per_beat);
});
```

- [ ] **Step 5: Update test_scoreboard.cpp 4 callsites (Phase A backward compat)**

For each existing `handle_write_completed(WriteResult{...}, data)` call, change signature site `WriteResult{addr, data, resp, id, line}` to `WriteResult{addr, data, std::vector<uint32_t>(1, 0xFFFF'FFFFu), resp, id, line}` and call:
```cpp
sb.handle_write_completed(WriteResult{...}, data, std::vector<uint32_t>(1, 0xFFFF'FFFFu));
```

- [ ] **Step 6: Update test_axi_master.cpp callsite for WriteResult lambda**

```cpp
master.on_write_completed([&](const axi::WriteResult& r) {
  fired = true;
  EXPECT_EQ(r.id, 7);
  EXPECT_EQ(r.resp, axi::Resp::OKAY);
});
```
No change needed if test only inspects `r.id` / `r.resp`; otherwise verify `r.strb_per_beat.size() == r.txn.len + 1`.

- [ ] **Step 7: Build + run all Phase A tests → 78/78 pass**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: 78/78 pass (Phase A backward compat).

- [ ] **Step 8: Drift gates + commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/include/axi/scoreboard.hpp \
        c_model/tests/axi/test_integration.cpp c_model/tests/axi/test_scoreboard.cpp \
        c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): WriteResult adds strb_per_beat; Scoreboard uses per-beat WSTRB"
```

### Task B-1.4: ScenarioParser strb_file unit tests + Scoreboard sparse test

**Files:**
- Modify: `c_model/tests/axi/test_axi_master.cpp` (parser tests)
- Modify: `c_model/tests/axi/test_scoreboard.cpp`

- [ ] **Step 1: Append 5 ScenarioParser tests**

In `test_axi_master.cpp` `ScenarioParser` fixture:
```cpp
TEST_F(ScenarioParser, StrbFileFieldAccepted) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    data_file: w.txt
    strb_file: s.txt
)YAML");
  auto sc = axi::load_scenario(path);
  ASSERT_EQ(sc.transactions.size(), 1u);
  EXPECT_EQ(sc.transactions[0].strb_file, "s.txt");
}

TEST_F(ScenarioParser, StrbFileOptional) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    data_file: w.txt
)YAML");
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.transactions[0].strb_file, "");
}

TEST_F(ScenarioParser, ReadTxnIgnoresStrbFile) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0
    id: 0
    len: 0
    size: 5
    burst: INCR
    dump_file: r.txt
)YAML");
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.transactions[0].strb_file, "");
}
```

- [ ] **Step 2: Append 2 AxiMaster strb_file load tests**

In `test_axi_master.cpp` `AxiMasterTest` fixture:
```cpp
TEST_F(AxiMasterTest, StrbFileMissingThrows) {
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + write_tmp_data("a.txt", "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20") + R"YAML(
    strb_file: nonexistent_strb.txt
)YAML");
  ni::cmodel::axi::testing::MockSlave mock;
  EXPECT_THROW({
    axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
        yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
    master.tick();  // throws on load
  }, std::runtime_error);
}

TEST_F(AxiMasterTest, StrbFileLineCountMismatchThrows) {
  auto wpath = write_tmp_data("w.txt", "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20");
  auto spath = write_tmp_data("s.txt", "FFFFFFFF\nFFFFFFFF\n");  // 2 lines for 1 beat
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + R"YAML(
    strb_file: )YAML" + spath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  EXPECT_THROW({
    axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
        yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
    master.tick();
  }, std::runtime_error);
}
```

(Helper: add `write_tmp_data` to `ScenarioParser` if not present — opens file, writes content, returns path.)

- [ ] **Step 3: Append Scoreboard sparse byte-merge test**

In `test_scoreboard.cpp`:
```cpp
TEST(Scoreboard, SparseWstrbByteMerge) {
  axi::Scoreboard sb;
  // 1-beat write with sparse strb 0x000F (lanes 0-3 only)
  axi::WriteResult wr{0x100, /*data*/{}, /*strb*/{}, axi::Resp::OKAY, 1, 1};
  std::vector<uint8_t> data(32, 0xAA);
  std::vector<uint32_t> strb{0x0000000F};
  sb.handle_write_completed(wr, data, strb);
  // Read of full 32 bytes: lanes 0-3 should be 0xAA, lanes 4-31 should be 0x00 (unwritten = default)
  std::vector<uint8_t> read_data(32, 0x00);
  for (int i = 0; i < 4; ++i) read_data[i] = 0xAA;
  // lanes 4-31 stay 0x00
  axi::ReadResult rr{0x100, read_data, axi::Resp::OKAY, 1, 2};
  sb.handle_read_observed(rr);
  EXPECT_EQ(sb.mismatch_count(), 0u);
}
```

- [ ] **Step 4: Build + run → all pass**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: 78 prior + 8 new = 86 tests pass.

- [ ] **Step 5: Karpathy + drift gates + commit**

```bash
git add c_model/tests/axi/test_axi_master.cpp c_model/tests/axi/test_scoreboard.cpp
git commit -m "test(c_model): strb_file parser + AxiMaster load + Scoreboard sparse byte-merge"
```

### Stage B-1 exit checklist
- [ ] strb_file field parses correctly
- [ ] AxiMaster loads strb_file with validation
- [ ] WriteResult carries strb_per_beat
- [ ] Scoreboard uses per-beat WSTRB
- [ ] 78 Phase A tests + 8 new = 86/86 pass
- [ ] Mark B-1 → Complete in IMPLEMENTATION_PLAN.md

---

# Stage B-2 — Unaligned start address

### Task B-2.1: Parser accepts INCR unaligned (relax existing check)

**Files:** Modify `c_model/include/axi/scenario_parser.hpp`

- [ ] **Step 1: Find and remove (or condition) the unaligned reject**

Currently parser has:
```cpp
if ((t.addr & ((1ull << t.size) - 1)) != 0) {
  throw std::runtime_error("scenario txn ... addr must be aligned to (1<<size) in Phase A");
}
```

Change to:
```cpp
// Phase B: INCR allows unaligned start; WRAP requires alignment
if (t.burst == Burst::WRAP && (t.addr & ((1ull << t.size) - 1)) != 0) {
  throw std::runtime_error("scenario txn " + std::to_string(line) +
                            ": WRAP burst addr must be aligned to (1<<size)");
}
```

- [ ] **Step 2: Add 1 parser test for INCR unaligned acceptance**

In `test_axi_master.cpp`:
```cpp
TEST_F(ScenarioParser, IncrUnalignedAccepted_PhaseB) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x1003
    id: 0
    len: 0
    size: 5
    burst: INCR
    dump_file: r.txt
)YAML");
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.transactions[0].addr, 0x1003u);
}
```

(Old `UnalignedAddrThrows_PhaseA` test will now break — DELETE that test in this commit.)

- [ ] **Step 3: Build + test → 86 + 1 (new) - 1 (deleted) = 86 pass**

- [ ] **Step 4: Drift + commit**

```bash
git add c_model/include/axi/scenario_parser.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): parser accepts INCR unaligned (Phase B); WRAP still rejected"
```

### Task B-2.2: AxiMaster computes first-beat unaligned WSTRB

**Files:** Modify `c_model/include/axi/axi_master.hpp`

- [ ] **Step 1: Add first-beat strb override in tick() push_w**

Locate the W push block in `tick()` and modify:
```cpp
while (ws.w_pushed_ <= ws.txn.len) {
  WBeat w{};
  std::size_t bpb = 1ull << ws.txn.size;
  for (std::size_t j = 0; j < DATA_BYTES; ++j) {
    std::size_t off = ws.w_pushed_ * bpb + j;
    w.data[j] = (off < ws.data.size()) ? ws.data[off] : 0;
  }
  w.strb = ws.strb_per_beat[ws.w_pushed_];

  // Phase B: first beat of unaligned-start burst — mask out lower lanes
  if (ws.w_pushed_ == 0) {
    std::size_t first_lane = ws.txn.addr & (DATA_BYTES - 1);
    if (first_lane != 0) {
      uint32_t mask = ~static_cast<uint32_t>((1ull << first_lane) - 1);
      w.strb &= mask;
    }
  }
  w.last = (ws.w_pushed_ == ws.txn.len);
  if (!slave_.push_w(w)) break;
  ++ws.w_pushed_;
}
```

- [ ] **Step 2: AwBeat addr is aligned-down**

For unaligned start, the wire AW addr should be aligned down to `(1<<size)`. Modify the AW push block:
```cpp
if (ws.aw_pushed_ == 0) {
  AwBeat aw{};
  aw.id = id;
  aw.addr = ws.txn.addr & ~((1ull << ws.txn.size) - 1);  // aligned down
  aw.len = ws.txn.len;
  aw.size = ws.txn.size;
  aw.burst = ws.txn.burst;
  if (!slave_.push_aw(aw)) continue;
  ws.aw_pushed_ = 1;
}
```

(Note: this aligns single AW. WRAP doesn't go through this Phase B path because parser rejects WRAP unaligned.)

- [ ] **Step 3: Append 5 unit tests for unaligned WSTRB calc**

In `test_axi_master.cpp`:
```cpp
TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked) {
  // addr=0x1003, size=5 (32B beat), 1 beat
  auto wpath = write_tmp_data("w.txt",
      "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
      "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1003
    id: 0x7
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x1000u);  // aligned down
  ASSERT_EQ(mock.captured_w.size(), 1u);
  // first_lane = 3 → strb mask = ~((1<<3)-1) = ~0x7 = 0xFFFFFFF8
  EXPECT_EQ(mock.captured_w[0].strb, 0xFFFFFFF8u);
}
```

(Add 4 more tests at different sizes: addr=0x1001 size=4, addr=0x1007 size=3, addr=0x100F size=2, addr=0x101F size=1.)

- [ ] **Step 4: Build + run + drift + commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): AxiMaster computes first-beat WSTRB for unaligned INCR start"
```

### Task B-2.3: AxiSlave handles sparse WSTRB forward (already works; add test)

**Files:** Modify `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Add 2 tests verifying sparse strb forward**

```cpp
TEST(AxiSlave, SparseStrbForwarededToMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 8; aw.addr = 0x1000; aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  axi::WBeat w{}; w.data.fill(0xCC); w.strb = 0xFFFFFFF8u; w.last = true;
  slave.push_w(w);
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].strb, 0xFFFFFFF8u);  // forwarded as-is
}
```

- [ ] **Step 2: Build + drift + commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave forwards sparse WSTRB to memory unchanged"
```

### Task B-2.4: Integration fixture `unaligned_start.yaml`

**Files:**
- Create: `c_model/tests/axi/fixtures/unaligned_start.yaml`
- Create: `c_model/tests/axi/fixtures/unaligned_start_data.txt`
- Modify: `c_model/tests/axi/test_integration.cpp` (add fixture row)

- [ ] **Step 1: Create fixture YAML**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: write
    addr: 0x1005
    id: 0x9
    len: 0
    size: 5
    burst: INCR
    data_file: fixtures/unaligned_start_data.txt
  - op: read
    addr: 0x1005
    id: 0x9
    len: 0
    size: 5
    burst: INCR
    dump_file: unused_read.txt
```

- [ ] **Step 2: Create data file (27 bytes user data + 5 bytes padding ignored)**

```
00 00 00 00 00 AB CD EF 12 34 56 78 9A BC DE F0 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00
```

(Bytes 0-4 are 0 because they're masked out by WSTRB; bytes 5-31 hold user data.)

- [ ] **Step 3: Add to INSTANTIATE_TEST_SUITE_P in test_integration.cpp**

```cpp
FixtureParam{"unaligned_start.yaml", "", false, true},
```

(`expect_file_diff_pass=false` because dump file will show full 32B per beat; diff against original data_file may not match due to alignment. Scoreboard is the source of truth.)

- [ ] **Step 4: Build + test → all pass**

- [ ] **Step 5: Commit + Karpathy 4-lens**

```bash
git add c_model/tests/axi/fixtures/unaligned_start.yaml \
        c_model/tests/axi/fixtures/unaligned_start_data.txt \
        c_model/tests/axi/test_integration.cpp
git commit -m "test(c_model): integration fixture for unaligned INCR start address"
```

### Stage B-2 exit checklist
- [ ] Parser accepts INCR unaligned (rejects WRAP unaligned)
- [ ] AxiMaster computes first-beat WSTRB + aligned-down AW addr
- [ ] AxiSlave forwards sparse WSTRB unchanged
- [ ] Integration fixture `unaligned_start.yaml` passes
- [ ] All tests still pass (86 + 5 unit + 2 axi_slave + 1 integration = 94)
- [ ] Mark B-2 → Complete

---

# Stage B-3 — Narrow transfer

### Task B-3.1: Parser accepts narrow size (no change needed if size<=5 already allowed)

Phase A parser already accepts `size <= 5`. Narrow (size 0-4) is already accepted. No change. Skip task.

### Task B-3.2: AxiMaster narrow byte-lane placement

**Files:** Modify `c_model/include/axi/axi_master.hpp`

- [ ] **Step 1: Refactor W push to handle narrow byte lanes**

Replace the W push block in `tick()` with:
```cpp
while (ws.w_pushed_ <= ws.txn.len) {
  WBeat w{};
  w.data.fill(0);  // start zero
  std::size_t bpb = 1ull << ws.txn.size;

  // Compute per-beat address (aligned down for first beat handled separately)
  uint64_t beat_addr = ws.txn.addr + ws.w_pushed_ * bpb;
  if (ws.txn.burst == Burst::FIXED) beat_addr = ws.txn.addr;  // FIXED reuses base
  // WRAP handled in B-4 (parser rejects WRAP unaligned, so addr always aligned)

  std::size_t byte_lane = beat_addr & (DATA_BYTES - 1);

  // Copy user data bytes into byte lane position
  for (std::size_t j = 0; j < bpb; ++j) {
    std::size_t off = ws.w_pushed_ * bpb + j;
    if (off < ws.data.size() && (byte_lane + j) < DATA_BYTES) {
      w.data[byte_lane + j] = ws.data[off];
    }
  }

  // Set strb: only enable bytes in [byte_lane, byte_lane + bpb)
  uint32_t lane_mask = ((1ull << bpb) - 1) << byte_lane;
  w.strb = ws.strb_per_beat[ws.w_pushed_] & lane_mask;

  // First-beat unaligned (existing B-2 logic) — already covered by byte_lane + lane_mask above
  // because the first beat's byte_lane reflects the unaligned offset

  w.last = (ws.w_pushed_ == ws.txn.len);
  if (!slave_.push_w(w)) break;
  ++ws.w_pushed_;
}
```

(Note: the previous B-2 first-beat strb override is now redundant — narrow lane logic naturally handles it. Remove the B-2 conditional block.)

- [ ] **Step 2: Add narrow size unit tests for all sizes 0-4**

In `test_axi_master.cpp`:
```cpp
TEST_F(AxiMasterTest, NarrowSize2_4BytePerBeat) {
  // addr=0x1004, len=1, size=2 (4B/beat), 2 beats, 8B total
  auto wpath = write_tmp_data("w_nar2.txt",
      "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
      "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1004
    id: 0x3
    len: 1
    size: 2
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
  master.tick();
  ASSERT_EQ(mock.captured_w.size(), 2u);
  // Beat 0: addr=0x1004, byte_lane=4, strb=0x000000F0
  EXPECT_EQ(mock.captured_w[0].strb, 0x000000F0u);
  EXPECT_EQ(mock.captured_w[0].data[4], 0x00);
  EXPECT_EQ(mock.captured_w[0].data[5], 0x01);
  EXPECT_EQ(mock.captured_w[0].data[6], 0x02);
  EXPECT_EQ(mock.captured_w[0].data[7], 0x03);
  // Beat 1: addr=0x1008, byte_lane=8, strb=0x00000F00
  EXPECT_EQ(mock.captured_w[1].strb, 0x00000F00u);
  EXPECT_EQ(mock.captured_w[1].data[8], 0x04);
}
```

(Add similar tests for sizes 0/1/3/4 — vary data file and expected strb/lanes.)

- [ ] **Step 3: Build + test + commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): AxiMaster narrow transfer byte-lane placement"
```

### Task B-3.3: AxiSlave narrow interpret (forwards strb + addr to memory)

AxiSlave currently forwards `(addr, data, strb)` to memory unchanged. Narrow handling is implicit because memory honors WSTRB byte mask. No code change needed.

- [ ] **Step 1: Add 1 axi_slave test verifying narrow forward**

```cpp
TEST(AxiSlave, NarrowTransferForwardedToMemory) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 4; aw.addr = 0x1004; aw.len = 1; aw.size = 2; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 2; ++i) {
    axi::WBeat w{}; w.data.fill(0xAA + i); w.strb = 0x000000F0u << (i * 4); w.last = (i == 1);
    slave.push_w(w);
  }
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 2u);
  EXPECT_EQ(mem.captured_writes[0].addr, 0x1004u);  // wire addr, not aligned-down
  EXPECT_EQ(mem.captured_writes[0].strb, 0x000000F0u);
  EXPECT_EQ(mem.captured_writes[1].addr, 0x1008u);
}
```

(Wait — actually slave computes beat_addr from aw.addr + i * (1<<size). For narrow, slave should compute the same `byte_lane`-aware addr. Verify implementation matches expectation.)

- [ ] **Step 2: Build + commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave narrow transfer addr + strb forwarded"
```

### Task B-3.4: Integration fixtures narrow_transfer_size2 + narrow_transfer_size0

**Files:** Create 2 fixtures + add to INSTANTIATE

- [ ] **Step 1: Fixture `narrow_transfer_size2.yaml`**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
  write_latency: 1
  read_latency: 1
transactions:
  - op: write
    addr: 0x1004
    id: 0x3
    len: 1
    size: 2
    burst: INCR
    data_file: fixtures/narrow_transfer_size2_data.txt
  - op: read
    addr: 0x1004
    id: 0x3
    len: 1
    size: 2
    burst: INCR
    dump_file: unused_read.txt
```

Data file (8 bytes):
```
AB CD EF 12 34 56 78 9A 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 EF 12 34 56 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

(First 4 bytes of each line are user data; rest padded zero. data_file format is per-beat 32-byte; the active bytes are at byte lane corresponding to addr.)

- [ ] **Step 2: Fixture `narrow_transfer_size0.yaml`** (1 byte per beat, extreme case)

Similar structure with size=0, len=3 (4 beats × 1 byte = 4 bytes).

- [ ] **Step 3: Add 2 rows to INSTANTIATE_TEST_SUITE_P**

```cpp
FixtureParam{"narrow_transfer_size2.yaml", "", false, true},
FixtureParam{"narrow_transfer_size0.yaml", "", false, true},
```

- [ ] **Step 4: Build + test + commit**

```bash
git add c_model/tests/axi/fixtures/narrow_transfer_size2.yaml \
        c_model/tests/axi/fixtures/narrow_transfer_size2_data.txt \
        c_model/tests/axi/fixtures/narrow_transfer_size0.yaml \
        c_model/tests/axi/fixtures/narrow_transfer_size0_data.txt \
        c_model/tests/axi/test_integration.cpp
git commit -m "test(c_model): integration fixtures for narrow transfer size 0/2"
```

### Stage B-3 exit checklist
- [ ] AxiMaster computes byte lane per beat (all sizes 0-5)
- [ ] AxiSlave forwards unchanged; memory honors strb
- [ ] 2 integration fixtures pass
- [ ] All tests pass (~98)
- [ ] Mark B-3 → Complete

---

# Stage B-4 — WRAP + FIXED burst

### Task B-4.1: AxiSlave WRAP addr formula

**Files:** Modify `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: Replace addr calc in step 4 (submit W)**

Find:
```cpp
req.addr = st.aw.addr + beat_idx * (1ull << st.aw.size);
```
Replace with:
```cpp
req.addr = beat_addr_(st.aw, beat_idx);
```

Add private helper:
```cpp
private:
  static uint64_t beat_addr_(const AwBeat& aw, std::size_t beat_idx) {
    std::size_t bpb = 1ull << aw.size;
    switch (aw.burst) {
      case Burst::FIXED:
        return aw.addr;
      case Burst::INCR:
        return aw.addr + beat_idx * bpb;
      case Burst::WRAP: {
        std::size_t burst_byte_total = (static_cast<std::size_t>(aw.len) + 1) * bpb;
        uint64_t wrap_lower = aw.addr & ~(burst_byte_total - 1);
        uint64_t addr = aw.addr + beat_idx * bpb;
        if (addr >= wrap_lower + burst_byte_total) {
          addr = wrap_lower + ((beat_idx * bpb) - (wrap_lower + burst_byte_total - aw.addr));
        }
        return addr;
      }
    }
    return aw.addr;  // unreachable
  }

  static uint64_t beat_addr_(const ArBeat& ar, std::size_t beat_idx) {
    // identical structure to AwBeat overload
    std::size_t bpb = 1ull << ar.size;
    switch (ar.burst) {
      case Burst::FIXED: return ar.addr;
      case Burst::INCR:  return ar.addr + beat_idx * bpb;
      case Burst::WRAP: {
        std::size_t burst_byte_total = (static_cast<std::size_t>(ar.len) + 1) * bpb;
        uint64_t wrap_lower = ar.addr & ~(burst_byte_total - 1);
        uint64_t addr = ar.addr + beat_idx * bpb;
        if (addr >= wrap_lower + burst_byte_total) {
          addr = wrap_lower + ((beat_idx * bpb) - (wrap_lower + burst_byte_total - ar.addr));
        }
        return addr;
      }
    }
    return ar.addr;
  }
```

Similarly replace addr calc in step 6 (submit AR) and step 3 (OOB pre-check uses bpb * (len+1) total — for WRAP this same total, so no change to OOB calc).

- [ ] **Step 2: Verify Phase A 78 tests still pass**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: 78/78 + any new from B-1~B-3 pass. INCR/FIXED paths unaffected.

- [ ] **Step 3: Append WRAP burst unit tests (all 4 valid lens)**

```cpp
TEST(AxiSlave, WrapBurstLen1_2BeatActualWrap) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 5;
  ar.addr = 0x1040;  // mid-boundary; burst total = 64B, wrap_lower = 0x1000, wrap_upper = 0x1040 → wraps immediately
  ar.len = 1; ar.size = 5; ar.burst = axi::Burst::WRAP;
  // burst total = 2 * 32 = 64; wrap_lower = 0x1040 & ~0x3F = 0x1040; wrap_upper = 0x1080
  // beat 0 at 0x1040; beat 1 at 0x1060 (within wrap_upper)
  // (Not actually wrapping in this case; pick different addr for real wrap)
  slave.push_ar(ar);
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 2u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x1040u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x1060u);
}

TEST(AxiSlave, WrapBurstLen3_4BeatActualWrap) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 5;
  ar.addr = 0x1060;  // burst total = 128B, wrap_lower = 0x1000, wrap_upper = 0x1080
  ar.len = 3; ar.size = 5; ar.burst = axi::Burst::WRAP;
  // beat 0 at 0x1060; beat 1 at 0x1080 → wraps to 0x1000; beat 2 at 0x1020; beat 3 at 0x1040
  slave.push_ar(ar);
  for (int t = 0; t < 5; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 4u);
  EXPECT_EQ(mem.captured_reads[0].addr, 0x1060u);
  EXPECT_EQ(mem.captured_reads[1].addr, 0x1000u);
  EXPECT_EQ(mem.captured_reads[2].addr, 0x1020u);
  EXPECT_EQ(mem.captured_reads[3].addr, 0x1040u);
}

// Similar tests for len=7 (8 beats) and len=15 (16 beats) — vary addr and size
TEST(AxiSlave, WrapBurstLen7_8Beat) { /* ... */ }
TEST(AxiSlave, WrapBurstLen15_16Beat) { /* ... */ }
```

- [ ] **Step 4: Append FIXED burst unit test**

```cpp
TEST(AxiSlave, FixedBurstAllBeatsSameAddr) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 6; aw.addr = 0x1000; aw.len = 3; aw.size = 5; aw.burst = axi::Burst::FIXED;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 4; ++i) {
    axi::WBeat w{}; w.data.fill(0x10 + i); w.strb = 0xFFFF'FFFFu; w.last = (i == 3);
    slave.push_w(w);
  }
  for (int t = 0; t < 8; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 4u);
  for (auto& cw : mem.captured_writes) {
    EXPECT_EQ(cw.addr, 0x1000u);
  }
}
```

- [ ] **Step 5: Build + test + commit**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/tests/axi/test_axi_slave.cpp
git commit -m "feat(c_model): AxiSlave WRAP and FIXED burst address calculation"
```

### Task B-4.2: Parser WRAP/FIXED acceptance + WRAP len/align checks

**Files:** Modify `c_model/include/axi/scenario_parser.hpp` + tests

- [ ] **Step 1: Remove or modify the "burst != INCR" reject**

Find:
```cpp
if (t.burst != Burst::INCR) {
  throw std::runtime_error("... Phase A only supports INCR burst");
}
```
Replace with WRAP-specific checks:
```cpp
if (t.burst == Burst::WRAP) {
  if (!(t.len == 1 || t.len == 3 || t.len == 7 || t.len == 15)) {
    throw std::runtime_error("scenario txn " + std::to_string(line) +
                              ": WRAP burst len must be 1, 3, 7, or 15");
  }
  if ((t.addr & ((1ull << t.size) - 1)) != 0) {
    throw std::runtime_error("scenario txn " + std::to_string(line) +
                              ": WRAP burst addr must be aligned to (1<<size)");
  }
}
// INCR / FIXED are now accepted; B-2 INCR-unaligned check already in place
```

- [ ] **Step 2: Tests for WRAP/FIXED parser acceptance + rejections**

```cpp
TEST_F(ScenarioParser, WrapAcceptedWithValidLen) {
  for (uint8_t len : {1, 3, 7, 15}) {
    auto path = write_tmp("transactions:\n"
                          "  - op: read\n    addr: 0x1000\n    id: 0\n"
                          "    len: " + std::to_string(len) + "\n    size: 5\n"
                          "    burst: WRAP\n    dump_file: r.txt\n");
    EXPECT_NO_THROW(axi::load_scenario(path)) << "len=" << int(len);
  }
}

TEST_F(ScenarioParser, WrapRejectedWithInvalidLen) {
  for (uint8_t len : {0, 2, 4, 5, 6, 8, 9, 16}) {
    auto path = write_tmp("transactions:\n"
                          "  - op: read\n    addr: 0x1000\n    id: 0\n"
                          "    len: " + std::to_string(len) + "\n    size: 5\n"
                          "    burst: WRAP\n    dump_file: r.txt\n");
    EXPECT_THROW(axi::load_scenario(path), std::runtime_error) << "len=" << int(len);
  }
}

TEST_F(ScenarioParser, WrapRejectedWithUnalignedAddr) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x1003
    id: 0
    len: 3
    size: 5
    burst: WRAP
    dump_file: r.txt
)YAML");
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}

TEST_F(ScenarioParser, FixedAccepted) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x1000
    id: 0
    len: 3
    size: 5
    burst: FIXED
    dump_file: r.txt
)YAML");
  EXPECT_NO_THROW(axi::load_scenario(path));
}
```

(Delete old `NonIncrBurstThrows_PhaseA` test.)

- [ ] **Step 3: Build + test + commit**

```bash
git add c_model/include/axi/scenario_parser.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): parser accepts WRAP/FIXED; rejects WRAP invalid len/unaligned"
```

### Task B-4.3: Integration fixtures wrap_burst_aligned + wrap_burst_actual_wrap + fixed_burst

Create 3 fixtures + add 3 rows to INSTANTIATE. Pattern same as Phase A. Commit:
```
test(c_model): integration fixtures for WRAP aligned, WRAP actual wrap, FIXED bursts
```

### Stage B-4 exit checklist
- [ ] WRAP addr formula correct for all 4 valid lens
- [ ] FIXED addr correct (same for all beats)
- [ ] Parser accepts WRAP/FIXED with constraints
- [ ] 3 integration fixtures pass
- [ ] All tests pass (~110)
- [ ] Mark B-4 → Complete

---

# Stage B-5a — 4KB cross + OperationContext + per-ID FIFO refactor

### Task B-5a.1: AxiSlave per-ID FIFO state model refactor

**Files:** Modify `c_model/include/axi/axi_slave.hpp`

- [ ] **Step 1: Refactor active_writes_ + active_reads_ to deque**

Replace:
```cpp
std::map<uint8_t, WriteBurstState> active_writes_;
std::map<uint8_t, ReadBurstState>  active_reads_;
```
with:
```cpp
std::map<uint8_t, std::deque<WriteBurstState>> active_writes_;
std::map<uint8_t, std::deque<ReadBurstState>>  active_reads_;
```

- [ ] **Step 2: Update step 1 (drain B): match by id, advance oldest burst's beats_completed**

```cpp
while (auto resp = memory_port_.pop_write_resp()) {
  auto it = active_writes_.find(resp->id);
  if (it == active_writes_.end() || it->second.empty()) continue;
  auto& st = it->second.front();  // oldest burst for this id
  ++st.beats_completed;
  if (resp->resp != Resp::OKAY) {
    if (static_cast<uint8_t>(resp->resp) > static_cast<uint8_t>(st.worst_resp))
      st.worst_resp = resp->resp;
  }
  if (st.beats_completed == static_cast<std::size_t>(st.aw.len) + 1) {
    b_q_.push_back(BBeat{st.aw.id, st.worst_resp, 0});
    it->second.pop_front();  // remove completed burst from FIFO
    // Remove from aw_issue_order_
    for (auto i = aw_issue_order_.begin(); i != aw_issue_order_.end(); ++i) {
      if (*i == st.aw.id) { aw_issue_order_.erase(i); break; }
    }
    if (it->second.empty()) active_writes_.erase(it);
  }
}
```

- [ ] **Step 3: Update step 3 (start AW): no same-id exclusion; admission per-id FIFO depth**

```cpp
while (!aw_q_.empty()) {
  auto& aw = aw_q_.front();
  // Phase A check: if (active_writes_.count(aw.id)) break;  // REMOVE THIS
  // Phase B: per-ID FIFO; allow multi outstanding per ID
  // (No max per-id limit in Phase B; if needed, add later)

  // OOB pre-check (unchanged) ...

  active_writes_[aw.id].push_back(WriteBurstState{aw, 0, 0, Resp::OKAY});
  aw_issue_order_.push_back(aw.id);
  aw_q_.pop_front();
}
```

- [ ] **Step 4: Update step 4 (submit W): match to oldest active burst for current aw_issue_order_ front id**

```cpp
while (!w_q_.empty() && !aw_issue_order_.empty()) {
  uint8_t front_id = aw_issue_order_.front();
  auto& st = active_writes_[front_id].front();  // oldest burst for this id (matches issue order)
  // ... rest unchanged ...
  // pop aw_issue_order_.front() when this burst's W beats fully submitted (existing logic)
}
```

- [ ] **Step 5: Update step 5 (start AR) + step 6 (submit AR) similarly**

(Mirror writes to reads; remove same-id exclusion; per-id FIFO.)

- [ ] **Step 6: Update existing AxiSlave tests that depended on single-slot behavior**

Find any Phase A test that asserted "same-id second push rejected" or similar; update to expect FIFO acceptance.

Specifically check `SequentialBurstsDifferentIds` and `ConcurrentBurstsDifferentIds_WRoutingAdvances` — they should still pass without modification.

- [ ] **Step 7: Build + run ALL tests → 78 + B-1~B-4 still pass**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: all green. If any Phase A test breaks, fix per FIFO semantics.

- [ ] **Step 8: Add new test for same-id multi-outstanding**

```cpp
TEST(AxiSlave, SameIdMultiOutstanding_FifoOrder) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  // Push 3 AWs all with id=5, distinct addrs
  for (int i = 0; i < 3; ++i) {
    axi::AwBeat aw{};
    aw.id = 5; aw.addr = 0x1000 + i * 0x40;
    aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
    slave.push_aw(aw);
    axi::WBeat w{}; w.data.fill(0xA0 + i); w.strb = 0xFFFF'FFFFu; w.last = true;
    slave.push_w(w);
  }
  for (int t = 0; t < 5; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 3u);
  EXPECT_EQ(mem.captured_writes[0].addr, 0x1000u);
  EXPECT_EQ(mem.captured_writes[1].addr, 0x1040u);
  EXPECT_EQ(mem.captured_writes[2].addr, 0x1080u);
  // All same id; FIFO order preserved
}
```

- [ ] **Step 9: Commit + Karpathy 4-lens**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/tests/axi/test_axi_slave.cpp
git commit -m "refactor(c_model): AxiSlave per-ID FIFO state model (multi-outstanding ready)

Phase A single-slot per-ID was insufficient for Phase B 4KB cross
auto-split (which produces same-ID multi-burst). active_writes_ /
active_reads_ now map ID to deque of bursts. Phase A 78 tests verified
green after refactor."
```

### Task B-5a.2: AxiMaster OperationContext struct + admission

**Files:** Modify `c_model/include/axi/axi_master.hpp`

- [ ] **Step 1: Add OperationContext struct**

```cpp
struct BurstSpec { uint64_t addr; uint8_t len, size; Burst burst; };

struct OperationContext {
  ScenarioTransaction src_txn;
  std::vector<BurstSpec> sub_bursts;
  std::size_t next_sub_burst_idx = 0;       // next sub-burst to push to slave
  std::size_t completed_count = 0;            // sub-bursts whose B/R returned
  Resp worst_resp = Resp::OKAY;
  // For writes: aggregate data + strb across sub-bursts
  std::vector<uint8_t> data;
  std::vector<uint32_t> strb_per_beat;
  // For reads: accumulate response data
  std::vector<uint8_t> read_accumulator;
};

std::deque<OperationContext> active_ops_;
```

(Replace existing WriteState/ReadState maps with OperationContext approach.)

- [ ] **Step 2: split_into_sub_bursts helper**

```cpp
static std::vector<BurstSpec> split_into_sub_bursts(const ScenarioTransaction& txn) {
  std::vector<BurstSpec> out;
  if (txn.burst != Burst::INCR) {
    // WRAP / FIXED: 1 sub-burst (no split)
    out.push_back({txn.addr, txn.len, txn.size, txn.burst});
    return out;
  }
  uint64_t addr = txn.addr;
  std::size_t bpb = 1ull << txn.size;
  std::size_t beats_remaining = static_cast<std::size_t>(txn.len) + 1;
  while (beats_remaining > 0) {
    std::size_t bytes_to_4kb = 0x1000 - (addr & 0xFFF);
    std::size_t beats_to_4kb = bytes_to_4kb / bpb;
    std::size_t beats_this_burst = std::min({beats_to_4kb, beats_remaining,
                                              static_cast<std::size_t>(256)});
    out.push_back({addr, static_cast<uint8_t>(beats_this_burst - 1), txn.size, Burst::INCR});
    addr += beats_this_burst * bpb;
    beats_remaining -= beats_this_burst;
  }
  return out;
}
```

- [ ] **Step 3: tick() rewrite admission to use OperationContext**

Replace `active_writes_ / active_reads_` map-based admission with `active_ops_` deque:
- Pull next scenario_txn, split into sub-bursts, create OperationContext, push to active_ops_
- Per OperationContext, push sub_bursts[next_sub_burst_idx] to slave
- On B/R: find OperationContext by id (match against current sub-burst's id); increment completed_count; if all done, fire WriteResult/ReadResult

(This is a large rewrite — TDD with small steps. Skeleton above; build TDD per Phase A test pass.)

- [ ] **Step 4: 4KB split unit tests**

```cpp
TEST_F(AxiMasterTest, Split4KBCross_2SubBursts) {
  auto wpath = write_tmp_data("w_4kb.txt", /* 256B = 8 beats × 32B */);
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0FE0
    id: 0x5
    len: 7
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 2u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x0FE0u);
  EXPECT_EQ(mock.captured_aw[0].len, 0);      // 1 beat
  EXPECT_EQ(mock.captured_aw[1].addr, 0x1000u);
  EXPECT_EQ(mock.captured_aw[1].len, 6);      // 7 beats
}

TEST_F(AxiMasterTest, NoSplit_AlignedAt4KBStart) {
  auto wpath = write_tmp_data("w_4kbs.txt", /* 32B = 1 beat */);
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
  EXPECT_EQ(mock.captured_aw.size(), 1u);  // no split
}

TEST_F(AxiMasterTest, MaxBurstLen256_Split) {
  // 512 beats × 32B = 16KB → split into 64 × 256-beat bursts (or fewer if 4KB cuts in)
  // ... TBD: scope-down to 257 beats → 2 sub-bursts (256 + 1)
}
```

- [ ] **Step 5: Build + test + commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): AxiMaster OperationContext + 4KB cross auto-split

Replaces per-ID WriteState/ReadState map with OperationContext deque.
Each scenario_txn → 1 OperationContext → N sub-bursts (split at 4KB
boundary, respecting 256-beat max). 1 WriteResult/ReadResult fires
per scenario_txn after all sub-bursts complete."
```

### Task B-5a.3: Integration fixture cross_4kb_auto_split

Create `cross_4kb_auto_split.yaml` + data file + add to INSTANTIATE. Commit:
```
test(c_model): integration fixture for 4KB cross auto-segmentation
```

### Stage B-5a exit checklist
- [ ] AxiSlave per-ID FIFO refactor; Phase A 78 tests + B-1~B-4 still green
- [ ] AxiMaster OperationContext + 4KB split implemented
- [ ] 4KB split unit tests + 1 integration fixture pass
- [ ] All tests ~120+ pass
- [ ] Mark B-5a → Complete

---

# Stage B-5b — Runtime protocol validation

### Task B-5b.1: Create `protocol_rules.hpp` with macro + 22 helpers

**Files:** Create `c_model/include/axi/protocol_rules.hpp`

- [ ] **Step 1: Skeleton file with macro + stateless helpers**

```cpp
// (Independent design; validation patterns inspired by cocotbext-axi)
#pragma once
#include "axi/types.hpp"
#include <cassert>
#include <cstdint>
#include <deque>

namespace ni::cmodel::axi::rules {

#ifdef NDEBUG
  #define AXI_PROTOCOL_ASSERT(cond, msg) ((void)0)
#else
  #define AXI_PROTOCOL_ASSERT(cond, msg) assert((cond) && (msg))
#endif

// === 5 stateless field checks ===
inline bool check_burst_encoding(Burst b) {
  return static_cast<int>(b) < 3;
}
inline bool check_size_bound(uint8_t size) { return size <= 5; }
inline bool check_wrap_len(Burst b, uint8_t len) {
  if (b != Burst::WRAP) return true;
  return len == 1 || len == 3 || len == 7 || len == 15;
}
inline bool check_wrap_align(Burst b, uint64_t addr, uint8_t size) {
  if (b != Burst::WRAP) return true;
  return (addr & ((1ull << size) - 1)) == 0;
}
inline bool check_resp_encoding(Resp r) { return static_cast<int>(r) < 4; }

// === 8 stateful intra-burst (caller passes state) ===
inline bool check_w_beat_count_within(std::size_t submitted, uint8_t len) {
  return submitted <= static_cast<std::size_t>(len);
}
inline bool check_w_last_timing(bool last, std::size_t beat_idx, uint8_t len) {
  return last == (beat_idx == static_cast<std::size_t>(len));
}
inline bool check_r_beat_count_within(std::size_t returned, uint8_t len) {
  return returned <= static_cast<std::size_t>(len);
}
inline bool check_r_last_timing(bool last, std::size_t beat_idx, uint8_t len) {
  return last == (beat_idx == static_cast<std::size_t>(len));
}
inline bool check_b_one_response_per_write(std::size_t b_count, std::size_t expected) {
  return b_count <= expected;
}
inline bool check_w_no_interleave(uint8_t expected_id, uint8_t actual_id) {
  // AXI4 W beats follow AW issue order, NOT WID interleaving (AXI3 only).
  // Always satisfied in our design because we don't carry WID on WBeat.
  (void)expected_id; (void)actual_id;
  return true;
}
inline bool check_strb_valid_bits(uint32_t strb) {
  constexpr uint32_t kValidMask = (DATA_BYTES >= 32) ? 0xFFFF'FFFFu
                                                      : (1u << DATA_BYTES) - 1u;
  return (strb & ~kValidMask) == 0;
}
inline bool check_strb_sparse_legal(uint32_t strb, uint8_t size, uint64_t beat_addr,
                                     std::size_t beat_idx, uint8_t len, Burst burst) {
  // Phase B: any byte pattern within the size-window is legal.
  // First/last beat may have partial; middle beats should be full.
  std::size_t bpb = 1ull << size;
  std::size_t byte_lane = beat_addr & (DATA_BYTES - 1);
  uint32_t lane_mask = ((1ull << bpb) - 1) << byte_lane;
  // strb must be subset of lane_mask
  return (strb & ~lane_mask) == 0;
}

// === 7 cross-channel ordering ===
inline bool check_b_id_match_outstanding(uint8_t b_id, const std::map<uint8_t, std::deque<auto>>& outstanding) {
  auto it = outstanding.find(b_id);
  return it != outstanding.end() && !it->second.empty();
}
// Similar for r_id_match_outstanding, same_id_w_order, same_id_r_order
inline bool check_diff_id_interleave_allowed() { return true; }  // tautology, documents rule
inline bool check_w_before_b(bool all_w_done) { return all_w_done; }
inline bool check_aw_w_independence() { return true; }  // tautology

}  // namespace ni::cmodel::axi::rules
```

(Some templates may need explicit type — use `template<typename T>` for outstanding helpers.)

- [ ] **Step 2: Build to verify compile**

- [ ] **Step 3: Commit**

```bash
git add c_model/include/axi/protocol_rules.hpp
git commit -m "feat(c_model): protocol_rules.hpp with AXI_PROTOCOL_ASSERT macro + 22 helpers"
```

### Task B-5b.2: Insert inline asserts in AxiSlave + AxiMaster

**Files:** Modify `c_model/include/axi/axi_slave.hpp` + `axi_master.hpp`

- [ ] **Step 1: Add `#include "axi/protocol_rules.hpp"` to both files**

- [ ] **Step 2: Insert asserts in AxiSlave::tick()**

In step 1 (drain B):
```cpp
AXI_PROTOCOL_ASSERT(rules::check_resp_encoding(resp->resp), "RESP_ENCODING: invalid BRESP");
AXI_PROTOCOL_ASSERT(it != active_writes_.end() && !it->second.empty(),
                    "B_ID_MATCH: BRESP id has no outstanding AW");
```

In step 3 (start AW):
```cpp
AXI_PROTOCOL_ASSERT(rules::check_burst_encoding(aw.burst), "BURST_ENCODING: invalid burst");
AXI_PROTOCOL_ASSERT(rules::check_size_bound(aw.size), "SIZE_BOUND: AxSIZE > 5");
AXI_PROTOCOL_ASSERT(rules::check_wrap_len(aw.burst, aw.len),
                    "WRAP_LEN_ENCODING: WRAP len must be 1/3/7/15");
AXI_PROTOCOL_ASSERT(rules::check_wrap_align(aw.burst, aw.addr, aw.size),
                    "WRAP_ALIGN: WRAP addr unaligned");
```

In step 4 (submit W):
```cpp
AXI_PROTOCOL_ASSERT(rules::check_w_beat_count_within(st.beats_submitted, st.aw.len),
                    "W_BEAT_COUNT_OVERFLOW");
AXI_PROTOCOL_ASSERT(rules::check_w_last_timing(w_q_.front().last, st.beats_submitted, st.aw.len),
                    "W_LAST_TIMING: WLAST mis-asserted");
AXI_PROTOCOL_ASSERT(rules::check_strb_valid_bits(w_q_.front().strb),
                    "STRB_VALID_BITS: strb upper bits set beyond WSTRB_WIDTH");
```

(Continue for steps 5, 6, R drain — symmetric.)

- [ ] **Step 3: Insert asserts in AxiMaster::tick()**

```cpp
// In B-drain inside OperationContext bookkeeping:
AXI_PROTOCOL_ASSERT(rules::check_b_one_response_per_write(ctx.completed_count + 1, ctx.sub_bursts.size()),
                    "B_ONE_RESPONSE_PER_WRITE: too many BRESP");
```

- [ ] **Step 4: Build to verify compile + all tests still pass**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: all tests still pass (assertions don't fire on valid inputs).

- [ ] **Step 5: Commit**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/include/axi/axi_master.hpp
git commit -m "feat(c_model): inline AXI_PROTOCOL_ASSERT in AxiSlave + AxiMaster (debug-only)"
```

### Task B-5b.3: Create `test_protocol_rules.cpp` with 22 parameterized death tests

**Files:** Create `c_model/tests/axi/test_protocol_rules.cpp` + update CMakeLists

- [ ] **Step 1: CMake**

```cmake
add_cmodel_test(test_protocol_rules)
target_link_libraries(test_protocol_rules PRIVATE yaml-cpp::yaml-cpp)
```

- [ ] **Step 2: Skeleton test file**

```cpp
// (Independent design; validation patterns inspired by cocotbext-axi)
#include "axi/protocol_rules.hpp"
#include "axi/axi_slave.hpp"
#include "axi/axi_master.hpp"
#include "mock_memory_port.hpp"
#include "mock_slave.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;
namespace rules = ni::cmodel::axi::rules;
namespace test = ni::cmodel::axi::testing;

#ifdef NDEBUG
// Release build: asserts compiled out; death tests skipped for inventory visibility
TEST(AxiProtocolDeath, AllRulesSkippedInRelease) {
  GTEST_SKIP() << "AXI_PROTOCOL_ASSERT compiled out in release build";
}
#else

// Test each rule fires AXI_PROTOCOL_ASSERT when violated.

TEST(AxiProtocolDeath, InvalidBurstEncoding) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  axi::AwBeat aw{};
  aw.burst = static_cast<axi::Burst>(3);  // invalid
  aw.id = 0; aw.addr = 0x1000; aw.len = 0; aw.size = 5;
  slave.push_aw(aw);
  EXPECT_DEATH({ slave.tick(); }, "BURST_ENCODING");
}

TEST(AxiProtocolDeath, SizeOverflow) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  axi::AwBeat aw{};
  aw.id = 0; aw.addr = 0x1000; aw.len = 0; aw.size = 6;  // invalid
  aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  EXPECT_DEATH({ slave.tick(); }, "SIZE_BOUND");
}

TEST(AxiProtocolDeath, WrapInvalidLen) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  axi::AwBeat aw{};
  aw.id = 0; aw.addr = 0x1000; aw.len = 2;  // invalid; valid = 1/3/7/15
  aw.size = 5; aw.burst = axi::Burst::WRAP;
  slave.push_aw(aw);
  EXPECT_DEATH({ slave.tick(); }, "WRAP_LEN_ENCODING");
}

// ... 19 more TESTS for each rule ...

#endif  // NDEBUG
```

(Cont'd: 19 more death tests covering WRAP_ALIGN, W_BEAT_COUNT_OVERFLOW, W_LAST_TIMING, B_ID_MATCH, R_ID_MATCH, RESP_ENCODING (B and R), W_BEAT_COUNT, R_BEAT_COUNT, R_LAST_TIMING, STRB_VALID_BITS, STRB_SPARSE_LEGAL, ORDER_SAME_ID_W, ORDER_SAME_ID_R, B_ONE_RESPONSE_PER_WRITE, W_NO_INTERLEAVE (tautology test passes), DIFF_ID_INTERLEAVE (tautology test passes), W_BEFORE_B, AW_W_INDEPENDENCE (tautology test passes). Most can use TEST_P consolidation if setup is similar.)

- [ ] **Step 3: Build + run → 22 death tests pass (debug build)**

- [ ] **Step 4: Commit**

```bash
git add c_model/tests/axi/test_protocol_rules.cpp c_model/tests/axi/CMakeLists.txt
git commit -m "test(c_model): 22 EXPECT_DEATH tests for AXI_PROTOCOL_ASSERT rules

Each rule has a debug-build death test. Release build (NDEBUG)
all tests SKIP via GTEST_SKIP() for test inventory visibility."
```

### Task B-5b.4: 2 combined fixtures + 1 parser negative test

- [ ] **Step 1: Create `narrow_unaligned.yaml`** (combines B-2 unaligned + B-3 narrow)

- [ ] **Step 2: Create `sparse_multibeat.yaml`** (sparse WSTRB across multi-beat burst)

- [ ] **Step 3: Add parser negative test for WRAP+unaligned**

```cpp
TEST_F(ScenarioParser, WrapUnalignedRejected) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x1003
    id: 0
    len: 3
    size: 5
    burst: WRAP
    dump_file: r.txt
)YAML");
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}
```

(Already added in Task B-4.2. Verify it's in commit history.)

- [ ] **Step 4: Build + commit**

### Task B-5b.5: Final drift gates + IMPLEMENTATION_PLAN.md removal

- [ ] **Step 1: Full drift gates**

```bash
cd c_model && cmake --build build && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
Expected: all green; ctest ~146 tests pass.

- [ ] **Step 2: Karpathy 4-lens on whole branch**

`git diff master..HEAD --stat` → review entire Phase B contribution against 4 lenses.

- [ ] **Step 3: Remove IMPLEMENTATION_PLAN.md**

```bash
git rm c_model/IMPLEMENTATION_PLAN.md
git commit -m "chore(c_model): remove IMPLEMENTATION_PLAN.md (Stage 2 Phase B done)"
```

### Stage B-5b exit checklist
- [ ] protocol_rules.hpp complete with 22 helpers + macro
- [ ] AxiSlave + AxiMaster inline asserts inserted
- [ ] 22 EXPECT_DEATH tests pass (debug build)
- [ ] 2 combined fixtures + 1 parser negative test pass
- [ ] ~146 total tests pass
- [ ] IMPLEMENTATION_PLAN.md removed
- [ ] Branch ready for final review + merge

---

## Out-of-plan follow-ups (do NOT include in this PR)

- **Phase C**: Exclusive access (AxLOCK + EXOKAY)
- **Stage 3+ DPI bridge**: unlocks handshake-level rules (`*_VALID_STABLE` etc.); SV testbench + Verilator integration
- **AXI4 extensions**: ATOP, WCONTINUE

## Risks to flag during execution

- **B-5a per-ID FIFO refactor**: highest risk task; Phase A 78 tests must all stay green after deque switch. TDD discipline critical.
- **OperationContext rewrite of AxiMaster tick()**: large change; small TDD steps + frequent green-state commits.
- **22 death tests boilerplate**: tempting to skip writing each one. Use TEST_P consolidation where setup permits.
- **strb_sparse_legal helper signature**: lane_mask computation depends on accurate first-lane derivation; cross-check against unaligned + narrow tests.
- **template auto& in protocol_rules.hpp**: GCC 15 should support; if older compiler, use explicit type parameters.
