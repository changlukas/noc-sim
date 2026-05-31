#include "axi/scoreboard.hpp"
#include <gtest/gtest.h>

namespace axi = ni::cmodel::axi;

TEST(Scoreboard, NoUpdateOnDecerr) {
  axi::Scoreboard sb;
  std::vector<uint32_t> strb1(1, 0xFFFF'FFFFu);
  sb.handle_write_completed(
      axi::WriteResult{0x100, {0xAB, 0xCD, 0xEF, 0x12}, strb1, axi::Resp::DECERR, 1, 1},
      std::vector<uint8_t>{0xAB, 0xCD, 0xEF, 0x12},
      strb1);
  sb.handle_read_observed(axi::ReadResult{0x100, {0x00, 0x00}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 0u);
}

TEST(Scoreboard, MismatchDetected) {
  axi::Scoreboard sb;
  std::vector<uint32_t> strb1(1, 0xFFFF'FFFFu);
  sb.handle_write_completed(
      axi::WriteResult{0x200, {0xAB, 0xCD, 0xEF, 0x12}, strb1, axi::Resp::OKAY, 1, 1},
      std::vector<uint8_t>{0xAB, 0xCD, 0xEF, 0x12},
      strb1);
  sb.handle_read_observed(
      axi::ReadResult{0x200, {0xAB, 0xCD, 0xEE, 0x12}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 1u);
  EXPECT_FALSE(sb.mismatch_report().empty());
}

TEST(Scoreboard, MatchPassesSilent) {
  axi::Scoreboard sb;
  std::vector<uint32_t> strb1(1, 0xFFFF'FFFFu);
  sb.handle_write_completed(
      axi::WriteResult{0x300, {0xDE, 0xAD, 0xBE, 0xEF}, strb1, axi::Resp::OKAY, 1, 1},
      std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF},
      strb1);
  sb.handle_read_observed(
      axi::ReadResult{0x300, {0xDE, 0xAD, 0xBE, 0xEF}, axi::Resp::OKAY, 1, 2});
  EXPECT_EQ(sb.mismatch_count(), 0u);
  EXPECT_EQ(sb.reads_checked(), 1u);
}

TEST(Scoreboard, ReadFromUnwrittenAddrReturnsFillDefault) {
  axi::Scoreboard sb;
  sb.handle_read_observed(
      axi::ReadResult{0x400, {0x00, 0x00, 0x00, 0x00}, axi::Resp::OKAY, 1, 1});
  EXPECT_EQ(sb.mismatch_count(), 0u);
}
