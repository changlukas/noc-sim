// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
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
