# Pure AXI Subsystem — c_model Design (Stage 2)

> 2026-05-29 · supersedes Stage 1 FEAT-NMU-AXI_SLAVE_PORT
>
> Stage 2 切 3 phases；本 spec 設計 **Phase A** = INCR happy-path。Phase B/C 列 future roadmap。

---

## Purpose

蓋一組純 AXI4 c_model（無 NoC、無 NMU/NSU wrapper），可獨立驗證 AXI master / slave / memory 的 round-trip 正確性，用 YAML scenario + write_data.txt → read_data.txt → diff + scoreboard byte-map 雙保險驗證。同樣這幾個 class 未來會被裝進 NMU / NSU。

---

## Scope

**In scope (Phase A)**:
- INCR burst, aligned start address, full WSTRB
- 4 c_model class: AxiMaster / AxiSlave / Memory / Scoreboard
- Memory port interface (`IMemoryPort`) with correlation `tag`
- Configurable memory latency
- Parameterized outstanding depth (`max_outstanding_write/read`)
- Per-ID active burst tracking from day 1
- File-based YAML scenario + hex data files
- Tick-driven test harness
- Top-level diff + scoreboard byte-map verification

**Out of scope**:
- WRAP / FIXED burst → Phase B
- Unaligned start, sparse WSTRB → Phase B
- Exclusive access (AxLOCK + EXOKAY) → Phase C
- AXI4 stateful protocol checks (W_BEAT_COUNT 等) → 未來 dedicated checker
- NoC / NMU / NSU integration → 未來
- CDC / metastability / performance modeling → 永遠 c_model 外
- DPI co-sim wiring → 未來

**Scope deviation from prior Stage 1 (FEAT-NMU-AXI_SLAVE_PORT)**:
Stage 1 設計把 AxiSlavePort 當 NMU forwarder（pasive beat queue），本 Stage 2 改成「正常 AXI slave controller」（active、talks to memory port）。Stage 1 的 7 個 commit 對應檔案全部 **刪除**（`c_model/include/nmu/axi_slave_port.hpp`、`c_model/tests/nmu/*`、`c_model/tests/CMakeLists.txt` 的 `add_subdirectory(nmu)`）。Stage 1 spec doc 保留為歷史紀錄。未來 NMU forwarder 重新設計（Stage 3+）。

---

## Architecture

**Namespace**: `ni::cmodel::axi`（與 Layer A `ni::cmodel::`、NMU `ni::cmodel::nmu::` 平行）

**File layout**:
```
c_model/include/axi/
    ATTRIBUTION.md          ← cocotbext-axi MIT attribution
    types.hpp               ← Burst/Resp enums + AwBeat/WBeat/ArBeat/BBeat/RBeat
    memory_port.hpp         ← MemWriteReq/Resp + MemReadReq/Resp + IMemoryPort
    memory.hpp              ← class Memory : IMemoryPort
    axi_slave.hpp           ← class AxiSlave (per-ID active tracking + tag correlation)
    axi_master.hpp          ← class AxiMaster (YAML driven + parameterized outstanding)
    scoreboard.hpp          ← class Scoreboard (byte-map expected vs actual)
    scenario_parser.hpp     ← YAML loader (yaml-cpp 後端)

c_model/tests/axi/
    CMakeLists.txt
    test_memory.cpp
    test_axi_slave.cpp      ← 用 MockMemoryPort 隔離測
    test_axi_master.cpp     ← 用 MockSlave 隔離測
    test_scoreboard.cpp
    test_integration.cpp    ← TEST_P loading fixtures/*.yaml
    fixtures/
        single_write_read_aligned.yaml + write_data.txt
        burst_incr_2beat.yaml + write_data.txt
        burst_incr_8beat.yaml + write_data.txt
        multi_txn_same_id.yaml + write_data.txt
        multi_txn_diff_id.yaml + write_data.txt
        decerr_oob_write.yaml + write_data.txt
        decerr_oob_read.yaml + write_data.txt
        latency_stress.yaml + write_data.txt
        single_read_default_fill.yaml
        burst_crosses_oob_boundary.yaml + write_data.txt
        backpressure_retry.yaml + write_data.txt
        multi_outstanding_stress.yaml + write_data.txt
```

**Dependency**: `yaml-cpp`（CMake FetchContent，仿 GoogleTest 寫法）

**Tick contract**: **same-cycle mailbox** — push 在某 class.tick() 內，**同 tick** 內下游 class.tick() 立刻可見。順序：master → slave → memory → scoreboard → cycle++。tests 不該 assert 精細 latency（< 1 整 loop）。

**OSS strategy (Approach Z)**: 純 C++17 + GoogleTest，algorithm line-by-line port 自 alexforencich/cocotbext-axi（MIT）。每個 `.hpp` 開頭 1 行 attribution。集中 `ATTRIBUTION.md` 列詳細映射。

**Future extension**: `IMemoryPort` 預留為 `AddressSpace` / `MemoryMap` 演化點（之後 NSU 接多個 memory region 時擴展，本 Phase A 只實作單一 Memory）。

---

## Components

### `types.hpp`

```cpp
namespace ni::cmodel::axi {

constexpr int DATA_BYTES = ni::WSTRB_WIDTH;          // = 32, 為「byte count」語意更清楚
constexpr int DATA_WIDTH = DATA_BYTES * 8;           // = 256

enum class Burst : uint8_t { FIXED=0, INCR=1, WRAP=2 };
enum class Resp  : uint8_t { OKAY=0, EXOKAY=1, SLVERR=2, DECERR=3 };

struct AwBeat { uint8_t id; uint64_t addr; uint8_t len, size;
                Burst burst; uint8_t cache, lock, prot, region, user, qos; };
struct WBeat  { std::array<uint8_t, DATA_BYTES> data; uint32_t strb;
                bool last; uint8_t user; };
struct ArBeat { /* 同 AwBeat */ };
struct BBeat  { uint8_t id; Resp resp; uint8_t user; };
struct RBeat  { uint8_t id; std::array<uint8_t, DATA_BYTES> data;
                Resp resp; bool last; uint8_t user; };

}  // namespace ni::cmodel::axi
```

### `memory_port.hpp` — IMemoryPort contract

```cpp
struct MemWriteReq { uint64_t addr; std::array<uint8_t, DATA_BYTES> data;
                     uint32_t strb; uint8_t id; bool last; uint64_t tag; };
struct MemWriteResp { uint8_t id; Resp resp; uint64_t tag; };
struct MemReadReq  { uint64_t addr; uint8_t size; uint8_t id; bool last; uint64_t tag; };
struct MemReadResp { uint8_t id; std::array<uint8_t, DATA_BYTES> data;
                     Resp resp; bool last; uint64_t tag; };

class IMemoryPort {
public:
  virtual ~IMemoryPort() = default;
  virtual bool submit_write(const MemWriteReq&) = 0;   // false if queue full → caller retry
  virtual bool submit_read (const MemReadReq&)  = 0;
  virtual std::optional<MemWriteResp> pop_write_resp() = 0;
  virtual std::optional<MemReadResp>  pop_read_resp () = 0;
};
```

`tag` 為 AxiSlave 私有 correlation token（Phase A 可不用，Phase C 多 outstanding 必要）。

### `memory.hpp`

```cpp
class Memory : public IMemoryPort {
public:
  Memory(uint64_t base_addr, std::size_t size_bytes,
         std::size_t write_latency_ticks, std::size_t read_latency_ticks,
         std::size_t pending_queue_depth = 32,
         uint8_t fill_byte = 0x00);
  // IMemoryPort overrides + tick() + peek() (observation)
};
```
- `std::vector<uint8_t> storage_` 連續 `size_bytes`，未寫過的 byte 為 `fill_byte`
- 2 個 pending queue（每 entry: req + countdown）+ 2 個 response queue
- `tick()`：所有 pending countdown -1；countdown==0 時執行 read/write、生 response 入 response queue
- In-bounds check：`addr ∈ [base, base+size_bytes)` 且 `addr + bytes_per_beat <= base+size_bytes` → OKAY；否則 DECERR、不動 storage
- WSTRB：write 時只寫 strb=1 的 byte（strb=0 保留原值）

### `axi_slave.hpp`

```cpp
class AxiSlave {
public:
  explicit AxiSlave(IMemoryPort& memory_port, std::size_t channel_queue_depth = 32);

  // 對 master 介面（beat-level）
  bool push_aw(const AwBeat&);     // false if internal queue full
  bool push_w (const WBeat&);
  bool push_ar(const ArBeat&);
  std::optional<BBeat> pop_b();
  std::optional<RBeat> pop_r();
  void tick();
};
```
- 5 個 channel queue（FIFO）+ per-ID 結構 `std::map<uint8_t, WriteBurstState>` / `std::map<uint8_t, ReadBurstState>`
- **AW/W independence**：W 可以先到 AW；w_q 暫存、AW 到時按 W issue 順序匹配（AXI4 規則：W-follows-AW-issue-order, 不是 W-matches-AW-by-ID）
- **Burst-atomic OOB check**：AW 到時計算 burst 全範圍，若超 memory bounds → 直接 push B(DECERR)、不 submit 任何 W beat、丟掉對應的 W beats
- `tick()` logic：
  1. Drain memory response (pop_write_resp / pop_read_resp)，用 tag 找對應 active burst，更新 beats_completed / last → 必要時 push B / R
  2. 對每個 active write burst：若 w_q 有對應下一 W beat → submit memory write（tag 為 implementation-defined opaque token，唯一識別此 beat — 例：`(burst_seq_no << 8) | beat_idx`，AxiSlave 自管編解）
  3. 對每個 active read burst：若 memory 有空間 → submit memory read（tag 同上）
  4. 啟動 AW：若 active_writes_ 還沒此 ID → 取 aw_q_ 前端、新增 active_write
  5. 啟動 AR：同上
- Backpressure：submit_write 回 false → 留 beat 在 burst state、下個 tick 重試

### `axi_master.hpp`

```cpp
struct ScenarioTransaction {
  enum class Op { Write, Read };
  Op       op;
  uint64_t addr;
  uint8_t  id;
  uint8_t  len, size;
  Burst    burst;
  std::string data_file;     // write only
  std::string dump_file;     // read only
  std::size_t scenario_line; // 給 debug message 用
};

struct WriteResult { uint64_t addr; std::size_t data_len; Resp resp;
                     uint8_t id; std::size_t scenario_line; };
struct ReadResult  { uint64_t addr; std::vector<uint8_t> data; Resp resp;
                     uint8_t id; std::size_t scenario_line; };

class AxiMaster {
public:
  AxiMaster(const std::string& scenario_yaml,
            AxiSlave& slave,
            const std::string& read_dump_path,
            std::size_t max_outstanding_write = 1,
            std::size_t max_outstanding_read  = 1);

  void tick();
  bool done() const;

  void on_write_completed(std::function<void(const WriteResult&)> cb);
  void on_read_observed  (std::function<void(const ReadResult&)>  cb);
};
```
- ctor：parse YAML（仿 cocotbext-axi `AxiMaster.write/read` API shape）、load write_data.txt 進 buffer、open read_dump_
- `tick()`：admission control 用 max_outstanding_*；發 AW/W/AR beat、drain B/R、累進 read accumulator、burst 結束 dump 到 read_dump_ + 觸發 callback
- `done()`：所有 scenario txn 完成 + 所有 active 清空
- Callback 在 B/R 收到時觸發；resp != OKAY 仍觸發（Scoreboard 自己決定是否更新 byte_map）

### `scoreboard.hpp`

```cpp
class Scoreboard {
public:
  // Subscribe (via AxiMaster.on_*)
  void handle_write_completed(const WriteResult&, const std::vector<uint8_t>& data, uint32_t strb_per_beat);
  void handle_read_observed  (const ReadResult&);

  std::size_t mismatch_count() const;
  std::size_t reads_checked() const;
  std::vector<std::string> mismatch_report() const;

private:
  std::map<uint64_t, uint8_t> expected_byte_map_;
};
```
- `handle_write_completed`：只在 `resp == OKAY` 更新 byte_map（按 strb 逐 byte）
- `handle_read_observed`：查 byte_map 比對；mismatch 入 log
- mismatch log 包含 `addr / expected / actual / scenario_line`

### YAML schema (Phase A minimal)

`config` section 所有欄位皆 optional；缺欄位用以下預設值。`transactions` 必填且非空。Unknown fields → `throw std::runtime_error`（user error）。

```yaml
config:
  memory_base:           0x1000      # default: 0x0
  memory_size:           0x100000    # default: 0x10000 (64KB)
  write_latency:         5           # default: 1
  read_latency:          5           # default: 1
  max_outstanding_write: 1           # default: 1（stress fixture override 為 8）
  max_outstanding_read:  1           # default: 1

transactions:
  - op: write
    addr:  0x1000
    id:    0x05
    len:   7                          # 8 beats
    size:  5                          # 32 bytes/beat
    burst: INCR
    data_file: write_data.txt
  - op: read
    addr:  0x1000
    id:    0x05
    len:   7
    size:  5
    burst: INCR
    dump_file: read_data.txt
```

### File format `write_data.txt` / `read_data.txt`

每行一個 beat，32 個 hex byte 空白分隔：
```
AB CD EF 12 34 56 78 9A BC DE F0 11 22 33 44 55 66 77 88 99 AA BB CC DD EE FF 00 11 22 33 44 55
55 44 33 22 ...
```

---

## Data flow

### Single write burst（INCR len=7 size=5 id=5 addr=0x1000）

```
Tick   AxiMaster                  AxiSlave                  Memory              Scoreboard
─────  ────────────────────       ────────────────────      ────────────────    ──────────
  0    pop scenario[0]=write
       push_aw(id=5, ...)
  1    push_w(beat 0, last=0)     pop_aw → active_writes_[5]
                                  burst-atomic OOB check: PASS
  2    push_w(beat 1, last=0)     pop_w(0), submit_write    enqueue req
                                  (addr=0x1000, tag=encode(id=5, beat=0))
  ...
  8    push_w(beat 7, last=1)     pop_w(7), submit_write    write 0 完成 → resp_q
  9                                pop_write_resp → 對應    write 1 完成
                                  active_write 計數
  13                              last beat write 完成 → push B(id=5, OKAY)
  14   pop_b(id=5, OKAY)
       fire on_write_completed(WriteResult) ──────────────────────────────────→ resp==OKAY 才更新
                                                                                  byte_map[0x1000..0x1100]
       active 清空
  15   pop scenario[1]=read       ...
```

### Single read burst

對稱，差異：master 不送 W，slave per-beat submit_read，response 累進 R beats 後 push 給 master、master accumulate 後 dump 到 read_data.txt + fire `on_read_observed`。Scoreboard 收到 `ReadResult` 後逐 byte 對照 byte_map。

### Burst-atomic OOB（DECERR path）

AW 到時若 `[addr, addr+(len+1)*(1<<size)-1]` 超出 memory bounds：
- AxiSlave 直接 push `B(id, DECERR)` 給 master、**不 submit 任何 memory write**、丟掉這個 burst 對應的 W beats（按 AW issue 順序計，丟前 N 個 W beat）
- Master 收 `B(DECERR)` → fire `on_write_completed(resp=DECERR)` → Scoreboard 不更新 byte_map

Read OOB 對稱：AxiSlave 直接 push N 個 `R(DECERR)` beat（last on 最後），跳過 memory_port。

### Tick loop ordering

```cpp
while (!master.done()) {
  master.tick();    // 1. drain B/R → fire events; admission control → push next AW/W/AR
  slave.tick();     // 2. process inbound → memory port; drain memory resp → push B/R
  memory.tick();    // 3. advance latency countdown → complete due requests
  scoreboard.tick();// 4. (optional) process pending event queue
  ++cycle;
  if (cycle > MAX_CYCLES) FAIL("Test hang / deadlock");
}
```
`MAX_CYCLES` 預設 100K。Watchdog 防 deadlock false positive。

---

## Error handling

| 類別 | 偵測 | 行為 | All builds? |
|------|------|------|------------|
| YAML unknown field / unsupported burst (≠INCR Phase A) / size>5 / unaligned start (Phase A) / lock=1 (Phase A) | Scenario parser ctor 時 | `throw std::runtime_error`、test FAIL | **是** |
| write_data.txt missing / size 不夠 | Master ctor or first W beat | throw、test FAIL | 是 |
| read_data.txt write fail（disk full）| Master dump | throw、test FAIL | 是 |
| Memory OOB（burst-atomic）| AxiSlave AW receive | DECERR via normal path | RTL-faithful（不 throw）|
| Internal invariant（W beats > len+1、active_write 重複 ID 等）| Slave tick | debug `assert`、release silent | Debug only |
| Watchdog timeout | Harness | `FAIL("watchdog timeout")` | 是 |

**原則**：**user input violation → throw（all builds）；c_model internal invariant → assert（debug only）**

**Mismatch report 格式**（Scoreboard 友善）：
```
[Scoreboard] MISMATCH at addr=0x10A0 (read txn id=5, scenario line 12, byte 16):
  expected=0xAB  actual=0x33
```

**No runtime reset in Phase A**：所有 class 透過 dtor + ctor 重啟。`reset()` method 不加。

---

## Testing

### Test file 分佈

| File | 目標 | 用 mock？ |
|------|------|----------|
| `test_memory.cpp` | submit/pop latency timing、in-bounds OKAY / out DECERR、WSTRB byte-merge | 不需 |
| `test_axi_slave.cpp` | beat queue、per-ID active map、AW/W independence、tag correlation、backpressure retry | `MockMemoryPort` |
| `test_axi_master.cpp` | YAML parse、scenario sequencing、admission control（max_outstanding）、read accumulator、callback fire | `MockSlave` |
| `test_scoreboard.cpp` | byte_map update on OKAY only、mismatch detection、sparse strb byte-merge（為 Phase B 鋪路）| 不需 |
| `test_integration.cpp` | TEST_P loading fixtures/*.yaml → 完整 4-class loop | 不需 |

### Phase A integration fixtures（12 個）

| # | Fixture | 驗證 |
|---|---------|------|
| 1 | `single_write_read_aligned.yaml` | 1 beat write、1 beat read |
| 2 | `burst_incr_2beat.yaml` | INCR len=1 |
| 3 | `burst_incr_8beat.yaml` | INCR len=7 |
| 4 | `multi_txn_same_id.yaml` | 連續 2 write + 2 read（單 ID）|
| 5 | `multi_txn_diff_id.yaml` | 連續 txn 用不同 ID |
| 6 | `decerr_oob_write.yaml` | OOB write → BRESP=DECERR、byte_map 不變、Memory storage 不變 |
| 7 | `decerr_oob_read.yaml` | OOB read → RRESP=DECERR |
| 8 | `latency_stress.yaml` | write_latency=20 read_latency=20，驗 watchdog 不誤觸 |
| 9 | `single_read_default_fill.yaml` | 未寫過的 addr 讀回 fill_byte（=0x00）|
| 10 | `burst_crosses_oob_boundary.yaml` | INCR burst 部分超界 → 整 burst DECERR |
| 11 | `backpressure_retry.yaml` | Memory queue depth 設小、撐爆，驗 AxiSlave retry 不丟 beat |
| 12 | `multi_outstanding_stress.yaml` | `max_outstanding_*=8`，16 txn 不等回應連發；驗 per-ID + tag correlation |

### TEST_P 範本

```cpp
struct ScenarioParam { std::string yaml; bool expect_pass; };

class IntegrationP : public ::testing::TestWithParam<ScenarioParam> {};

TEST_P(IntegrationP, RunFixture) {
  auto pr = GetParam();
  auto r = run_scenario(pr.yaml);
  if (pr.expect_pass) {
    EXPECT_TRUE(r.file_diff_pass);
    EXPECT_EQ(r.scoreboard_mismatch_count, 0u);
  } else {
    EXPECT_GT(r.scoreboard_mismatch_count + r.parse_errors, 0u);
  }
}

INSTANTIATE_TEST_SUITE_P(AxiFixtures, IntegrationP, ::testing::Values(/* 12 ScenarioParam */));
```

### Drift gates

```
cd c_model && cmake --build build && ctest
cd ../spec_validate && py -3 -m pytest -q
py -3 tools/codegen.py --check
py -3 tools/gen_inventory.py --check
```

### OSS attribution

- 每個 `.hpp` 開頭 1 行 `// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md`
- `c_model/include/axi/ATTRIBUTION.md` 列詳細映射 (axi_master.py → axi_master.hpp 等)
- `c_model/tests/axi/fixtures/README.md` 標 scenarios inspired by cocotbext-axi `test_axi.py` directed cases

---

## Future roadmap (Phase B / C)

### Phase B（純新 logic，不動 Phase A 結構）

- WRAP / FIXED burst support（AxiSlave 算 addr 公式擴展）
- Unaligned start address（first beat WSTRB 標部分有效 byte）
- Sparse WSTRB byte-merge（Memory 已支援、AxiMaster 從 strb_file 讀 WSTRB pattern）
- YAML schema extend：optional `strb_file` field

### Phase C（純新 logic）

- Exclusive access：AxiSlave 加 exclusive monitor（per-ID + addr range tracking）；ARLOCK=1 → 記 exclusive read；AWLOCK=1 → 檢查無中介 write → EXOKAY / OKAY
- YAML schema extend：optional `lock` field on AW/AR

### 未來 NoC 整合 (Stage 3+)

- 本 axi/* 維持不變，作為 NSU 的 AXI master + 可選的 testbench 用 AXI slave
- NMU 重新設計 AXI slave forwarder（不是本 design 範圍；架構與本 design 解耦）

---

## [TBD]

- `MAX_CYCLES` 預設 100K 適不適合所有 Phase A fixtures（latency_stress.yaml 可能需要 override，預留 yaml-level config）
- `pending_queue_depth` Memory 預設 32 是否合理（cocotbext-axi 預設 2，我們較大為對齊 backpressure_retry fixture）
- `scenario_line` 在 mismatch report 內目前用 1-based — 等 spec 確認標準
