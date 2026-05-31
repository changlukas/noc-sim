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
  ASSERT_EQ(sc.transactions.size(), 1u);
  EXPECT_EQ(sc.transactions[0].addr, 0x1003u);
  EXPECT_EQ(sc.transactions[0].burst, axi::Burst::INCR);
}

TEST_F(ScenarioParser, StrbFileFieldAccepted) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
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
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: w.txt
)YAML");
  auto sc = axi::load_scenario(path);
  ASSERT_EQ(sc.transactions.size(), 1u);
  EXPECT_EQ(sc.transactions[0].strb_file, "");
}

TEST_F(ScenarioParser, ReadTxnIgnoresStrbFile) {
  auto path = write_tmp(R"YAML(
transactions:
  - op: read
    addr: 0x0
    id: 0x2
    len: 0
    size: 5
    burst: INCR
    dump_file: r.txt
    strb_file: s.txt
)YAML");
  auto sc = axi::load_scenario(path);
  ASSERT_EQ(sc.transactions.size(), 1u);
  EXPECT_EQ(sc.transactions[0].strb_file, "");
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

  master.tick();
  EXPECT_EQ(mock.captured_ar.size(), 1u);

  axi::RBeat r{}; r.id = 9; r.data.fill(0xAB);
  r.resp = axi::Resp::OKAY; r.last = true; r.user = 0;
  mock.queued_r.push_back(r);
  master.tick();
  EXPECT_TRUE(master.done());

  std::ifstream f(dumpPath); std::string line; std::getline(f, line);
  EXPECT_EQ(line.substr(0, 5), "AB AB");
}

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
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r.txt", 2, 1);
  master.tick();
  EXPECT_EQ(mock.captured_aw.size(), 2u);
  EXPECT_EQ(mock.captured_aw[0].id, 1);
  EXPECT_EQ(mock.captured_aw[1].id, 2);

  mock.queued_b.push_back(axi::BBeat{1, axi::Resp::OKAY, 0});
  master.tick();
  EXPECT_EQ(mock.captured_aw.size(), 3u);
  EXPECT_EQ(mock.captured_aw[2].id, 3);
}

TEST_F(AxiMasterTest, StrbFileMissingThrows) {
  auto wpath = std::string(::testing::TempDir()) + "/w_missing_strb.txt";
  std::ofstream(wpath) << "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 "
                          "11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20\n";
  auto strb_missing = std::string(::testing::TempDir()) + "/does_not_exist.strb";
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + R"YAML(
    strb_file: )YAML" + strb_missing + "\n");

  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_missing.txt");
  EXPECT_THROW(master.tick(), std::runtime_error);
}

TEST_F(AxiMasterTest, StrbFileLineCountMismatchThrows) {
  auto wpath = std::string(::testing::TempDir()) + "/w_lc.txt";
  std::ofstream(wpath) << "01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 "
                          "11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F 20\n";
  // expected beats = len + 1 = 1, but provide 2 strb tokens
  auto spath = std::string(::testing::TempDir()) + "/s_lc.txt";
  std::ofstream(spath) << "FFFFFFFF FFFFFFFF\n";
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
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_lc.txt");
  EXPECT_THROW(master.tick(), std::runtime_error);
}

// Phase B-2.2: AxiMaster aligns AW.addr DOWN to (1<<size) and masks first-beat
// WSTRB lanes 0..first_lane-1 where first_lane = txn.addr & (DATA_BYTES - 1).

namespace {
// Write 32 arbitrary non-zero bytes to a tmp data file and return the path.
std::string write_32byte_tmp_data(const std::string& tag) {
  auto path = std::string(::testing::TempDir()) + "/" + tag + ".dat";
  std::ofstream f(path);
  for (int j = 0; j < 32; ++j) {
    if (j) f << ' ';
    char buf[4];
    std::snprintf(buf, sizeof(buf), "%02X",
                  static_cast<unsigned>(0x40u + (j & 0x3Fu)));
    f << buf;
  }
  f << '\n';
  return path;
}
}  // namespace

TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked_Size5_Off3) {
  auto wpath = write_32byte_tmp_data("u_s5_off3");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1003
    id: 0x1
    len: 0
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_u_s5_off3.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x1000u);
  ASSERT_EQ(mock.captured_w.size(), 1u);
  EXPECT_EQ(mock.captured_w[0].strb, 0xFFFFFFF8u);
  EXPECT_EQ(mock.captured_w[0].last, true);
}

TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked_Size4_Off1) {
  auto wpath = write_32byte_tmp_data("u_s4_off1");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1001
    id: 0x1
    len: 0
    size: 4
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_u_s4_off1.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x1000u);
  ASSERT_EQ(mock.captured_w.size(), 1u);
  EXPECT_EQ(mock.captured_w[0].strb, 0xFFFFFFFEu);
}

TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked_Size3_Off7) {
  auto wpath = write_32byte_tmp_data("u_s3_off7");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x1007
    id: 0x1
    len: 0
    size: 3
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_u_s3_off7.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x1000u);
  ASSERT_EQ(mock.captured_w.size(), 1u);
  EXPECT_EQ(mock.captured_w[0].strb, 0xFFFFFF80u);
}

TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked_Size2_OffF) {
  auto wpath = write_32byte_tmp_data("u_s2_offF");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x100F
    id: 0x1
    len: 0
    size: 2
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_u_s2_offF.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x100Cu);
  ASSERT_EQ(mock.captured_w.size(), 1u);
  EXPECT_EQ(mock.captured_w[0].strb, 0xFFFF8000u);
}

TEST_F(AxiMasterTest, UnalignedAddrFirstBeatStrbMasked_Size1_Off1F) {
  auto wpath = write_32byte_tmp_data("u_s1_off1F");
  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x101F
    id: 0x1
    len: 0
    size: 1
    burst: INCR
    data_file: )YAML") + wpath + "\n");
  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_u_s1_off1F.txt");
  master.tick();
  ASSERT_EQ(mock.captured_aw.size(), 1u);
  EXPECT_EQ(mock.captured_aw[0].addr, 0x101Eu);
  ASSERT_EQ(mock.captured_w.size(), 1u);
  EXPECT_EQ(mock.captured_w[0].strb, 0x80000000u);
}

TEST_F(AxiMasterTest, StrbFilePropagatesToWChannel) {
  // 2-beat write with sparse first beat (0x0F) and full second beat (0xFFFFFFFF).
  // Verify both strb masks ride the W channel in order.
  auto wpath = std::string(::testing::TempDir()) + "/w_strb_prop.txt";
  // 2 beats * DATA_BYTES bytes; arbitrary data, content not under test.
  {
    std::ofstream wf(wpath);
    for (int beat = 0; beat < 2; ++beat) {
      for (int j = 0; j < axi::DATA_BYTES; ++j) {
        if (j) wf << ' ';
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X",
                      static_cast<unsigned>((beat * axi::DATA_BYTES + j) & 0xFFu));
        wf << buf;
      }
      wf << '\n';
    }
  }
  auto spath = std::string(::testing::TempDir()) + "/s_strb_prop.txt";
  std::ofstream(spath) << "0000000F\nFFFFFFFF\n";

  auto yaml = write_tmp(std::string(R"YAML(
transactions:
  - op: write
    addr: 0x0
    id: 0x4
    len: 1
    size: 5
    burst: INCR
    data_file: )YAML") + wpath + R"YAML(
    strb_file: )YAML" + spath + "\n");

  ni::cmodel::axi::testing::MockSlave mock;
  axi::AxiMasterT<ni::cmodel::axi::testing::MockSlave> master(
      yaml, mock, std::string(::testing::TempDir()) + "/r_strb_prop.txt");

  master.tick();
  ASSERT_EQ(mock.captured_w.size(), 2u);
  EXPECT_EQ(mock.captured_w[0].strb, 0x0000000Fu);
  EXPECT_EQ(mock.captured_w[0].last, false);
  EXPECT_EQ(mock.captured_w[1].strb, 0xFFFFFFFFu);
  EXPECT_EQ(mock.captured_w[1].last, true);
}
