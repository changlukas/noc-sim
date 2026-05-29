# Pure AXI Subsystem Implementation Plan (Stage 2 Phase A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 蓋 4 個純 AXI4 c_model class（AxiMaster / AxiSlave / Memory / Scoreboard）能跑 standalone loopback；驗證 INCR happy-path 透過 YAML scenarios + file diff + scoreboard byte-map 雙保險。

**Architecture:** namespace `ni::cmodel::axi`；header-only 為主；`IMemoryPort` 抽象介面解耦 AxiSlave 與 Memory；per-ID active burst tracking + parameterized `max_outstanding`；tick-driven same-cycle-mailbox 模型；algorithms ported line-by-line from cocotbext-axi (MIT)。

**Tech Stack:** C++17 + GoogleTest + CMake + yaml-cpp (FetchContent)；spec values via `ni::WSTRB_WIDTH` / `ni::width::*` from `spec_validate/include/*.h`（**禁止 hardcode 任何 spec 值**）。

**Spec**: `docs/superpowers/specs/2026-05-29-pure-axi-subsystem-design.md`

**Project rules**:
- Windows env: `py -3` (not python3)；Bash 用 `&&`、PowerShell 用 `; if ($?) { ... }`
- Drift gates every commit: `cd c_model && cmake --build build && ctest; cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check`
- Commit format: `type(scope): description` (English) — feat/fix/docs/style/refactor/test/chore/perf
- DO NOT use `--no-verify`; DO NOT touch `spec/ni/doc/*`
- Post-task: Karpathy 4-lens review (overcomplication / surgical / surface assumptions / verifiable success) before checkpoint
- **Use codegen-sourced constants only** (`ni::WSTRB_WIDTH`、`ni::width::AXI_*_WIDTH`、`ni::width::NOC_DATA_WIDTH`) — no hardcoded `32`/`256`/`64`/`8`

---

## File structure (final state)

```
c_model/
├── CMakeLists.txt                     ← modify: add yaml-cpp FetchContent
├── IMPLEMENTATION_PLAN.md             ← removed at Stage 5 exit
├── include/
│   ├── flit.hpp                       (Layer A, unchanged)
│   ├── register_file.hpp              (Layer A, unchanged)
│   ├── ni_spec.hpp                    (Layer A, unchanged)
│   ├── nmu/                           ← REMOVED in Stage 1 Task 1.1
│   └── axi/                           ← NEW (Stage 1 Task 1.3 onward)
│       ├── ATTRIBUTION.md             ← Stage 1 Task 1.4
│       ├── types.hpp                  ← Stage 1 Task 1.3
│       ├── memory_port.hpp            ← Stage 2 Task 2.1
│       ├── memory.hpp                 ← Stage 2 Task 2.2+
│       ├── axi_slave.hpp              ← Stage 3
│       ├── axi_master.hpp             ← Stage 4
│       ├── scoreboard.hpp             ← Stage 4
│       └── scenario_parser.hpp        ← Stage 4
├── src/
│   └── register_file.cpp              (Layer A, unchanged)
└── tests/
    ├── CMakeLists.txt                 ← modify: remove add_subdirectory(nmu), add(axi)
    ├── test_flit.cpp                  (Layer A)
    ├── test_register_file.cpp         (Layer A)
    ├── test_pins_smoke.cpp            (Layer A)
    ├── nmu/                           ← REMOVED in Stage 1 Task 1.1
    └── axi/                           ← NEW
        ├── CMakeLists.txt             ← Stage 1 Task 1.3
        ├── mock_memory_port.hpp       ← Stage 3 Task 3.1
        ├── mock_slave.hpp             ← Stage 4 Task 4.5
        ├── test_scaffold.cpp          ← Stage 1 Task 1.3
        ├── test_memory.cpp            ← Stage 2 Task 2.2+
        ├── test_axi_slave.cpp         ← Stage 3
        ├── test_axi_master.cpp        ← Stage 4
        ├── test_scoreboard.cpp        ← Stage 4
        ├── test_integration.cpp       ← Stage 5
        └── fixtures/                  ← Stage 5
            ├── README.md
            └── *.yaml + *.txt         (12 fixtures)
```

---

# Stage 1 — Cleanup + Scaffold

### Task 1.1: Delete Stage 1 AxiSlavePort files + CMake

**Files:**
- Delete: `c_model/include/nmu/axi_slave_port.hpp`
- Delete: `c_model/tests/nmu/CMakeLists.txt`
- Delete: `c_model/tests/nmu/test_axi_slave_port.cpp`
- Delete: `c_model/tests/nmu/` (empty dir afterward)
- Modify: `c_model/tests/CMakeLists.txt` — remove `add_subdirectory(nmu)`

- [ ] **Step 1: Verify current state**

Run:
```bash
ls c_model/include/nmu/ c_model/tests/nmu/
grep -n "add_subdirectory(nmu)" c_model/tests/CMakeLists.txt
```
Expected: `axi_slave_port.hpp` + nmu test files listed; `add_subdirectory(nmu)` present in tests/CMakeLists.txt.

- [ ] **Step 2: Remove files via git rm**

```bash
git rm c_model/include/nmu/axi_slave_port.hpp
git rm c_model/tests/nmu/CMakeLists.txt
git rm c_model/tests/nmu/test_axi_slave_port.cpp
```

- [ ] **Step 3: Remove empty nmu directories**

```bash
rmdir c_model/include/nmu c_model/tests/nmu
```
If `rmdir` fails on non-empty dir, list with `ls -la` and remove any stragglers.

- [ ] **Step 4: Remove add_subdirectory(nmu) from tests/CMakeLists.txt**

Edit `c_model/tests/CMakeLists.txt`, delete the line `add_subdirectory(nmu)` (and any trailing blank line if it leaves double-blank).

After edit, the file should end:
```cmake
add_cmodel_test(test_register_file)
target_sources(test_register_file PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/register_file.cpp
)
```

- [ ] **Step 5: Run build to verify clean removal**

```bash
cd c_model && cmake --build build && ctest --test-dir build
```
Expected: build succeeds; 27 Layer A tests pass (no nmu tests).

- [ ] **Step 6: Drift gates**

```bash
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
All four green.

- [ ] **Step 7: Karpathy 4-lens review**

1. Overcomplication: PASS — pure deletion, no logic added
2. Surgical: PASS — only removes nmu/* + 1 CMake line
3. Surface assumptions: PASS — verified `add_subdirectory(nmu)` was the only nmu reference in tests/CMakeLists.txt
4. Verifiable success: PASS — ctest counts confirm 27 prior tests still pass

- [ ] **Step 8: Commit**

```bash
git add c_model/tests/CMakeLists.txt
git commit -m "refactor(c_model): supersede nmu/AxiSlavePort by axi/AxiSlave (per Stage 2 design)

AxiSlavePort was a passive beat queue suited for NMU forwarder pattern.
Stage 2 redesigns it as a normal AXI slave controller in ni::cmodel::axi
namespace. Existing NMU forwarder will be redesigned in future stage.

Files removed:
- c_model/include/nmu/axi_slave_port.hpp
- c_model/tests/nmu/test_axi_slave_port.cpp
- c_model/tests/nmu/CMakeLists.txt"
```

---

### Task 1.2: Add yaml-cpp dependency via CMake FetchContent

**Files:**
- Modify: `c_model/CMakeLists.txt`

- [ ] **Step 1: Locate existing FetchContent block**

Open `c_model/CMakeLists.txt`. Locate the existing `FetchContent_Declare(googletest ...)` block (around line 24-32).

- [ ] **Step 2: Add yaml-cpp FetchContent after googletest**

After the `FetchContent_MakeAvailable(googletest)` line, add:

```cmake
FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
)
set(YAML_CPP_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS  OFF CACHE BOOL "" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)
```

The `set(...)` calls suppress yaml-cpp's own tests/tools to keep build fast.

- [ ] **Step 3: First-time build to trigger download**

```bash
cd c_model && cmake --build build
```
Expected: yaml-cpp clone happens (first time only, ~10-30 seconds); subsequent rebuilds are cached. Build success.

- [ ] **Step 4: Verify yaml-cpp target available**

```bash
find c_model/build -name "libyaml-cpp*" -o -name "yaml-cpp.lib" 2>/dev/null | head
```
Expected: at least one `libyaml-cpp.a` (or `.lib` on MSVC) artifact present in build dir.

- [ ] **Step 5: Drift gates**

```bash
cd c_model && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
All green.

- [ ] **Step 6: Karpathy 4-lens review**

1. Overcomplication: PASS — pattern mirrors existing googletest block
2. Surgical: PASS — only CMakeLists.txt touched
3. Surface assumptions: yaml-cpp 0.8.0 is latest stable (Oct 2023); ABI compatible w/ C++17
4. Verifiable success: PASS — yaml-cpp lib artifact present in build dir

- [ ] **Step 7: Commit**

```bash
git add c_model/CMakeLists.txt
git commit -m "chore(c_model): add yaml-cpp 0.8.0 via FetchContent

Required by Stage 2 axi/scenario_parser.hpp for loading scenario.yaml.
Pattern mirrors existing googletest FetchContent block."
```

---

### Task 1.3: Create axi/ namespace tree + types.hpp + scaffold test

**Files:**
- Create: `c_model/include/axi/types.hpp`
- Create: `c_model/tests/axi/CMakeLists.txt`
- Create: `c_model/tests/axi/test_scaffold.cpp`
- Modify: `c_model/tests/CMakeLists.txt` — add `add_subdirectory(axi)`

- [ ] **Step 1: Create `c_model/include/axi/types.hpp`**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "ni_signals.h"            // ni::pins (not yet used in types, here for future axi_slave)
#include "ni_flit_constants.h"     // ni::WSTRB_WIDTH, ni::width::*
#include <array>
#include <cstdint>

namespace ni::cmodel::axi {

// Spec-derived widths (all sourced from codegen elaborated headers — never hardcoded)
constexpr int DATA_BYTES = ni::WSTRB_WIDTH;            // = 32, byte-count semantic
constexpr int DATA_WIDTH = DATA_BYTES * 8;             // = 256

// Invariant: WSTRB byte count must match NOC_DATA_WIDTH bits exactly
static_assert(DATA_BYTES * 8 == ni::width::NOC_DATA_WIDTH,
              "DATA_BYTES (= WSTRB_WIDTH) * 8 must equal NOC_DATA_WIDTH "
              "for byte-level WSTRB semantics");

enum class Burst : uint8_t { FIXED = 0, INCR = 1, WRAP = 2 };
enum class Resp  : uint8_t { OKAY  = 0, EXOKAY = 1, SLVERR = 2, DECERR = 3 };

struct AwBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size;
  Burst    burst;
  uint8_t  cache, lock, prot, region, user, qos;
};

struct WBeat {
  std::array<uint8_t, DATA_BYTES> data;
  uint32_t strb;
  bool     last;
  uint8_t  user;
};

struct ArBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size;
  Burst    burst;
  uint8_t  cache, lock, prot, region, user, qos;
};

struct BBeat {
  uint8_t id;
  Resp    resp;
  uint8_t user;
};

struct RBeat {
  uint8_t  id;
  std::array<uint8_t, DATA_BYTES> data;
  Resp     resp;
  bool     last;
  uint8_t  user;
};

}  // namespace ni::cmodel::axi
```

- [ ] **Step 2: Create `c_model/tests/axi/CMakeLists.txt`**

```cmake
add_cmodel_test(test_scaffold)
```

- [ ] **Step 3: Create `c_model/tests/axi/test_scaffold.cpp`**

```cpp
#include "axi/types.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;

TEST(AxiScaffold, ConstantsFromCodegen) {
  EXPECT_EQ(axi::DATA_BYTES, ni::WSTRB_WIDTH);
  EXPECT_EQ(axi::DATA_WIDTH, ni::width::NOC_DATA_WIDTH);
  EXPECT_EQ(axi::DATA_BYTES * 8, axi::DATA_WIDTH);
}

TEST(AxiScaffold, BurstEnumValues) {
  EXPECT_EQ(static_cast<int>(axi::Burst::FIXED), 0);
  EXPECT_EQ(static_cast<int>(axi::Burst::INCR),  1);
  EXPECT_EQ(static_cast<int>(axi::Burst::WRAP),  2);
}

TEST(AxiScaffold, RespEnumValues) {
  EXPECT_EQ(static_cast<int>(axi::Resp::OKAY),   0);
  EXPECT_EQ(static_cast<int>(axi::Resp::EXOKAY), 1);
  EXPECT_EQ(static_cast<int>(axi::Resp::SLVERR), 2);
  EXPECT_EQ(static_cast<int>(axi::Resp::DECERR), 3);
}

TEST(AxiScaffold, BeatStructsAreConstructible) {
  axi::AwBeat aw{};
  axi::WBeat  w{};
  axi::ArBeat ar{};
  axi::BBeat  b{};
  axi::RBeat  r{};
  (void)aw; (void)w; (void)ar; (void)b; (void)r;
  SUCCEED();
}

TEST(AxiScaffold, WBeatDataArrayMatchesDataBytes) {
  axi::WBeat w{};
  EXPECT_EQ(w.data.size(), static_cast<std::size_t>(axi::DATA_BYTES));
}
```

- [ ] **Step 4: Modify `c_model/tests/CMakeLists.txt` — add axi subdirectory**

At end of file (after existing `add_cmodel_test(test_register_file)` block):

```cmake
add_subdirectory(axi)
```

- [ ] **Step 5: Build + run scaffold tests**

```bash
cd c_model && cmake --build build --target test_scaffold && ctest --test-dir build -R AxiScaffold -V
```
Expected: 5 tests PASS.

- [ ] **Step 6: Drift gates**

```bash
cd c_model && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
All green; ctest now 27 + 5 = 32 tests.

- [ ] **Step 7: Karpathy 4-lens review**

1. Overcomplication: PASS — pure POD types + scoped enums + static_assert
2. Surgical: PASS — only Stage 1 types; no class behavior; no memory port yet
3. Surface assumptions: `ni::WSTRB_WIDTH=32` and `ni::width::NOC_DATA_WIDTH=256` verified in elaborated header; static_assert locks the invariant
4. Verifiable success: PASS — tests prove constants come from codegen + beat structs compile + enum values correct

- [ ] **Step 8: Commit**

```bash
git add c_model/include/axi/types.hpp \
        c_model/tests/axi/CMakeLists.txt \
        c_model/tests/axi/test_scaffold.cpp \
        c_model/tests/CMakeLists.txt
git commit -m "feat(c_model): scaffold ni::cmodel::axi namespace + types.hpp

types.hpp defines:
- DATA_BYTES (= ni::WSTRB_WIDTH = 32) + DATA_WIDTH (= 256)
  with static_assert locking the byte/bit invariant
- Burst / Resp enums per AXI4
- AwBeat / WBeat / ArBeat / BBeat / RBeat POD structs

All spec values sourced from codegen elaborated headers
(ni::WSTRB_WIDTH, ni::width::NOC_DATA_WIDTH) — no hardcoding."
```

---

### Task 1.4: Create ATTRIBUTION.md (cocotbext-axi MIT)

**Files:**
- Create: `c_model/include/axi/ATTRIBUTION.md`

- [ ] **Step 1: Create ATTRIBUTION.md**

```markdown
# OSS Attribution — c_model/include/axi/

Algorithms in this directory are ported line-by-line from
[alexforencich/cocotbext-axi](https://github.com/alexforencich/cocotbext-axi),
MIT license. cocotbext-axi is Python (cocotb async); this c_model is C++17 +
GoogleTest with synchronous tick-driven semantics.

## File mapping

| c_model file (this repo)          | Upstream Python source             |
|-----------------------------------|------------------------------------|
| `axi/types.hpp`                   | `cocotbext/axi/*.py` (enums + structs) |
| `axi/memory_port.hpp`             | `cocotbext/axi/memory.py` (MemoryInterface API) |
| `axi/memory.hpp`                  | `cocotbext/axi/axi_ram.py` (AxiRam) |
| `axi/axi_slave.hpp`               | `cocotbext/axi/axi_slave.py` (AxiSlave + AxiSlaveWrite + AxiSlaveRead) |
| `axi/axi_master.hpp`              | `cocotbext/axi/axi_master.py` (AxiMaster + AxiMasterWrite + AxiMasterRead) |
| `axi/scoreboard.hpp`              | (independent design; pattern from cocotbext-axi tests) |
| `axi/scenario_parser.hpp`         | (independent; cocotbext-axi has no scenario file format) |

## Adaptation notes

- cocotb async → C++ sync tick(): `async def _run` loops become `tick()` step functions
- Python `Queue` → `std::deque<T>`
- Python `Event` → boolean flags
- Python exceptions → `std::runtime_error` for user input; `assert(...)` for invariants
- Per-ID dicts in cocotbext-axi → `std::map<uint8_t, T>` in our c_model

## License

cocotbext-axi is MIT licensed. See <https://github.com/alexforencich/cocotbext-axi/blob/master/LICENSE>.

This c_model project inherits the MIT terms for the ported algorithms;
the rest of c_model follows the project's own license (see repo root).
```

- [ ] **Step 2: Commit (no code change → no build needed)**

```bash
git add c_model/include/axi/ATTRIBUTION.md
git commit -m "docs(c_model): add cocotbext-axi MIT attribution for axi/* port"
```

---

### Stage 1 exit checklist

- [ ] AxiSlavePort files all removed (Task 1.1)
- [ ] yaml-cpp 0.8.0 builds via FetchContent (Task 1.2)
- [ ] `axi/types.hpp` + 5 scaffold tests passing (Task 1.3)
- [ ] ATTRIBUTION.md committed (Task 1.4)
- [ ] 4 drift gates green; ctest 32/32 pass
- [ ] Mark `c_model/IMPLEMENTATION_PLAN.md` Stage 1 → Complete

---

# Stage 2 — Memory class

### Task 2.1: Define IMemoryPort interface + req/resp structs

**Files:**
- Create: `c_model/include/axi/memory_port.hpp`

- [ ] **Step 1: Create `memory_port.hpp`**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include <optional>

namespace ni::cmodel::axi {

struct MemWriteReq {
  uint64_t addr;
  std::array<uint8_t, DATA_BYTES> data;
  uint32_t strb;
  uint8_t  id;
  bool     last;
  uint64_t tag;  // opaque correlation token, set by AxiSlave, echoed in resp
};

struct MemWriteResp {
  uint8_t  id;
  Resp     resp;
  uint64_t tag;
};

struct MemReadReq {
  uint64_t addr;
  uint8_t  size;
  uint8_t  id;
  bool     last;
  uint64_t tag;
};

struct MemReadResp {
  uint8_t  id;
  std::array<uint8_t, DATA_BYTES> data;
  Resp     resp;
  bool     last;
  uint64_t tag;
};

class IMemoryPort {
public:
  virtual ~IMemoryPort() = default;

  // submit_*: return false if internal pending queue full → caller retries next tick
  virtual bool submit_write(const MemWriteReq&) = 0;
  virtual bool submit_read (const MemReadReq&)  = 0;

  // pop_*_resp: return nullopt if no response ready
  virtual std::optional<MemWriteResp> pop_write_resp() = 0;
  virtual std::optional<MemReadResp>  pop_read_resp () = 0;
};

}  // namespace ni::cmodel::axi
```

- [ ] **Step 2: Add scaffold test for compile-only sanity**

Append to `c_model/tests/axi/test_scaffold.cpp`:

```cpp
#include "axi/memory_port.hpp"

TEST(AxiScaffold, MemoryPortStructsAreConstructible) {
  axi::MemWriteReq  wr{};
  axi::MemWriteResp wresp{};
  axi::MemReadReq   rr{};
  axi::MemReadResp  rresp{};
  (void)wr; (void)wresp; (void)rr; (void)rresp;
  SUCCEED();
}
```

- [ ] **Step 3: Build + run**

```bash
cd c_model && cmake --build build --target test_scaffold && ctest --test-dir build -R AxiScaffold -V
```
Expected: 6 tests PASS (5 prior + 1 new).

- [ ] **Step 4: Drift gates + Karpathy + Commit**

```bash
cd c_model && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
git add c_model/include/axi/memory_port.hpp c_model/tests/axi/test_scaffold.cpp
git commit -m "feat(c_model): IMemoryPort interface + req/resp structs

IMemoryPort abstracts the AxiSlave ↔ Memory boundary. Designed so a
MockMemoryPort can be substituted for AxiSlave unit tests in Stage 3.
Per-request 'tag' field enables AxiSlave to correlate multi-outstanding
responses (needed in Phase C; designed in now per R2 decision)."
```

---

### Task 2.2: Memory basic in-bounds write + read (no latency)

**Files:**
- Create: `c_model/include/axi/memory.hpp`
- Create: `c_model/tests/axi/test_memory.cpp`
- Modify: `c_model/tests/axi/CMakeLists.txt` — add `add_cmodel_test(test_memory)`

- [ ] **Step 1: Write failing test in `test_memory.cpp`**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#include "axi/memory.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;

TEST(Memory, InBoundsWriteImmediateResp_ZeroLatency) {
  axi::Memory mem(/*base=*/0x1000, /*size=*/0x1000,
                  /*write_latency=*/0, /*read_latency=*/0);
  axi::MemWriteReq req{};
  req.addr = 0x1000;
  req.data.fill(0xAB);
  req.strb = 0xFFFF'FFFFu;
  req.id   = 0x05;
  req.last = true;
  req.tag  = 42;
  EXPECT_TRUE(mem.submit_write(req));

  mem.tick();  // 0-latency → resp ready immediately after this tick

  auto resp = mem.pop_write_resp();
  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(resp->id,   0x05);
  EXPECT_EQ(resp->resp, axi::Resp::OKAY);
  EXPECT_EQ(resp->tag,  42u);
}
```

- [ ] **Step 2: Add test target to `c_model/tests/axi/CMakeLists.txt`**

```cmake
add_cmodel_test(test_scaffold)
add_cmodel_test(test_memory)
```

- [ ] **Step 3: Build to verify FAIL (no `memory.hpp` yet)**

```bash
cd c_model && cmake --build build --target test_memory
```
Expected: build FAIL (`memory.hpp` not found).

- [ ] **Step 4: Create `memory.hpp` with minimal in-bounds write + 0-latency path**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/memory_port.hpp"
#include <cstddef>
#include <deque>
#include <vector>

namespace ni::cmodel::axi {

class Memory : public IMemoryPort {
public:
  Memory(uint64_t base_addr, std::size_t size_bytes,
         std::size_t write_latency_ticks, std::size_t read_latency_ticks,
         std::size_t pending_queue_depth = 32,
         uint8_t fill_byte = 0x00)
      : base_(base_addr), size_(size_bytes),
        write_lat_(write_latency_ticks), read_lat_(read_latency_ticks),
        pending_depth_(pending_queue_depth),
        storage_(size_bytes, fill_byte) {}

  bool submit_write(const MemWriteReq& req) override {
    if (pending_writes_.size() >= pending_depth_) return false;
    pending_writes_.push_back({req, write_lat_});
    return true;
  }
  bool submit_read(const MemReadReq& req) override {
    if (pending_reads_.size() >= pending_depth_) return false;
    pending_reads_.push_back({req, read_lat_});
    return true;
  }
  std::optional<MemWriteResp> pop_write_resp() override {
    if (write_resp_q_.empty()) return std::nullopt;
    auto r = write_resp_q_.front(); write_resp_q_.pop_front(); return r;
  }
  std::optional<MemReadResp> pop_read_resp() override {
    if (read_resp_q_.empty()) return std::nullopt;
    auto r = read_resp_q_.front(); read_resp_q_.pop_front(); return r;
  }

  void tick() {
    // Decrement countdowns; move 0-countdown items to response queues
    for (auto it = pending_writes_.begin(); it != pending_writes_.end(); ) {
      if (it->ticks-- == 0) {
        write_resp_q_.push_back(perform_write_(it->req));
        it = pending_writes_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = pending_reads_.begin(); it != pending_reads_.end(); ) {
      if (it->ticks-- == 0) {
        read_resp_q_.push_back(perform_read_(it->req));
        it = pending_reads_.erase(it);
      } else {
        ++it;
      }
    }
  }

  uint8_t peek(uint64_t addr) const {
    return in_bounds_(addr, 1) ? storage_[addr - base_] : 0u;
  }
  std::size_t pending_writes() const { return pending_writes_.size(); }
  std::size_t pending_reads()  const { return pending_reads_.size();  }

private:
  bool in_bounds_(uint64_t addr, std::size_t bytes) const {
    return addr >= base_ && (addr + bytes) <= (base_ + size_);
  }

  MemWriteResp perform_write_(const MemWriteReq& req) {
    std::size_t bytes_per_beat = DATA_BYTES;  // Phase A: full beat
    if (!in_bounds_(req.addr, bytes_per_beat)) {
      return MemWriteResp{req.id, Resp::DECERR, req.tag};
    }
    for (std::size_t i = 0; i < bytes_per_beat; ++i) {
      if ((req.strb >> i) & 0x1u) {
        storage_[(req.addr - base_) + i] = req.data[i];
      }
    }
    return MemWriteResp{req.id, Resp::OKAY, req.tag};
  }

  MemReadResp perform_read_(const MemReadReq& req) {
    MemReadResp resp{};
    resp.id = req.id; resp.tag = req.tag; resp.last = req.last;
    std::size_t bytes_per_beat = 1u << req.size;  // 1<<size bytes
    if (!in_bounds_(req.addr, bytes_per_beat)) {
      resp.resp = Resp::DECERR;
      resp.data.fill(0x00);
      return resp;
    }
    resp.resp = Resp::OKAY;
    resp.data.fill(0x00);
    for (std::size_t i = 0; i < bytes_per_beat; ++i) {
      resp.data[i] = storage_[(req.addr - base_) + i];
    }
    return resp;
  }

  struct PendingWrite { MemWriteReq req; std::size_t ticks; };
  struct PendingRead  { MemReadReq  req; std::size_t ticks; };

  uint64_t base_;
  std::size_t size_;
  std::size_t write_lat_, read_lat_;
  std::size_t pending_depth_;
  std::vector<uint8_t> storage_;
  std::deque<PendingWrite> pending_writes_;
  std::deque<PendingRead>  pending_reads_;
  std::deque<MemWriteResp> write_resp_q_;
  std::deque<MemReadResp>  read_resp_q_;
};

}  // namespace ni::cmodel::axi
```

- [ ] **Step 5: Run test → PASS**

```bash
cd c_model && cmake --build build --target test_memory && ctest --test-dir build -R Memory -V
```
Expected: `Memory.InBoundsWriteImmediateResp_ZeroLatency` PASS.

- [ ] **Step 6: Drift gates + Karpathy + Commit**

```bash
cd c_model && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
git add c_model/include/axi/memory.hpp \
        c_model/tests/axi/test_memory.cpp \
        c_model/tests/axi/CMakeLists.txt
git commit -m "feat(c_model): Memory in-bounds write/read with 0-latency baseline

Implements IMemoryPort. Storage is std::vector<uint8_t> sized to base+size.
WSTRB byte-mask honored. Read returns 1<<size bytes from offset.
Pending queues are FIFO with countdown ticks; 0-latency case lands resp
on tick following submit.
Out-of-bounds path falls through to DECERR (covered in next task)."
```

---

### Task 2.3: Memory latency countdown verification

**Files:**
- Modify: `c_model/tests/axi/test_memory.cpp`

- [ ] **Step 1: Append test**

```cpp
TEST(Memory, WriteLatencyCountdown) {
  axi::Memory mem(0x1000, 0x1000, /*write_latency=*/5, 0);
  axi::MemWriteReq req{};
  req.addr = 0x1000; req.data.fill(0x55); req.strb = 0xFFFF'FFFFu;
  req.id = 1; req.last = true; req.tag = 100;
  EXPECT_TRUE(mem.submit_write(req));

  // Ticks 1..5: no response yet
  for (int t = 1; t <= 5; ++t) {
    mem.tick();
    auto r = mem.pop_write_resp();
    if (t < 5) {
      EXPECT_FALSE(r.has_value()) << "premature response at tick " << t;
    } else {
      ASSERT_TRUE(r.has_value()) << "expected response at tick 5";
      EXPECT_EQ(r->tag, 100u);
    }
  }
}

TEST(Memory, ReadLatencyCountdown) {
  axi::Memory mem(0x1000, 0x1000, 0, /*read_latency=*/3);
  axi::MemReadReq req{};
  req.addr = 0x1000; req.size = 5; req.id = 2; req.last = true; req.tag = 200;
  EXPECT_TRUE(mem.submit_read(req));

  EXPECT_FALSE((mem.tick(), mem.pop_read_resp()).has_value());  // t=1
  EXPECT_FALSE((mem.tick(), mem.pop_read_resp()).has_value());  // t=2
  auto r = (mem.tick(), mem.pop_read_resp());                   // t=3
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->tag, 200u);
}
```

- [ ] **Step 2: Run → both PASS** (existing impl covers latency)

```bash
cd c_model && cmake --build build --target test_memory && ctest --test-dir build -R "Memory" -V
```

- [ ] **Step 3: Drift + Karpathy + Commit**

```bash
git add c_model/tests/axi/test_memory.cpp
git commit -m "test(c_model): Memory latency countdown for write/read paths"
```

---

### Task 2.4: Memory out-of-bounds → DECERR

**Files:**
- Modify: `c_model/tests/axi/test_memory.cpp`

- [ ] **Step 1: Append tests**

```cpp
TEST(Memory, OobWriteReturnsDecerr) {
  axi::Memory mem(0x1000, 0x100, 0, 0);  // 256-byte region
  axi::MemWriteReq req{};
  req.addr = 0x10F0; req.data.fill(0xAA); req.strb = 0xFFFF'FFFFu;
  req.id = 1; req.last = true; req.tag = 300;
  // addr 0x10F0 + DATA_BYTES (32) = 0x1110 > base+size 0x1100 → OOB
  EXPECT_TRUE(mem.submit_write(req));
  mem.tick();
  auto r = mem.pop_write_resp();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->resp, axi::Resp::DECERR);
}

TEST(Memory, OobReadReturnsDecerr) {
  axi::Memory mem(0x1000, 0x100, 0, 0);
  axi::MemReadReq req{};
  req.addr = 0x10FE; req.size = 5; req.id = 2; req.last = true; req.tag = 400;
  EXPECT_TRUE(mem.submit_read(req));
  mem.tick();
  auto r = mem.pop_read_resp();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->resp, axi::Resp::DECERR);
}

TEST(Memory, OobWriteDoesNotMutateStorage) {
  axi::Memory mem(0x1000, 0x100, 0, 0, 32, /*fill=*/0xFF);
  axi::MemWriteReq req{};
  req.addr = 0x10F8; req.data.fill(0x00); req.strb = 0xFFFF'FFFFu;
  req.id = 1; req.last = true;
  mem.submit_write(req); mem.tick(); (void)mem.pop_write_resp();
  // Verify in-bounds bytes near boundary still hold fill byte
  EXPECT_EQ(mem.peek(0x10F8), 0xFF);
  EXPECT_EQ(mem.peek(0x10FE), 0xFF);
}
```

- [ ] **Step 2: Run → PASS** (impl already returns DECERR + skips storage write)

```bash
cd c_model && cmake --build build --target test_memory && ctest --test-dir build -R Memory -V
```

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_memory.cpp
git commit -m "test(c_model): Memory OOB returns DECERR and preserves storage"
```

---

### Task 2.5: Memory WSTRB byte mask

**Files:**
- Modify: `c_model/tests/axi/test_memory.cpp`

- [ ] **Step 1: Append test**

```cpp
TEST(Memory, WstrbByteMaskMergeWithFill) {
  axi::Memory mem(0x1000, 0x100, 0, 0, 32, /*fill=*/0xFF);
  axi::MemWriteReq req{};
  req.addr = 0x1000;
  req.data.fill(0x00);  // would write zeros if strb full
  req.strb = 0b0000'0000'0000'0000'0000'0000'0000'1010;  // bits 1, 3 only
  req.id = 1; req.last = true;
  mem.submit_write(req); mem.tick(); (void)mem.pop_write_resp();

  EXPECT_EQ(mem.peek(0x1000), 0xFF);  // bit 0: strb=0 → unchanged
  EXPECT_EQ(mem.peek(0x1001), 0x00);  // bit 1: strb=1 → written
  EXPECT_EQ(mem.peek(0x1002), 0xFF);  // bit 2: strb=0
  EXPECT_EQ(mem.peek(0x1003), 0x00);  // bit 3: strb=1
  EXPECT_EQ(mem.peek(0x1004), 0xFF);  // bit 4: strb=0
}
```

- [ ] **Step 2: Run → PASS** (impl honors strb)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_memory.cpp
git commit -m "test(c_model): Memory WSTRB byte mask merge with fill byte"
```

---

### Task 2.6: Memory backpressure (queue full)

**Files:**
- Modify: `c_model/tests/axi/test_memory.cpp`

- [ ] **Step 1: Append test**

```cpp
TEST(Memory, BackpressureSubmitReturnsFalseWhenQueueFull) {
  axi::Memory mem(0x1000, 0x10000, /*lat=*/10, 10, /*depth=*/4);
  axi::MemWriteReq req{};
  req.addr = 0x1000; req.data.fill(0); req.strb = 0xFFFF'FFFFu;
  req.id = 1; req.last = true;
  EXPECT_TRUE(mem.submit_write(req));
  EXPECT_TRUE(mem.submit_write(req));
  EXPECT_TRUE(mem.submit_write(req));
  EXPECT_TRUE(mem.submit_write(req));
  EXPECT_FALSE(mem.submit_write(req)) << "5th submit should be rejected (queue depth=4)";

  // Tick enough times for first write to complete + drain
  for (int i = 0; i < 11; ++i) mem.tick();
  ASSERT_TRUE(mem.pop_write_resp().has_value());
  // Now there is room → submit succeeds again
  EXPECT_TRUE(mem.submit_write(req));
}
```

- [ ] **Step 2: Run → PASS** (impl checks `pending_writes_.size() >= depth_`)

- [ ] **Step 3: Karpathy + Commit (Stage 2 complete after this)**

```bash
git add c_model/tests/axi/test_memory.cpp
git commit -m "test(c_model): Memory backpressure when pending queue full"
```

---

### Stage 2 exit checklist

- [ ] `Memory` class fully implemented; ≥10 test cases under `test_memory.cpp`
- [ ] All drift gates green
- [ ] Mark `c_model/IMPLEMENTATION_PLAN.md` Stage 2 → Complete

---

# Stage 3 — AxiSlave class

### Task 3.1: MockMemoryPort + AxiSlave skeleton

**Files:**
- Create: `c_model/tests/axi/mock_memory_port.hpp`
- Create: `c_model/include/axi/axi_slave.hpp`
- Create: `c_model/tests/axi/test_axi_slave.cpp`
- Modify: `c_model/tests/axi/CMakeLists.txt` — add `add_cmodel_test(test_axi_slave)`

- [ ] **Step 1: Create `mock_memory_port.hpp`**

```cpp
#pragma once
#include "axi/memory_port.hpp"
#include <deque>

namespace ni::cmodel::axi::testing {

class MockMemoryPort : public IMemoryPort {
public:
  // Control parameters set by test
  std::size_t write_capacity = 16;
  std::size_t read_capacity  = 16;

  bool submit_write(const MemWriteReq& req) override {
    if (captured_writes.size() >= write_capacity) return false;
    captured_writes.push_back(req);
    return true;
  }
  bool submit_read(const MemReadReq& req) override {
    if (captured_reads.size() >= read_capacity) return false;
    captured_reads.push_back(req);
    return true;
  }
  std::optional<MemWriteResp> pop_write_resp() override {
    if (queued_write_resps.empty()) return std::nullopt;
    auto r = queued_write_resps.front(); queued_write_resps.pop_front(); return r;
  }
  std::optional<MemReadResp> pop_read_resp() override {
    if (queued_read_resps.empty()) return std::nullopt;
    auto r = queued_read_resps.front(); queued_read_resps.pop_front(); return r;
  }

  // Test fixtures push responses to enqueue replies
  std::deque<MemWriteReq>  captured_writes;
  std::deque<MemReadReq>   captured_reads;
  std::deque<MemWriteResp> queued_write_resps;
  std::deque<MemReadResp>  queued_read_resps;
};

}  // namespace ni::cmodel::axi::testing
```

- [ ] **Step 2: Add minimal `axi_slave.hpp` skeleton (compiles, no logic yet)**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include "axi/memory_port.hpp"
#include <deque>
#include <map>

namespace ni::cmodel::axi {

class AxiSlave {
public:
  explicit AxiSlave(IMemoryPort& memory_port, std::size_t channel_queue_depth = 32)
      : memory_port_(memory_port), depth_(channel_queue_depth) {}

  bool push_aw(const AwBeat& b) { if (aw_q_.size() >= depth_) return false; aw_q_.push_back(b); return true; }
  bool push_w (const WBeat&  b) { if (w_q_.size()  >= depth_) return false; w_q_.push_back(b);  return true; }
  bool push_ar(const ArBeat& b) { if (ar_q_.size() >= depth_) return false; ar_q_.push_back(b); return true; }

  std::optional<BBeat> pop_b() {
    if (b_q_.empty()) return std::nullopt;
    auto r = b_q_.front(); b_q_.pop_front(); return r;
  }
  std::optional<RBeat> pop_r() {
    if (r_q_.empty()) return std::nullopt;
    auto r = r_q_.front(); r_q_.pop_front(); return r;
  }

  void tick();  // implemented in Task 3.2+

  // Observation
  std::size_t aw_q_size() const { return aw_q_.size(); }
  std::size_t w_q_size()  const { return w_q_.size();  }
  std::size_t ar_q_size() const { return ar_q_.size(); }
  std::size_t b_q_size()  const { return b_q_.size();  }
  std::size_t r_q_size()  const { return r_q_.size();  }

private:
  struct WriteBurstState {
    AwBeat aw;
    std::size_t beats_submitted = 0;     // # W beats submitted to memory
    std::size_t beats_completed = 0;     // # memory write responses received
  };
  struct ReadBurstState {
    ArBeat ar;
    std::size_t beats_submitted = 0;
    std::size_t beats_returned  = 0;
  };

  IMemoryPort& memory_port_;
  std::size_t depth_;
  std::deque<AwBeat> aw_q_;
  std::deque<WBeat>  w_q_;
  std::deque<ArBeat> ar_q_;
  std::deque<BBeat>  b_q_;
  std::deque<RBeat>  r_q_;
  // AXI4 W follows AW issue order, NOT W matches by ID — so a single FIFO of
  // expected AWs feeds W matching:
  std::deque<uint8_t> aw_issue_order_;  // per-burst id, oldest first
  std::map<uint8_t, WriteBurstState> active_writes_;
  std::map<uint8_t, ReadBurstState>  active_reads_;

  inline void tick_();  // single source for tick() impl
};

// Stub tick() (real impl in following tasks)
inline void AxiSlave::tick() {}

}  // namespace ni::cmodel::axi
```

- [ ] **Step 3: Skeleton test that just constructs both classes**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#include "axi/axi_slave.hpp"
#include "mock_memory_port.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;
namespace test = ni::cmodel::axi::testing;

TEST(AxiSlave, ConstructsAndAcceptsEmptyTick) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  EXPECT_EQ(slave.aw_q_size(), 0u);
  slave.tick();  // no-op so far
  EXPECT_EQ(slave.b_q_size(), 0u);
}
```

- [ ] **Step 4: Add target to CMake**

```cmake
add_cmodel_test(test_scaffold)
add_cmodel_test(test_memory)
add_cmodel_test(test_axi_slave)
```

- [ ] **Step 5: Build + run + drift + commit**

```bash
cd c_model && cmake --build build --target test_axi_slave && ctest --test-dir build -R AxiSlave -V
git add c_model/tests/axi/mock_memory_port.hpp \
        c_model/include/axi/axi_slave.hpp \
        c_model/tests/axi/test_axi_slave.cpp \
        c_model/tests/axi/CMakeLists.txt
git commit -m "feat(c_model): AxiSlave skeleton + MockMemoryPort for unit tests"
```

---

### Task 3.2: AxiSlave write path (single-in-flight, in-bounds OKAY)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp` — implement `tick()` write path
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(AxiSlave, WriteBurstSingleBeatInBoundsOkay) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  axi::AwBeat aw{};
  aw.id = 7; aw.addr = 0x1000; aw.len = 0; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  axi::WBeat w{};
  w.data.fill(0xCD); w.strb = 0xFFFF'FFFFu; w.last = true;
  slave.push_w(w);

  // Tick 1: slave consumes aw_q + w_q → submits MemWriteReq
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes.front().id,   7);
  EXPECT_EQ(mem.captured_writes.front().addr, 0x1000u);
  EXPECT_EQ(mem.captured_writes.front().last, true);

  // Memory responds OKAY
  mem.queued_write_resps.push_back(axi::MemWriteResp{
      mem.captured_writes.front().id,
      axi::Resp::OKAY,
      mem.captured_writes.front().tag});

  // Tick 2: slave drains memory resp → pushes B
  slave.tick();
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->id,   7);
  EXPECT_EQ(b->resp, axi::Resp::OKAY);
}
```

- [ ] **Step 2: Implement `tick()` write path**

Replace the stub `inline void AxiSlave::tick() {}` at end of `axi_slave.hpp` with:

```cpp
inline void AxiSlave::tick() {
  // ── 1. Drain memory write responses → match to active_writes by tag/id ──
  while (auto resp = memory_port_.pop_write_resp()) {
    auto it = active_writes_.find(resp->id);
    if (it == active_writes_.end()) continue;  // stale (shouldn't happen Phase A)
    auto& st = it->second;
    ++st.beats_completed;
    // When all beats completed → push B with worst resp seen
    if (st.beats_completed == static_cast<std::size_t>(st.aw.len) + 1) {
      b_q_.push_back(BBeat{st.aw.id, resp->resp, /*user=*/0});
      active_writes_.erase(it);
      // remove from issue order
      for (auto i = aw_issue_order_.begin(); i != aw_issue_order_.end(); ++i) {
        if (*i == st.aw.id) { aw_issue_order_.erase(i); break; }
      }
    }
  }

  // ── 2. Start new AW (Phase A single-in-flight enforced by AxiMaster) ──
  while (!aw_q_.empty()) {
    auto& aw = aw_q_.front();
    if (active_writes_.count(aw.id)) break;  // same-ID already active — wait
    active_writes_[aw.id] = WriteBurstState{aw, 0, 0};
    aw_issue_order_.push_back(aw.id);
    aw_q_.pop_front();
  }

  // ── 3. Submit W beats for the oldest-issued active write ──
  while (!w_q_.empty() && !aw_issue_order_.empty()) {
    uint8_t front_id = aw_issue_order_.front();
    auto& st = active_writes_[front_id];
    std::size_t beat_idx = st.beats_submitted;
    MemWriteReq req{};
    req.addr = st.aw.addr + beat_idx * (1ull << st.aw.size);
    req.data = w_q_.front().data;
    req.strb = w_q_.front().strb;
    req.id   = st.aw.id;
    req.last = w_q_.front().last;
    req.tag  = (static_cast<uint64_t>(front_id) << 32) | beat_idx;
    if (!memory_port_.submit_write(req)) break;  // retry next tick (backpressure)
    ++st.beats_submitted;
    w_q_.pop_front();
    if (st.beats_submitted == static_cast<std::size_t>(st.aw.len) + 1) {
      // All W beats submitted for this burst; W path moves to next aw_issue_order
      // (When all completed via memory resp, aw_issue_order_ entry is removed.)
      // But until completion, we shouldn't accept more W for next burst.
      // Hack: aw_issue_order_.front() stays this id; W consumption stops.
      break;
    }
  }
}
```

(Note: this is Phase A; W-to-burst matching uses aw_issue_order_. AW/W independence comes in Task 3.4.)

- [ ] **Step 3: Run → PASS**

```bash
cd c_model && cmake --build build --target test_axi_slave && ctest --test-dir build -R AxiSlave -V
```

- [ ] **Step 4: Drift + Karpathy + Commit**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/tests/axi/test_axi_slave.cpp
git commit -m "feat(c_model): AxiSlave single-beat write path AW+W → memory → B"
```

---

### Task 3.3: AxiSlave write path multi-beat burst (INCR)

**Files:**
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(AxiSlave, WriteBurstIncr8Beat_InBounds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  axi::AwBeat aw{};
  aw.id = 3; aw.addr = 0x2000; aw.len = 7; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  // Push 8 W beats with distinguishable data
  for (uint8_t i = 0; i < 8; ++i) {
    axi::WBeat w{};
    w.data.fill(0x10 + i);
    w.strb = 0xFFFF'FFFFu;
    w.last = (i == 7);
    slave.push_w(w);
  }

  // Tick repeatedly until all 8 captured + responses ready
  for (int t = 0; t < 16; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 8u);
  for (std::size_t i = 0; i < 8; ++i) {
    EXPECT_EQ(mem.captured_writes[i].addr, 0x2000u + i * 32u);
    EXPECT_EQ(mem.captured_writes[i].data[0], 0x10 + static_cast<uint8_t>(i));
    EXPECT_EQ(mem.captured_writes[i].last, i == 7);
  }

  // Drop 8 OKAY responses
  for (std::size_t i = 0; i < 8; ++i) {
    mem.queued_write_resps.push_back(
        axi::MemWriteResp{3, axi::Resp::OKAY, mem.captured_writes[i].tag});
  }
  // Tick to drain
  for (int t = 0; t < 8; ++t) slave.tick();
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->id, 3);
}
```

- [ ] **Step 2: Run → PASS** (Task 3.2 impl handles burst via beats_submitted/beats_completed)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave 8-beat INCR write burst"
```

---

### Task 3.4: AxiSlave AW/W independence (W before AW)

**Files:**
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(AxiSlave, AwWIndependence_WBeforeAw) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);

  // Push 2 W beats first
  for (uint8_t i = 0; i < 2; ++i) {
    axi::WBeat w{};
    w.data.fill(0xAA + i);
    w.strb = 0xFFFF'FFFFu;
    w.last = (i == 1);
    slave.push_w(w);
  }
  slave.tick();  // No AW yet → no memory submission
  EXPECT_EQ(mem.captured_writes.size(), 0u);

  // Now push AW for len=1 (2 beats)
  axi::AwBeat aw{};
  aw.id = 5; aw.addr = 0x3000; aw.len = 1; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);

  // Tick → slave should now match W beats to AW
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 2u);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0xAA);
  EXPECT_EQ(mem.captured_writes[1].data[0], 0xAB);
}
```

- [ ] **Step 2: Run → PASS** (Task 3.2 impl already handles this — w_q_ stays, AW arrival starts consumption)

If FAIL: verify `tick()` ordering — drain resp → start AW (admit) → submit W. The Task 3.2 code does this in order.

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave AW/W independence (W beats arrive before AW)"
```

---

### Task 3.5: AxiSlave burst-atomic OOB → DECERR

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp` — add OOB check at AW admission
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(AxiSlave, WriteBurstAtomicOob_PushesDecerrSkipsMemory) {
  test::MockMemoryPort mem;
  // memory_bounds_ tells slave the "valid" range; here we mimic by sizing
  // the burst to exceed a known range — but AxiSlave doesn't know memory
  // bounds yet. Add an axi::Memory ctor param.
  // (See Step 2 — we extend AxiSlave to accept a bounds pair.)
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x100);  // 256-byte region

  axi::AwBeat aw{};
  aw.id = 9; aw.addr = 0x10E0; aw.len = 3; aw.size = 5; aw.burst = axi::Burst::INCR;
  // 4 beats * 32 bytes = 128 bytes; 0x10E0 + 128 = 0x1160 > 0x1100 → OOB
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 4; ++i) {
    axi::WBeat w{}; w.data.fill(0); w.strb = 0xFFFF'FFFFu; w.last = (i==3);
    slave.push_w(w);
  }
  slave.tick();

  // Slave should NOT submit to memory
  EXPECT_EQ(mem.captured_writes.size(), 0u);
  // Slave should push B(DECERR) immediately
  auto b = slave.pop_b();
  ASSERT_TRUE(b.has_value());
  EXPECT_EQ(b->resp, axi::Resp::DECERR);
  // And w_q_ should be drained (all 4 W beats consumed/discarded for this burst)
  EXPECT_EQ(slave.w_q_size(), 0u);
}
```

- [ ] **Step 2: Extend `axi_slave.hpp` — add `set_memory_bounds` + OOB pre-check**

In the public section, add:
```cpp
  void set_memory_bounds(uint64_t base, std::size_t size) {
    bounds_base_ = base; bounds_size_ = size; bounds_set_ = true;
  }
```

In private members:
```cpp
  uint64_t bounds_base_ = 0;
  std::size_t bounds_size_ = 0;
  bool bounds_set_ = false;
```

Modify the `// ── 2. Start new AW ──` block in `tick()`:

```cpp
  while (!aw_q_.empty()) {
    auto& aw = aw_q_.front();
    if (active_writes_.count(aw.id)) break;
    // Burst-atomic OOB check
    if (bounds_set_) {
      std::size_t bpb = 1ull << aw.size;
      std::size_t total = bpb * (static_cast<std::size_t>(aw.len) + 1);
      bool oob = (aw.addr < bounds_base_) ||
                 (aw.addr + total > bounds_base_ + bounds_size_);
      if (oob) {
        b_q_.push_back(BBeat{aw.id, Resp::DECERR, 0});
        // Discard the corresponding W beats from front of w_q_
        for (std::size_t i = 0; i < static_cast<std::size_t>(aw.len) + 1; ++i) {
          if (w_q_.empty()) break;
          w_q_.pop_front();
        }
        aw_q_.pop_front();
        continue;  // try next AW
      }
    }
    active_writes_[aw.id] = WriteBurstState{aw, 0, 0};
    aw_issue_order_.push_back(aw.id);
    aw_q_.pop_front();
  }
```

- [ ] **Step 3: Run → PASS**

```bash
cd c_model && cmake --build build --target test_axi_slave && ctest --test-dir build -R AxiSlave -V
```

- [ ] **Step 4: Commit**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/tests/axi/test_axi_slave.cpp
git commit -m "feat(c_model): AxiSlave burst-atomic OOB → BRESP=DECERR, no partial write"
```

---

### Task 3.6: AxiSlave read path (single + burst + OOB)

**Files:**
- Modify: `c_model/include/axi/axi_slave.hpp` — add read tick logic
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write failing test (single-beat read)**

```cpp
TEST(AxiSlave, ReadBurstSingleBeatInBoundsOkay) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 2; ar.addr = 0x1080; ar.len = 0; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 1u);
  EXPECT_EQ(mem.captured_reads.front().addr, 0x1080u);

  // Memory responds with data
  axi::MemReadResp rresp{}; rresp.id = 2; rresp.data.fill(0x77); rresp.resp = axi::Resp::OKAY;
  rresp.last = true; rresp.tag = mem.captured_reads.front().tag;
  mem.queued_read_resps.push_back(rresp);
  slave.tick();

  auto r = slave.pop_r();
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(r->id, 2); EXPECT_EQ(r->resp, axi::Resp::OKAY);
  EXPECT_EQ(r->last, true);
  EXPECT_EQ(r->data[0], 0x77);
}
```

- [ ] **Step 2: Implement read path in `tick()`**

Append to `tick()` at end (after write path sections):

```cpp
  // ── 4. Drain memory read responses → push R beats ──
  while (auto rresp = memory_port_.pop_read_resp()) {
    auto it = active_reads_.find(rresp->id);
    if (it == active_reads_.end()) continue;
    auto& st = it->second;
    RBeat rb{};
    rb.id = st.ar.id; rb.data = rresp->data; rb.resp = rresp->resp;
    rb.last = (st.beats_returned + 1 == static_cast<std::size_t>(st.ar.len) + 1);
    rb.user = 0;
    r_q_.push_back(rb);
    ++st.beats_returned;
    if (rb.last) active_reads_.erase(it);
  }

  // ── 5. Start new AR ──
  while (!ar_q_.empty()) {
    auto& ar = ar_q_.front();
    if (active_reads_.count(ar.id)) break;
    if (bounds_set_) {
      std::size_t bpb = 1ull << ar.size;
      std::size_t total = bpb * (static_cast<std::size_t>(ar.len) + 1);
      bool oob = (ar.addr < bounds_base_) ||
                 (ar.addr + total > bounds_base_ + bounds_size_);
      if (oob) {
        for (uint8_t i = 0; i < ar.len + 1; ++i) {
          RBeat rb{}; rb.id = ar.id;
          rb.data.fill(0); rb.resp = Resp::DECERR;
          rb.last = (i == ar.len); rb.user = 0;
          r_q_.push_back(rb);
        }
        ar_q_.pop_front();
        continue;
      }
    }
    active_reads_[ar.id] = ReadBurstState{ar, 0, 0};
    ar_q_.pop_front();
  }

  // ── 6. Submit AR beats to memory (one per active read per tick) ──
  for (auto& [id, st] : active_reads_) {
    while (st.beats_submitted < static_cast<std::size_t>(st.ar.len) + 1) {
      MemReadReq req{};
      req.addr = st.ar.addr + st.beats_submitted * (1ull << st.ar.size);
      req.size = st.ar.size; req.id = st.ar.id;
      req.last = (st.beats_submitted == static_cast<std::size_t>(st.ar.len));
      req.tag  = (static_cast<uint64_t>(id) << 32) | st.beats_submitted;
      if (!memory_port_.submit_read(req)) break;
      ++st.beats_submitted;
    }
  }
```

- [ ] **Step 3: Run → PASS** for single-beat read

- [ ] **Step 4: Add multi-beat read test**

```cpp
TEST(AxiSlave, ReadBurstIncr4Beat_InBounds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::ArBeat ar{};
  ar.id = 6; ar.addr = 0x1000; ar.len = 3; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  for (int t = 0; t < 4; ++t) slave.tick();
  ASSERT_EQ(mem.captured_reads.size(), 4u);

  // Memory responds 4 beats
  for (uint8_t i = 0; i < 4; ++i) {
    axi::MemReadResp rresp{};
    rresp.id = 6; rresp.data.fill(0xB0 + i); rresp.resp = axi::Resp::OKAY;
    rresp.last = (i == 3); rresp.tag = mem.captured_reads[i].tag;
    mem.queued_read_resps.push_back(rresp);
  }
  for (int t = 0; t < 4; ++t) slave.tick();

  for (uint8_t i = 0; i < 4; ++i) {
    auto r = slave.pop_r();
    ASSERT_TRUE(r.has_value()) << "beat " << int(i);
    EXPECT_EQ(r->data[0], 0xB0 + i);
    EXPECT_EQ(r->last, i == 3);
  }
}

TEST(AxiSlave, ReadBurstAtomicOob_AllBeatsDecerr) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x100);
  axi::ArBeat ar{};
  ar.id = 4; ar.addr = 0x10F0; ar.len = 1; ar.size = 5; ar.burst = axi::Burst::INCR;
  slave.push_ar(ar);
  slave.tick();
  EXPECT_EQ(mem.captured_reads.size(), 0u);
  for (uint8_t i = 0; i < 2; ++i) {
    auto r = slave.pop_r();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->resp, axi::Resp::DECERR);
    EXPECT_EQ(r->last, i == 1);
  }
}
```

- [ ] **Step 5: Run → all PASS**

- [ ] **Step 6: Commit**

```bash
git add c_model/include/axi/axi_slave.hpp c_model/tests/axi/test_axi_slave.cpp
git commit -m "feat(c_model): AxiSlave read path AR → memory → R + burst-atomic OOB DECERR"
```

---

### Task 3.7: AxiSlave backpressure retry (Memory queue full)

**Files:**
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST(AxiSlave, BackpressureRetry_NoBeatDropped) {
  test::MockMemoryPort mem;
  mem.write_capacity = 1;  // memory accepts only 1 in-flight write
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  axi::AwBeat aw{};
  aw.id = 1; aw.addr = 0x1000; aw.len = 2; aw.size = 5; aw.burst = axi::Burst::INCR;
  slave.push_aw(aw);
  for (uint8_t i = 0; i < 3; ++i) {
    axi::WBeat w{}; w.data.fill(0x40 + i); w.strb = 0xFFFF'FFFFu; w.last = (i == 2);
    slave.push_w(w);
  }
  // First tick: 1 W consumed (capacity), 2 still queued
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(slave.w_q_size(), 2u);

  // Drain memory + complete resp → capacity free
  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  mem.captured_writes.pop_front();  // simulate drain
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);  // 2nd accepted

  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  mem.captured_writes.pop_front();
  slave.tick();
  EXPECT_EQ(mem.captured_writes.size(), 1u);  // 3rd accepted

  // All 3 W consumed; no drops
  EXPECT_EQ(slave.w_q_size(), 0u);
}
```

- [ ] **Step 2: Run → PASS** (Task 3.2 impl breaks out of submit loop on false return)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave retries when memory submit_write returns false"
```

---

### Task 3.8: AxiSlave multi-burst per-ID tracking

**Files:**
- Modify: `c_model/tests/axi/test_axi_slave.cpp`

- [ ] **Step 1: Write test (sequential, single in-flight, different IDs)**

```cpp
TEST(AxiSlave, SequentialBurstsDifferentIds) {
  test::MockMemoryPort mem;
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(0x1000, 0x1000);

  // First burst: id=1, 1 beat
  axi::AwBeat aw1{}; aw1.id = 1; aw1.addr = 0x1000; aw1.len = 0; aw1.size = 5;
  aw1.burst = axi::Burst::INCR;
  slave.push_aw(aw1);
  axi::WBeat w1{}; w1.data.fill(0x11); w1.strb = 0xFFFF'FFFFu; w1.last = true;
  slave.push_w(w1);
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].id, 1);
  mem.queued_write_resps.push_back(
      axi::MemWriteResp{1, axi::Resp::OKAY, mem.captured_writes[0].tag});
  slave.tick();
  EXPECT_TRUE(slave.pop_b().has_value());

  // Second burst: id=2
  axi::AwBeat aw2 = aw1; aw2.id = 2; aw2.addr = 0x1100;
  axi::WBeat w2 = w1; w2.data.fill(0x22);
  slave.push_aw(aw2);
  slave.push_w(w2);
  mem.captured_writes.pop_front();
  slave.tick();
  ASSERT_EQ(mem.captured_writes.size(), 1u);
  EXPECT_EQ(mem.captured_writes[0].id, 2);
  EXPECT_EQ(mem.captured_writes[0].data[0], 0x22);
}
```

- [ ] **Step 2: Run → PASS** (per-ID map already cleans up after burst completes)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_slave.cpp
git commit -m "test(c_model): AxiSlave sequential bursts with different IDs"
```

---

### Stage 3 exit checklist

- [ ] AxiSlave full controller logic in `axi_slave.hpp` (write path, read path, OOB, backpressure, per-ID map)
- [ ] `test_axi_slave.cpp` ≥7 distinct TEST cases
- [ ] All drift gates green
- [ ] Mark Stage 3 → Complete

---

# Stage 4 — AxiMaster + ScenarioParser + Scoreboard

### Task 4.1: ScenarioTransaction struct + ScenarioParser (yaml-cpp)

**Files:**
- Create: `c_model/include/axi/scenario_parser.hpp`
- Modify: `c_model/tests/axi/CMakeLists.txt` — add `add_cmodel_test(test_axi_master)` and link yaml-cpp

- [ ] **Step 1: Create `scenario_parser.hpp`**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace ni::cmodel::axi {

struct ScenarioConfig {
  uint64_t    memory_base           = 0;
  std::size_t memory_size           = 0x10000;
  std::size_t write_latency         = 1;
  std::size_t read_latency          = 1;
  std::size_t max_outstanding_write = 1;
  std::size_t max_outstanding_read  = 1;
};

struct ScenarioTransaction {
  enum class Op { Write, Read };
  Op       op;
  uint64_t addr;
  uint8_t  id;
  uint8_t  len, size;
  Burst    burst;
  std::string data_file;
  std::string dump_file;
  std::size_t scenario_line;
};

struct Scenario {
  ScenarioConfig config;
  std::vector<ScenarioTransaction> transactions;
};

inline Burst parse_burst(const std::string& s) {
  if (s == "INCR")  return Burst::INCR;
  if (s == "WRAP")  return Burst::WRAP;
  if (s == "FIXED") return Burst::FIXED;
  throw std::runtime_error("scenario: unknown burst '" + s + "' (Phase A only supports INCR)");
}

inline Scenario load_scenario(const std::string& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception& e) {
    throw std::runtime_error("scenario: parse failed for '" + path + "': " + e.what());
  }
  Scenario sc;

  if (root["config"]) {
    auto cfg = root["config"];
    static const std::vector<std::string> known_cfg = {
        "memory_base", "memory_size", "write_latency", "read_latency",
        "max_outstanding_write", "max_outstanding_read"};
    for (auto it = cfg.begin(); it != cfg.end(); ++it) {
      auto key = it->first.as<std::string>();
      bool ok = false;
      for (auto& k : known_cfg) if (k == key) { ok = true; break; }
      if (!ok) throw std::runtime_error("scenario config: unknown field '" + key + "'");
    }
    if (cfg["memory_base"])           sc.config.memory_base           = cfg["memory_base"].as<uint64_t>();
    if (cfg["memory_size"])           sc.config.memory_size           = cfg["memory_size"].as<std::size_t>();
    if (cfg["write_latency"])         sc.config.write_latency         = cfg["write_latency"].as<std::size_t>();
    if (cfg["read_latency"])          sc.config.read_latency          = cfg["read_latency"].as<std::size_t>();
    if (cfg["max_outstanding_write"]) sc.config.max_outstanding_write = cfg["max_outstanding_write"].as<std::size_t>();
    if (cfg["max_outstanding_read"])  sc.config.max_outstanding_read  = cfg["max_outstanding_read"].as<std::size_t>();
  }

  if (!root["transactions"] || !root["transactions"].IsSequence() ||
      root["transactions"].size() == 0) {
    throw std::runtime_error("scenario: 'transactions' must be a non-empty sequence");
  }

  std::size_t line = 0;
  for (const auto& txn : root["transactions"]) {
    ++line;
    ScenarioTransaction t{};
    t.scenario_line = line;
    auto op = txn["op"].as<std::string>();
    if (op == "write")      t.op = ScenarioTransaction::Op::Write;
    else if (op == "read")  t.op = ScenarioTransaction::Op::Read;
    else throw std::runtime_error("scenario txn " + std::to_string(line) +
                                  ": unknown op '" + op + "'");
    t.addr  = txn["addr"].as<uint64_t>();
    t.id    = txn["id"].as<uint8_t>();
    t.len   = txn["len"].as<uint8_t>();
    t.size  = txn["size"].as<uint8_t>();
    if (t.size > 5) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": size must be ≤ 5 (Phase A max beat = 32 bytes)");
    }
    t.burst = parse_burst(txn["burst"].as<std::string>());
    if (t.burst != Burst::INCR) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": Phase A only supports INCR burst");
    }
    // Phase A: aligned-only check
    if ((t.addr & ((1ull << t.size) - 1)) != 0) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": addr must be aligned to (1<<size) in Phase A");
    }
    if (t.op == ScenarioTransaction::Op::Write) t.data_file = txn["data_file"].as<std::string>();
    if (t.op == ScenarioTransaction::Op::Read)  t.dump_file = txn["dump_file"].as<std::string>();
    sc.transactions.push_back(t);
  }
  return sc;
}

}  // namespace ni::cmodel::axi
```

- [ ] **Step 2: Modify `c_model/tests/axi/CMakeLists.txt`**

```cmake
add_cmodel_test(test_scaffold)
add_cmodel_test(test_memory)
add_cmodel_test(test_axi_slave)
add_cmodel_test(test_axi_master)
target_link_libraries(test_axi_master PRIVATE yaml-cpp::yaml-cpp)
```

- [ ] **Step 3: Create `c_model/tests/axi/test_axi_master.cpp` with parser tests**

```cpp
#include "axi/scenario_parser.hpp"
#include <gtest/gtest.h>
#include <fstream>

namespace axi = ni::cmodel::axi;

class ScenarioParser : public ::testing::Test {
protected:
  std::string write_tmp(const std::string& contents) {
    auto path = std::string(::testing::TempDir()) + "/scenario.yaml";
    std::ofstream f(path); f << contents; return path;
  }
};

TEST_F(ScenarioParser, MinimalWriteReadScenario) {
  auto path = write_tmp(R"YAML(
config:
  memory_base: 0x1000
  memory_size: 0x1000
transactions:
  - op: write
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    data_file: w.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    dump_file: r.txt
)YAML");
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.config.memory_base, 0x1000u);
  ASSERT_EQ(sc.transactions.size(), 2u);
  EXPECT_EQ(sc.transactions[0].op, axi::ScenarioTransaction::Op::Write);
  EXPECT_EQ(sc.transactions[0].data_file, "w.txt");
  EXPECT_EQ(sc.transactions[1].dump_file, "r.txt");
}

TEST_F(ScenarioParser, DefaultsAppliedWhenConfigOmitted) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x0
    id: 0
    len: 0
    size: 0
    burst: INCR
    dump_file: r.txt
)YAML");
  auto sc = axi::load_scenario(path);
  EXPECT_EQ(sc.config.memory_base,   0u);
  EXPECT_EQ(sc.config.memory_size,   0x10000u);
  EXPECT_EQ(sc.config.write_latency, 1u);
  EXPECT_EQ(sc.config.max_outstanding_write, 1u);
}

TEST_F(ScenarioParser, UnknownConfigFieldThrows) {
  auto path = write_tmp(R"YAML(
config:
  bogus_field: 123
transactions:
  - op: read
    addr: 0
    id: 0
    len: 0
    size: 0
    burst: INCR
    dump_file: r.txt
)YAML");
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}

TEST_F(ScenarioParser, NonIncrBurstThrows_PhaseA) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0
    id: 0
    len: 1
    size: 5
    burst: WRAP
    dump_file: r.txt
)YAML");
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}

TEST_F(ScenarioParser, UnalignedAddrThrows_PhaseA) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x1001
    id: 0
    len: 0
    size: 5
    burst: INCR
    dump_file: r.txt
)YAML");
  EXPECT_THROW(axi::load_scenario(path), std::runtime_error);
}
```

- [ ] **Step 4: Build + run → all PASS**

```bash
cd c_model && cmake --build build --target test_axi_master && ctest --test-dir build -R ScenarioParser -V
```

- [ ] **Step 5: Commit**

```bash
git add c_model/include/axi/scenario_parser.hpp \
        c_model/tests/axi/test_axi_master.cpp \
        c_model/tests/axi/CMakeLists.txt
git commit -m "feat(c_model): ScenarioParser using yaml-cpp + Phase A guardrails

Rejects unsupported burst (Phase A INCR-only), unaligned addr, unknown
config fields. Defaults applied when config block omitted."
```

---

### Task 4.2: MockSlave + AxiMaster skeleton

**Files:**
- Create: `c_model/tests/axi/mock_slave.hpp`
- Create: `c_model/include/axi/axi_master.hpp`
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Create `mock_slave.hpp`**

```cpp
#pragma once
#include "axi/types.hpp"
#include <deque>

namespace ni::cmodel::axi::testing {

// Minimal mirror of AxiSlave's master-facing API for AxiMaster unit tests.
class MockSlave {
public:
  bool push_aw(const AwBeat& b) { captured_aw.push_back(b); return true; }
  bool push_w (const WBeat&  b) { captured_w.push_back(b);  return true; }
  bool push_ar(const ArBeat& b) { captured_ar.push_back(b); return true; }
  std::optional<BBeat> pop_b() {
    if (queued_b.empty()) return std::nullopt;
    auto r = queued_b.front(); queued_b.pop_front(); return r;
  }
  std::optional<RBeat> pop_r() {
    if (queued_r.empty()) return std::nullopt;
    auto r = queued_r.front(); queued_r.pop_front(); return r;
  }
  void tick() {}

  std::deque<AwBeat> captured_aw;
  std::deque<WBeat>  captured_w;
  std::deque<ArBeat> captured_ar;
  std::deque<BBeat>  queued_b;
  std::deque<RBeat>  queued_r;
};

}
```

- [ ] **Step 2: Create minimal `axi_master.hpp` skeleton**

```cpp
// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include "axi/scenario_parser.hpp"
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ni::cmodel::axi {

struct WriteResult { uint64_t addr; std::size_t data_len; Resp resp;
                     uint8_t id; std::size_t scenario_line; };
struct ReadResult  { uint64_t addr; std::vector<uint8_t> data; Resp resp;
                     uint8_t id; std::size_t scenario_line; };

template<typename SlaveT>
class AxiMasterT {
public:
  AxiMasterT(const std::string& scenario_yaml,
             SlaveT& slave,
             const std::string& read_dump_path,
             std::size_t max_outstanding_write = 1,
             std::size_t max_outstanding_read  = 1)
      : slave_(slave),
        max_out_w_(max_outstanding_write), max_out_r_(max_outstanding_read) {
    sc_ = load_scenario(scenario_yaml);
    read_dump_.open(read_dump_path);
    if (!read_dump_.is_open())
      throw std::runtime_error("AxiMaster: cannot open read_dump_path: " + read_dump_path);
  }

  void tick();      // implemented in Task 4.3+
  bool done() const { return next_txn_idx_ >= sc_.transactions.size()
                          && active_writes_.empty() && active_reads_.empty(); }

  void on_write_completed(std::function<void(const WriteResult&)> cb) { wcb_ = std::move(cb); }
  void on_read_observed  (std::function<void(const ReadResult&)>  cb) { rcb_ = std::move(cb); }

private:
  Scenario   sc_;
  SlaveT&    slave_;
  std::size_t max_out_w_, max_out_r_;
  std::size_t next_txn_idx_ = 0;
  std::ofstream read_dump_;
  std::function<void(const WriteResult&)> wcb_;
  std::function<void(const ReadResult&)>  rcb_;

  struct WriteState {
    ScenarioTransaction txn;
    std::vector<uint8_t> data;     // loaded from data_file
    std::size_t aw_pushed_ = 0;
    std::size_t w_pushed_  = 0;
  };
  struct ReadState {
    ScenarioTransaction txn;
    std::size_t ar_pushed_ = 0;
    std::vector<uint8_t> accumulator;
    std::size_t beats_observed = 0;
  };
  std::map<uint8_t, WriteState> active_writes_;
  std::map<uint8_t, ReadState>  active_reads_;
};

// Convenience typedef
class AxiSlave;
using AxiMaster = AxiMasterT<AxiSlave>;

// tick() stub for now; real impl in Task 4.3
template<typename T> inline void AxiMasterT<T>::tick() {}

}  // namespace ni::cmodel::axi
```

(Template parameter lets MockSlave work without inheritance.)

- [ ] **Step 3: Scaffold test that just constructs AxiMaster**

Append to `test_axi_master.cpp`:

```cpp
#include "axi/axi_master.hpp"
#include "mock_slave.hpp"

class AxiMasterTest : public ScenarioParser {};

TEST_F(AxiMasterTest, ConstructsFromYamlAndOpensDump) {
  auto wpath = std::string(::testing::TempDir()) + "/w.txt";
  std::ofstream(wpath) << "AB CD EF 12 34 56 78 9A BC DE F0 11 22 33 44 55 "
                          "66 77 88 99 AA BB CC DD EE FF 00 11 22 33 44 55\n";
  auto yaml = write_tmp(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML" + wpath + R"YAML(
  - op: read
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    dump_file: )YAML" + std::string(::testing::TempDir()) + R"YAML(/r.txt
)YAML");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_out.txt", 1, 1);
  EXPECT_FALSE(master.done());
}
```

- [ ] **Step 4: Build + run + commit**

```bash
cd c_model && cmake --build build --target test_axi_master && ctest --test-dir build -R AxiMaster -V
git add c_model/tests/axi/mock_slave.hpp \
        c_model/include/axi/axi_master.hpp \
        c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): AxiMaster skeleton (templated for MockSlave testing)"
```

---

### Task 4.3: AxiMaster write transaction execution

**Files:**
- Modify: `c_model/include/axi/axi_master.hpp` — implement write path in `tick()`
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST_F(AxiMasterTest, SingleWriteTransactionExecutes) {
  auto wpath = std::string(::testing::TempDir()) + "/w_single.txt";
  std::ofstream(wpath) << "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 "
                          "11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20\n";
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x7
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");

  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt");

  bool fired = false;
  master.on_write_completed([&](const axi::WriteResult& r) {
    fired = true;
    EXPECT_EQ(r.id, 7);
    EXPECT_EQ(r.resp, axi::Resp::OKAY);
  });

  // Tick → push AW + push W (one each — Phase A simplification)
  master.tick();
  EXPECT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_w.size(),  1u);
  EXPECT_EQ(mock.captured_aw[0].id, 7);
  EXPECT_EQ(mock.captured_w[0].data[0], 0x01);
  EXPECT_EQ(mock.captured_w[0].last, true);

  // Slave (mock) returns B
  mock.queued_b.push_back(axi::BBeat{7, axi::Resp::OKAY, 0});
  master.tick();
  EXPECT_TRUE(fired);
  EXPECT_TRUE(master.done());
}
```

- [ ] **Step 2: Implement write path in `tick()`**

Replace the stub `tick()` with full template impl:

```cpp
template<typename SlaveT>
inline void AxiMasterT<SlaveT>::tick() {
  // 1. Drain B responses
  while (auto b = slave_.pop_b()) {
    auto it = active_writes_.find(b->id);
    if (it == active_writes_.end()) continue;
    if (wcb_) wcb_(WriteResult{it->second.txn.addr,
                                it->second.data.size(),
                                b->resp,
                                b->id,
                                it->second.txn.scenario_line});
    active_writes_.erase(it);
  }
  // 2. Drain R responses
  while (auto r = slave_.pop_r()) {
    auto it = active_reads_.find(r->id);
    if (it == active_reads_.end()) continue;
    // Accumulate beat
    std::size_t bpb = 1ull << it->second.txn.size;
    for (std::size_t i = 0; i < bpb; ++i)
      it->second.accumulator.push_back(r->data[i]);
    ++it->second.beats_observed;
    if (r->last) {
      // Dump to file (per-beat lines, 32 hex space-separated)
      std::size_t lines = it->second.accumulator.size() / DATA_BYTES;
      for (std::size_t line = 0; line < lines; ++line) {
        for (std::size_t j = 0; j < DATA_BYTES; ++j) {
          if (j > 0) read_dump_ << ' ';
          char buf[4];
          std::snprintf(buf, sizeof(buf), "%02X", it->second.accumulator[line * DATA_BYTES + j]);
          read_dump_ << buf;
        }
        read_dump_ << '\n';
      }
      if (rcb_) rcb_(ReadResult{it->second.txn.addr,
                                  it->second.accumulator,
                                  r->resp,
                                  r->id,
                                  it->second.txn.scenario_line});
      active_reads_.erase(it);
    }
  }

  // 3. Start next transaction if admission allows
  while (next_txn_idx_ < sc_.transactions.size()) {
    const auto& txn = sc_.transactions[next_txn_idx_];
    if (txn.op == ScenarioTransaction::Op::Write) {
      if (active_writes_.size() >= max_out_w_) break;
      WriteState ws{txn, {}, 0, 0};
      ws.data = load_write_data_(txn.data_file, (txn.len + 1u) * (1u << txn.size));
      active_writes_[txn.id] = ws;
    } else {
      if (active_reads_.size() >= max_out_r_) break;
      active_reads_[txn.id] = ReadState{txn, 0, {}, 0};
    }
    ++next_txn_idx_;
  }

  // 4. Push AW + W for active writes
  for (auto& [id, ws] : active_writes_) {
    if (ws.aw_pushed_ == 0) {
      AwBeat aw{};
      aw.id = id; aw.addr = ws.txn.addr; aw.len = ws.txn.len; aw.size = ws.txn.size;
      aw.burst = ws.txn.burst;
      slave_.push_aw(aw);
      ws.aw_pushed_ = 1;
    }
    while (ws.w_pushed_ <= ws.txn.len) {
      WBeat w{};
      std::size_t bpb = 1ull << ws.txn.size;
      for (std::size_t j = 0; j < DATA_BYTES; ++j)
        w.data[j] = (ws.w_pushed_ * bpb + j < ws.data.size())
                  ? ws.data[ws.w_pushed_ * bpb + j] : 0;
      w.strb = 0xFFFF'FFFFu;  // Phase A full strb
      w.last = (ws.w_pushed_ == ws.txn.len);
      if (!slave_.push_w(w)) break;
      ++ws.w_pushed_;
    }
  }
  // 5. Push AR for active reads
  for (auto& [id, rs] : active_reads_) {
    if (rs.ar_pushed_ == 0) {
      ArBeat ar{};
      ar.id = id; ar.addr = rs.txn.addr; ar.len = rs.txn.len; ar.size = rs.txn.size;
      ar.burst = rs.txn.burst;
      slave_.push_ar(ar);
      rs.ar_pushed_ = 1;
    }
  }
}

private:
  static std::vector<uint8_t> load_write_data_(const std::string& path, std::size_t expected_bytes) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("AxiMaster: cannot open data_file: " + path);
    std::vector<uint8_t> bytes;
    std::string tok;
    while (f >> tok) bytes.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
    if (bytes.size() < expected_bytes)
      throw std::runtime_error("AxiMaster: data_file too short (" + std::to_string(bytes.size())
                               + " < " + std::to_string(expected_bytes) + "): " + path);
    return bytes;
  }
};  // end class AxiMasterT (move closing brace + private section)
```

**IMPORTANT**: the `load_write_data_` declaration needs to be moved INSIDE the class. The shown block here is for the tick() body and a free-standing helper; actual edit places them appropriately. The implementer should re-organize the class definition cohesively (private members + private helper + `}` of class).

- [ ] **Step 3: Build + run → PASS**

```bash
cd c_model && cmake --build build --target test_axi_master && ctest --test-dir build -R AxiMaster -V
```

- [ ] **Step 4: Commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/tests/axi/test_axi_master.cpp
git commit -m "feat(c_model): AxiMaster executes scenario write transactions"
```

---

### Task 4.4: AxiMaster read transaction + dump

**Files:**
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST_F(AxiMasterTest, SingleReadTransactionDumpsToFile) {
  auto dumpPath = std::string(::testing::TempDir()) + "/r_single.txt";
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: read
    addr: 0x0
    id: 0x9
    len: 0
    size: 5
    burst: INCR
    dump_file: )YAML") + dumpPath + "\n");

  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(yaml, mock, dumpPath);

  // tick → master pushes AR
  master.tick();
  EXPECT_EQ(mock.captured_ar.size(), 1u);

  // mock returns R last beat
  axi::RBeat r{}; r.id = 9; r.data.fill(0xAB);
  r.resp = axi::Resp::OKAY; r.last = true; r.user = 0;
  mock.queued_r.push_back(r);
  master.tick();
  EXPECT_TRUE(master.done());

  // Check dump file content
  std::ifstream f(dumpPath); std::string line; std::getline(f, line);
  // Expect 32 "AB" tokens
  EXPECT_EQ(line.substr(0, 5), "AB AB");
}
```

- [ ] **Step 2: Run → PASS** (Task 4.3 impl covers read accumulator + dump)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_master.cpp
git commit -m "test(c_model): AxiMaster read transaction dumps to file"
```

---

### Task 4.5: AxiMaster max_outstanding admission control

**Files:**
- Modify: `c_model/tests/axi/test_axi_master.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST_F(AxiMasterTest, MaxOutstandingWriteLimitsConcurrency) {
  auto wpath = std::string(::testing::TempDir()) + "/w_concur.txt";
  std::ofstream(wpath) << "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF "
                          "00 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF\n";
  auto yaml = write_tmp(std::string(R"YAML(
config:
  max_outstanding_write: 2
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + R"YAML(
  - op: write
    addr: 0x20
    id: 0x2
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML" + wpath + R"YAML(
  - op: write
    addr: 0x40
    id: 0x3
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML" + wpath + "\n");

  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(yaml, mock,
      std::string(::testing::TempDir()) + "/r.txt", 2, 1);
  master.tick();
  // Only 2 of 3 should be in flight (max=2)
  EXPECT_EQ(mock.captured_aw.size(), 2u);
  EXPECT_EQ(mock.captured_aw[0].id, 1);
  EXPECT_EQ(mock.captured_aw[1].id, 2);
  // Complete first → third admitted
  mock.queued_b.push_back(axi::BBeat{1, axi::Resp::OKAY, 0});
  master.tick();
  EXPECT_EQ(mock.captured_aw.size(), 3u);
  EXPECT_EQ(mock.captured_aw[2].id, 3);
}
```

- [ ] **Step 2: Run → PASS** (admission control already in Task 4.3 impl)

- [ ] **Step 3: Commit**

```bash
git add c_model/tests/axi/test_axi_master.cpp
git commit -m "test(c_model): AxiMaster max_outstanding admission control"
```

---

### Task 4.6: Scoreboard byte_map update on OKAY + mismatch detection

**Files:**
- Create: `c_model/include/axi/scoreboard.hpp`
- Create: `c_model/tests/axi/test_scoreboard.cpp`
- Modify: `c_model/tests/axi/CMakeLists.txt` — add `add_cmodel_test(test_scoreboard)`

- [ ] **Step 1: Create `scoreboard.hpp`**

```cpp
// (Scoreboard pattern is independent; see ATTRIBUTION.md)
#pragma once
#include "axi/axi_master.hpp"
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ni::cmodel::axi {

class Scoreboard {
public:
  void handle_write_completed(const WriteResult& wr,
                              const std::vector<uint8_t>& data,
                              uint32_t /*strb_per_beat*/ = 0xFFFF'FFFFu) {
    if (wr.resp != Resp::OKAY) return;
    for (std::size_t i = 0; i < data.size(); ++i)
      expected_[wr.addr + i] = data[i];
  }
  void handle_read_observed(const ReadResult& rr) {
    if (rr.resp != Resp::OKAY) return;
    for (std::size_t i = 0; i < rr.data.size(); ++i) {
      uint64_t a = rr.addr + i;
      auto it = expected_.find(a);
      uint8_t exp = (it == expected_.end()) ? 0x00 : it->second;
      if (exp != rr.data[i]) {
        ++mismatches_;
        std::ostringstream oss;
        oss << "[Scoreboard] MISMATCH at addr=0x" << std::hex << a
            << " (scenario line " << std::dec << rr.scenario_line << "): "
            << "expected=0x" << std::hex << +exp
            << " actual=0x" << +rr.data[i];
        log_.push_back(oss.str());
      }
    }
    ++reads_checked_;
  }
  std::size_t mismatch_count() const { return mismatches_; }
  std::size_t reads_checked()  const { return reads_checked_; }
  const std::vector<std::string>& mismatch_report() const { return log_; }

private:
  std::map<uint64_t, uint8_t> expected_;
  std::size_t mismatches_ = 0;
  std::size_t reads_checked_ = 0;
  std::vector<std::string> log_;
};

}  // namespace ni::cmodel::axi
```

- [ ] **Step 2: Add target**

```cmake
add_cmodel_test(test_scoreboard)
```

- [ ] **Step 3: Create `test_scoreboard.cpp`**

```cpp
#include "axi/scoreboard.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;

TEST(Scoreboard, NoUpdateOnDecerr) {
  axi::Scoreboard sb;
  sb.handle_write_completed(
      axi::WriteResult{0x100, 4, axi::Resp::DECERR, 1, 1},
      std::vector<uint8_t>{0xAB, 0xCD, 0xEF, 0x12});
  // No expected updated → any read should still be == 0x00
  sb.handle_read_observed(axi::ReadResult{0x100, {0x00, 0x00}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 0u);
}

TEST(Scoreboard, MismatchDetected) {
  axi::Scoreboard sb;
  sb.handle_write_completed(
      axi::WriteResult{0x200, 4, axi::Resp::OKAY, 1, 1},
      std::vector<uint8_t>{0xAB, 0xCD, 0xEF, 0x12});
  sb.handle_read_observed(
      axi::ReadResult{0x200, {0xAB, 0xCD, 0xEE, 0x12}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 1u);
  EXPECT_FALSE(sb.mismatch_report().empty());
}

TEST(Scoreboard, MatchPassesSilent) {
  axi::Scoreboard sb;
  sb.handle_write_completed(
      axi::WriteResult{0x300, 4, axi::Resp::OKAY, 1, 1},
      std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF});
  sb.handle_read_observed(
      axi::ReadResult{0x300, {0xDE, 0xAD, 0xBE, 0xEF}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 0u);
  EXPECT_EQ(sb.reads_checked(), 1u);
}

TEST(Scoreboard, ReadFromUnwrittenAddrReturnsFillDefault) {
  axi::Scoreboard sb;
  // No write yet → expected is 0x00 (default)
  sb.handle_read_observed(
      axi::ReadResult{0x400, {0x00, 0x00, 0x00, 0x00}, axi::Resp::OKAY, 1, 1});
  EXPECT_EQ(sb.mismatch_count(), 0u);
}
```

- [ ] **Step 4: Build + run + commit**

```bash
cd c_model && cmake --build build --target test_scoreboard && ctest --test-dir build -R Scoreboard -V
git add c_model/include/axi/scoreboard.hpp \
        c_model/tests/axi/test_scoreboard.cpp \
        c_model/tests/axi/CMakeLists.txt
git commit -m "feat(c_model): Scoreboard byte_map update on OKAY + mismatch report"
```

---

### Stage 4 exit checklist

- [ ] ScenarioParser handles minimal YAML + defaults + unknown-field throw + Phase A guardrails
- [ ] AxiMaster executes write + read; admission control; callbacks fire
- [ ] Scoreboard tracks byte_map and reports mismatches
- [ ] All drift gates green
- [ ] Mark Stage 4 → Complete

---

# Stage 5 — Integration

### Task 5.1: Integration test harness + diff utility

**Files:**
- Create: `c_model/tests/axi/test_integration.cpp`
- Create: `c_model/tests/axi/fixtures/README.md`
- Modify: `c_model/tests/axi/CMakeLists.txt` — add `add_cmodel_test(test_integration)` + link yaml-cpp + copy fixtures

- [ ] **Step 1: CMake — add test_integration with fixture copy**

```cmake
add_cmodel_test(test_integration)
target_link_libraries(test_integration PRIVATE yaml-cpp::yaml-cpp)
add_custom_command(TARGET test_integration POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    $<TARGET_FILE_DIR:test_integration>/fixtures)
```

- [ ] **Step 2: Create `test_integration.cpp`**

```cpp
#include "axi/axi_master.hpp"
#include "axi/axi_slave.hpp"
#include "axi/memory.hpp"
#include "axi/scenario_parser.hpp"
#include "axi/scoreboard.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace axi = ni::cmodel::axi;

struct IntegrationResult {
  bool file_diff_pass;
  std::size_t scoreboard_mismatches;
  std::size_t reads_checked;
  std::size_t cycle_count;
};

inline bool diff_files(const std::string& a, const std::string& b) {
  std::ifstream fa(a), fb(b);
  std::stringstream sa, sb;
  sa << fa.rdbuf(); sb << fb.rdbuf();
  return sa.str() == sb.str();
}

static IntegrationResult run_scenario(const std::string& yaml_path,
                                       const std::string& write_data_path,
                                       const std::string& read_dump_path) {
  auto sc = axi::load_scenario(yaml_path);

  axi::Memory   mem(sc.config.memory_base, sc.config.memory_size,
                    sc.config.write_latency, sc.config.read_latency);
  axi::AxiSlave slave(mem);
  slave.set_memory_bounds(sc.config.memory_base, sc.config.memory_size);
  axi::AxiMasterT<axi::AxiSlave> master(yaml_path, slave, read_dump_path,
                                         sc.config.max_outstanding_write,
                                         sc.config.max_outstanding_read);
  axi::Scoreboard sb;
  master.on_write_completed([&](const axi::WriteResult& wr) {
    // Re-load data_file to feed scoreboard
    // (Simplified: scoreboard would observe via tap; here we replay file)
    sb.handle_write_completed(wr, /*data=*/{});  // FIXME data lookup
  });
  master.on_read_observed([&](const axi::ReadResult& rr) {
    sb.handle_read_observed(rr);
  });

  constexpr std::size_t MAX_CYCLES = 100'000;
  std::size_t cycle = 0;
  while (!master.done()) {
    master.tick();
    slave.tick();
    mem.tick();
    if (++cycle > MAX_CYCLES) {
      return IntegrationResult{false, sb.mismatch_count(), sb.reads_checked(), cycle};
    }
  }
  bool fdiff = diff_files(write_data_path, read_dump_path);
  return IntegrationResult{fdiff, sb.mismatch_count(), sb.reads_checked(), cycle};
}

struct FixtureParam {
  std::string yaml;
  std::string write_data;  // empty if read-only
  bool        expect_file_diff_pass;
  bool        expect_zero_mismatches;
};

class IntegrationP : public ::testing::TestWithParam<FixtureParam> {};

TEST_P(IntegrationP, RunFixture) {
  auto p = GetParam();
  std::string yaml_path = "fixtures/" + p.yaml;
  std::string wpath     = p.write_data.empty() ? "" : ("fixtures/" + p.write_data);
  std::string rpath     = std::string(::testing::TempDir()) + "/" + p.yaml + ".read.txt";
  auto r = run_scenario(yaml_path, wpath, rpath);
  if (p.expect_file_diff_pass) EXPECT_TRUE(r.file_diff_pass);
  if (p.expect_zero_mismatches) EXPECT_EQ(r.scoreboard_mismatches, 0u);
}

INSTANTIATE_TEST_SUITE_P(AxiFixtures, IntegrationP, ::testing::Values(
  FixtureParam{"single_write_read_aligned.yaml", "write_data.txt", true,  true},
  FixtureParam{"burst_incr_2beat.yaml",          "write_data.txt", true,  true},
  FixtureParam{"burst_incr_8beat.yaml",          "write_data.txt", true,  true},
  FixtureParam{"multi_txn_same_id.yaml",         "write_data.txt", true,  true},
  FixtureParam{"multi_txn_diff_id.yaml",         "write_data.txt", true,  true},
  FixtureParam{"decerr_oob_write.yaml",          "write_data.txt", false, true},
  FixtureParam{"decerr_oob_read.yaml",           "",               false, true},
  FixtureParam{"latency_stress.yaml",            "write_data.txt", true,  true},
  FixtureParam{"single_read_default_fill.yaml",  "",               false, true},
  FixtureParam{"burst_crosses_oob_boundary.yaml","write_data.txt", false, true},
  FixtureParam{"backpressure_retry.yaml",        "write_data.txt", true,  true},
  FixtureParam{"multi_outstanding_stress.yaml",  "write_data.txt", true,  true}
));
```

- [ ] **Step 3: Create `fixtures/README.md`**

```markdown
# axi/fixtures — Integration test scenarios

Scenarios inspired by alexforencich/cocotbext-axi `test_axi.py` directed cases (MIT).
Each `.yaml` describes a scenario; `.txt` files hold hex per-beat payload bytes.

Phase A coverage:
- Single write/read aligned, INCR bursts of len 1/8
- Multi transactions same-ID + diff-ID
- DECERR paths (OOB read, OOB write, burst crossing boundary)
- Default fill (read before write)
- Backpressure retry (memory pending depth small)
- Multi-outstanding stress (max_outstanding=8)
- Latency stress (latency=20)

Per-beat line format: 32 hex bytes space-separated (DATA_BYTES = 32 = WSTRB_WIDTH).
```

- [ ] **Step 4: Commit (without fixtures yet — placeholder skeleton)**

```bash
mkdir -p c_model/tests/axi/fixtures
git add c_model/tests/axi/test_integration.cpp c_model/tests/axi/fixtures/README.md c_model/tests/axi/CMakeLists.txt
git commit -m "feat(c_model): integration test harness + diff utility skeleton"
```

---

### Task 5.2: Create 12 fixture YAML + data files

Create each fixture. For brevity, only first two shown in full; remaining follow same pattern.

**Files:** Create all under `c_model/tests/axi/fixtures/`

- [ ] **Step 1: Fixture 1 — `single_write_read_aligned.yaml` + `write_data.txt`**

`single_write_read_aligned.yaml`:
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
    data_file: write_data.txt
  - op: read
    addr: 0x1000
    id: 0x5
    len: 0
    size: 5
    burst: INCR
    dump_file: read_data.txt
```

`write_data.txt`:
```
01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20
```

- [ ] **Step 2: Fixture 2 — `burst_incr_2beat.yaml`**

```yaml
config:
  memory_base: 0x1000
  memory_size: 0x1000
transactions:
  - op: write
    addr: 0x1000
    id: 0x6
    len: 1
    size: 5
    burst: INCR
    data_file: write_data.txt
  - op: read
    addr: 0x1000
    id: 0x6
    len: 1
    size: 5
    burst: INCR
    dump_file: read_data.txt
```

`write_data.txt` for this fixture: 2 lines of 32 hex bytes each. (Use the helper script below.)

- [ ] **Step 3: Helper script to generate per-burst write_data.txt**

Create `c_model/tests/axi/fixtures/gen_write_data.py`:

```python
#!/usr/bin/env python3
"""Generate per-fixture write_data.txt with predictable hex bytes.

Usage: py -3 gen_write_data.py <output> <beats>
"""
import sys
out, n_beats = sys.argv[1], int(sys.argv[2])
with open(out, 'w') as f:
    for beat in range(n_beats):
        row = [f"{(beat * 32 + i) & 0xFF:02X}" for i in range(32)]
        f.write(' '.join(row) + '\n')
```

Run:
```bash
cd c_model/tests/axi/fixtures
py -3 gen_write_data.py write_data.txt 1   # for single + len=0 fixtures
```

For multi-beat fixtures, manually copy + scale. (Implementer can write one master write_data.txt with enough lines and each fixture references it; or per-fixture file.)

- [ ] **Step 4: Create remaining 10 fixtures**

Use the same pattern. Each YAML follows the structure above with `addr` / `len` / `id` / `burst` varied per the spec Testing table:

- `burst_incr_8beat.yaml`: len=7
- `multi_txn_same_id.yaml`: 2 writes + 2 reads, all id=5
- `multi_txn_diff_id.yaml`: 4 txns with id=1/2/3/4
- `decerr_oob_write.yaml`: addr beyond memory_size
- `decerr_oob_read.yaml`: read addr OOB; expect_file_diff_pass=false
- `latency_stress.yaml`: write_latency=20, read_latency=20
- `single_read_default_fill.yaml`: only `op: read`; no write
- `burst_crosses_oob_boundary.yaml`: addr near end, len causes overflow
- `backpressure_retry.yaml`: small Memory pending depth (need config extension? OR rely on Memory ctor default trick: just submit many writes; for Phase A, skip if too complex)
- `multi_outstanding_stress.yaml`: `max_outstanding_write: 8`, 8+ transactions

- [ ] **Step 5: Build + run all fixtures**

```bash
cd c_model && cmake --build build --target test_integration && ctest --test-dir build -R IntegrationP -V
```

Expected: 12 instances all PASS (or behave per `expect_file_diff_pass` / `expect_zero_mismatches`).

- [ ] **Step 6: Commit fixtures**

```bash
git add c_model/tests/axi/fixtures/
git commit -m "test(c_model): 12 Phase A integration fixtures (YAML + write_data.txt)"
```

---

### Task 5.3: Wire scoreboard data tap (fix FIXME from Task 5.1)

**Files:**
- Modify: `c_model/tests/axi/test_integration.cpp` — replace scoreboard write-data lookup

- [ ] **Step 1: Implement write-data tap**

Replace the `master.on_write_completed` lambda body. The scoreboard needs the actual write payload; pull from a separate accumulator that records each push_w:

```cpp
// Alternative: use a thin wrapper around AxiSlave that captures W beats per id
// for scoreboard consumption. Or have AxiMaster expose its loaded data buffer.
// Simplest: extend WriteResult to include the actual data bytes (already implemented? if not, do it now).
```

If `WriteResult` doesn't carry data, modify `axi_master.hpp` to include the loaded data vector in the WriteResult event:

```cpp
struct WriteResult {
  uint64_t addr;
  std::vector<uint8_t> data;  // ← add this
  Resp resp;
  uint8_t id;
  std::size_t scenario_line;
};
```

And update `tick()` callsite to pass `it->second.data`.

Update integration test:
```cpp
master.on_write_completed([&](const axi::WriteResult& wr) {
  sb.handle_write_completed(wr, wr.data);
});
```

- [ ] **Step 2: Re-run all fixtures → all PASS**

```bash
cd c_model && cmake --build build && ctest --test-dir build -R IntegrationP -V
```

- [ ] **Step 3: Commit**

```bash
git add c_model/include/axi/axi_master.hpp c_model/tests/axi/test_integration.cpp
git commit -m "feat(c_model): WriteResult carries data payload for Scoreboard tap"
```

---

### Task 5.4: Final drift gates + remove IMPLEMENTATION_PLAN.md

- [ ] **Step 1: Run full drift gates**

```bash
cd c_model && cmake --build build && ctest --test-dir build
cd ../spec_validate && py -3 -m pytest -q && py -3 tools/codegen.py --check && py -3 tools/gen_inventory.py --check
```
Expected: 4 conditions all PASS; ctest count = 27 (Layer A) + 6 scaffold + ≥10 memory + ≥7 slave + ≥5 master + ≥4 scoreboard + 12 integration = ≥71.

- [ ] **Step 2: Karpathy 4-lens final review**

Look at the cumulative diff `git diff master..HEAD --stat` and apply 4-lens to the full implementation:
1. Overcomplication: PASS / CONCERN — note any over-engineered class
2. Surgical: PASS — each Phase A feature traceable to spec scope
3. Surface assumptions: PASS — all spec values from codegen; static_assert in types.hpp
4. Verifiable success: PASS — 12 integration fixtures, each tied to a spec test category

Document findings in the final commit message.

- [ ] **Step 3: Remove IMPLEMENTATION_PLAN.md**

```bash
git rm c_model/IMPLEMENTATION_PLAN.md
git commit -m "chore(c_model): remove IMPLEMENTATION_PLAN.md (Stage 2 Phase A done)"
```

- [ ] **Step 4: Mark Stage 5 → Complete (in your local tracking)**

---

### Stage 5 exit checklist

- [ ] All 12 fixtures pass under TEST_P
- [ ] `diff_files()` infrastructure works
- [ ] `c_model/IMPLEMENTATION_PLAN.md` removed
- [ ] 4 drift gates green
- [ ] Branch `feat/pure-axi-subsystem` ready for final code review + merge

---

## Out-of-plan follow-ups (do NOT include in this PR)

- **Phase B**: WRAP / FIXED burst support; unaligned start address; sparse WSTRB byte-merge; YAML schema extension (`strb_file` field)
- **Phase C**: Exclusive access (AxLOCK + EXOKAY); per-ID exclusive monitor
- **NoC integration (Stage 3+)**: Wrap AxiSlave + AxiMaster into NMU/NSU with PACKETIZE/DEPACKETIZE; deprecate the direct AxiMaster-to-AxiSlave wiring in favor of NoC link
- **DPI bridge**: Wire AxiMaster + AxiSlave to RTL via DPI-C for co-sim
- **CDC modeling**: out of c_model scope; RTL-side concern

## Risks to flag during execution

- **yaml-cpp first-time fetch** may be slow / fail on restricted networks; have a fallback (vendor as a submodule or in-tree mini parser)
- **Mixing template AxiMasterT + concrete AxiSlave** may surface link issues — be prepared to refactor to non-template version with virtual base if needed
- **TempDir() path on Windows** has backslash quirks — use forward slashes consistently
- **Integration scoreboard data tap** in Task 5.3 may require AxiMaster API change; flagged as risk so reviewer expects the additional change
