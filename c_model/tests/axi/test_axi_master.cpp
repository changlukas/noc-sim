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

#include "axi/axi_master.hpp"
#include "mock_slave.hpp"

class AxiMasterTest : public ScenarioParser {};

TEST_F(AxiMasterTest, ConstructsFromYamlAndOpensDump) {
  auto wpath = std::string(::testing::TempDir()) + "/w.txt";
  std::ofstream(wpath) << "AB CD EF 12 34 56 78 9A BC DE F0 11 22 33 44 55 "
                          "66 77 88 99 AA BB CC DD EE FF 00 11 22 33 44 55\n";
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_out.txt", 1, 1);
  EXPECT_FALSE(master.done());
}

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

  master.tick();
  EXPECT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_w.size(),  1u);
  EXPECT_EQ(mock.captured_aw[0].id, 7);
  EXPECT_EQ(mock.captured_w[0].data[0], 0x01);
  EXPECT_EQ(mock.captured_w[0].last, true);

  mock.queued_b.push_back(axi::BBeat{7, axi::Resp::OKAY, 0});
  master.tick();
  EXPECT_TRUE(fired);
  EXPECT_TRUE(master.done());
}
