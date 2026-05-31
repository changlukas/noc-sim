// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#include "axi/axi_master.hpp"
#include "axi/axi_slave.hpp"
#include "axi/memory.hpp"
#include "axi/scenario_parser.hpp"
#include "axi/scoreboard.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

namespace axi = ni::cmodel::axi;

// Watchdog cap to catch hangs / deadlocks in any single fixture run.
constexpr std::size_t kMaxCycles = 100'000;

struct IntegrationResult {
  bool        file_diff_pass;
  std::size_t scoreboard_mismatches;
  std::size_t cycle_count;
};

inline bool diff_files(const std::string& a, const std::string& b) {
  std::ifstream fa(a), fb(b);
  std::stringstream sa, sb;
  sa << fa.rdbuf();
  sb << fb.rdbuf();
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
    sb.handle_write_completed(wr, wr.data, wr.strb_per_beat);
  });
  master.on_read_observed([&](const axi::ReadResult& rr) {
    sb.handle_read_observed(rr);
  });

  std::size_t cycle = 0;
  while (!master.done()) {
    master.tick();
    slave.tick();
    mem.tick();
    if (++cycle > kMaxCycles) {
      return IntegrationResult{false, sb.mismatch_count(), cycle};
    }
  }
  bool fdiff = write_data_path.empty() ? true : diff_files(write_data_path, read_dump_path);
  return IntegrationResult{fdiff, sb.mismatch_count(), cycle};
}

struct FixtureParam {
  std::string yaml;
  std::string write_data;  // file to diff against the read dump (empty = skip diff)
  bool        expect_file_diff_pass;
  bool        expect_zero_mismatches;
};

// Generates a readable name for each TEST_P instance, e.g.
// AxiFixtures/IntegrationP.RunFixture/single_write_read_aligned
struct FixtureName {
  std::string operator()(const ::testing::TestParamInfo<FixtureParam>& info) const {
    auto n = info.param.yaml;
    auto dot = n.rfind('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    return n;
  }
};

class IntegrationP : public ::testing::TestWithParam<FixtureParam> {};

TEST_P(IntegrationP, RunFixture) {
  auto p = GetParam();
  std::string yaml_path = "fixtures/" + p.yaml;
  std::string wpath     = p.write_data.empty() ? std::string{} : ("fixtures/" + p.write_data);
  std::string rpath     = std::string(::testing::TempDir()) + "/" + p.yaml + ".read.txt";
  auto r = run_scenario(yaml_path, wpath, rpath);
  if (p.expect_file_diff_pass) {
    EXPECT_TRUE(r.file_diff_pass) << "file diff failed: " << p.yaml
                                  << " (wpath=" << wpath << ", rpath=" << rpath << ")";
  }
  if (p.expect_zero_mismatches) {
    EXPECT_EQ(r.scoreboard_mismatches, 0u) << "scoreboard mismatches: " << p.yaml;
  }
  EXPECT_LE(r.cycle_count, kMaxCycles) << "watchdog tripped: " << p.yaml;
}

INSTANTIATE_TEST_SUITE_P(
    AxiFixtures, IntegrationP,
    ::testing::Values(
        FixtureParam{"single_write_read_aligned.yaml",  "single_write_read_aligned_data.txt",  true,  true},
        FixtureParam{"burst_incr_2beat.yaml",           "burst_incr_2beat_data.txt",           true,  true},
        FixtureParam{"burst_incr_8beat.yaml",           "burst_incr_8beat_data.txt",           true,  true},
        FixtureParam{"multi_txn_same_id.yaml",          "multi_txn_same_id_data.txt",          true,  true},
        FixtureParam{"multi_txn_diff_id.yaml",          "multi_txn_diff_id_data.txt",          true,  true},
        FixtureParam{"decerr_oob_write.yaml",           "",                                    false, true},
        FixtureParam{"decerr_oob_read.yaml",            "",                                    false, true},
        FixtureParam{"latency_stress.yaml",             "latency_stress_data.txt",             true,  true},
        FixtureParam{"single_read_default_fill.yaml",   "",                                    false, true},
        FixtureParam{"burst_crosses_oob_boundary.yaml", "",                                    false, true},
        // backpressure_retry: 4 concurrent writes + 4 concurrent reads on the
        // same addresses race each other (no AXI write-before-read ordering),
        // so the read dump is non-deterministic relative to write completion.
        // We verify watchdog + scoreboard only; file diff is skipped.
        FixtureParam{"backpressure_retry.yaml",         "",                                    false, true},
        FixtureParam{"multi_outstanding_stress.yaml",   "multi_outstanding_stress_data.txt",   true,  true},
        // Phase B-2: INCR with unaligned start addr 0x1005 size=5; first beat
        // WSTRB clears lanes 0..4. Read dump cannot byte-match the write data
        // (alignment differs); scoreboard validates byte-level correctness.
        FixtureParam{"unaligned_start.yaml",            "",                                    false, true}),
    FixtureName{});
