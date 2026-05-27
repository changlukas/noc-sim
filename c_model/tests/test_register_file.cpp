#include "register_file.hpp"
#include <gtest/gtest.h>

using ni::cmodel::RegisterFile;
using ni::cmodel::AbiResult;

TEST(RegisterFile, ReadUnmappedReturnsDecErr) {
  RegisterFile rf;
  auto r = rf.read32(0xFFFC);  // unmapped offset
  EXPECT_EQ(r.status, AbiResult::DecErr);
  EXPECT_EQ(r.data,   0u);
}

TEST(RegisterFile, ResetValuesAreZeroForNow) {
  RegisterFile rf;
  auto r = rf.read32(ni::regs::PKT_PROBE_EN_OFFSET);
  EXPECT_EQ(r.status, AbiResult::Ok);
  EXPECT_EQ(r.data,   0u);
}

TEST(RegisterFile, WriteMisalignedReturnsSlvErr) {
  RegisterFile rf;
  auto r = rf.write32(ni::regs::PKT_PROBE_EN_OFFSET + 1, 0xDEADBEEF);
  EXPECT_EQ(r.status, AbiResult::SlvErr);  // per csr_policy: misaligned=slverr
}

TEST(RegisterFile, SubWordWriteReturnsSlvErr) {
  RegisterFile rf;
  auto r = rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0xDEADBEEF, /*wstrb=*/0b0001);
  EXPECT_EQ(r.status, AbiResult::SlvErr);  // per csr_policy: sub_word_write=slverr
}

TEST(RegisterFile, WriteFollowedByRead) {
  RegisterFile rf;
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0x12345678);
  auto r = rf.read32(ni::regs::PKT_PROBE_EN_OFFSET);
  EXPECT_EQ(r.status, AbiResult::Ok);
  EXPECT_EQ(r.data,   0x12345678u);
}

TEST(RegisterFile, ReadFieldMasks) {
  RegisterFile rf;
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0x000000F0);
  uint32_t v = rf.read_field(ni::regs::PKT_PROBE_EN_OFFSET, 0x000000F0);
  EXPECT_EQ(v, 0x0Fu);
}

TEST(RegisterFile, WriteFieldDoesNotTouchOtherBits) {
  RegisterFile rf;
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0xFFFFFFFF);
  rf.write_field(ni::regs::PKT_PROBE_EN_OFFSET, 0x000000F0, 0x0);
  auto r = rf.read32(ni::regs::PKT_PROBE_EN_OFFSET);
  EXPECT_EQ(r.data, 0xFFFFFF0Fu);
}

TEST(RegisterFile, LastWriteIrqInitiallyFalse) {
  RegisterFile rf;
  EXPECT_FALSE(rf.last_write_triggered_irq());
}

TEST(RegisterFile, LastWriteRw1cInitiallyFalse) {
  RegisterFile rf;
  EXPECT_FALSE(rf.last_write_cleared_rw1c_field());
}

TEST(RegisterFile, WriteFieldThenReadField) {
  RegisterFile rf;
  rf.write_field(ni::regs::PKT_PROBE_EN_OFFSET, 0x000000FF, 0xA5);
  uint32_t v = rf.read_field(ni::regs::PKT_PROBE_EN_OFFSET, 0x000000FF);
  EXPECT_EQ(v, 0xA5u);
}

TEST(RegisterFile, MultipleRegistersAreIndependent) {
  RegisterFile rf;
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET,   0xAAAA);
  rf.write32(ni::regs::PKT_PROBE_MODE_OFFSET, 0xBBBB);
  EXPECT_EQ(rf.read32(ni::regs::PKT_PROBE_EN_OFFSET).data,   0xAAAAu);
  EXPECT_EQ(rf.read32(ni::regs::PKT_PROBE_MODE_OFFSET).data, 0xBBBBu);
}

TEST(RegisterFile, ResetClearsAllStorage) {
  RegisterFile rf;
  rf.write32(ni::regs::PKT_PROBE_EN_OFFSET, 0xDEAD);
  rf.reset();
  EXPECT_EQ(rf.read32(ni::regs::PKT_PROBE_EN_OFFSET).data, 0u);
}
