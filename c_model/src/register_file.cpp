#include "register_file.hpp"
#include "ni_spec.hpp"
#include <unordered_set>

namespace ni::cmodel {

namespace {
  // Build a set of all elaborated register offsets at startup.
  // Sufficiency finding F-005: codegen should elaborate ALL_OFFSETS[] array
  // so this list isn't hand-maintained.
  const std::unordered_set<uint32_t>& known_offsets() {
    static const std::unordered_set<uint32_t> s = {
      ni::regs::PKT_PROBE_EN_OFFSET,
      ni::regs::PKT_PROBE_MODE_OFFSET,
      ni::regs::PKT_WINDOW_SIZE_OFFSET,
      ni::regs::PKT_BYTE_COUNT_OFFSET,
      ni::regs::PKT_BANDWIDTH_OFFSET,
      ni::regs::TXN_PROBE_EN_OFFSET,
      ni::regs::TXN_THRESHOLD_0_OFFSET,
      ni::regs::TXN_THRESHOLD_1_OFFSET,
      ni::regs::TXN_THRESHOLD_2_OFFSET,
      ni::regs::TXN_THRESHOLD_3_OFFSET,
      ni::regs::TXN_BIN_0_COUNT_OFFSET,
      ni::regs::TXN_BIN_1_COUNT_OFFSET,
      ni::regs::TXN_BIN_2_COUNT_OFFSET,
      ni::regs::TXN_BIN_3_COUNT_OFFSET,
      ni::regs::TXN_BIN_4_COUNT_OFFSET,
      ni::regs::TXN_MIN_LATENCY_OFFSET,
      ni::regs::TXN_MAX_LATENCY_OFFSET,
      ni::regs::TXN_TOTAL_COUNT_OFFSET,
      ni::regs::ERR_STATUS_OFFSET,
      ni::regs::ECC_UNCORR_ERR_CNT_OFFSET,
      ni::regs::LAST_ERR_INFO_OFFSET,
      ni::regs::IRQ_ENABLE_OFFSET,
      ni::regs::ECC_CORR_ERR_CNT_OFFSET,
      ni::regs::ROUTE_PAR_ERR_CNT_OFFSET,
      ni::regs::AXI_PARITY_ERR_CNT_OFFSET,
      ni::regs::PENDING_R_COUNT_OFFSET,
      ni::regs::PENDING_W_COUNT_OFFSET,
      ni::regs::QUIESCE_CTRL_OFFSET,
      ni::regs::QUIESCE_STATUS_OFFSET,
      ni::regs::EXCLUSIVE_MONITOR_CTRL_OFFSET,
      ni::regs::EXCLUSIVE_MONITOR_STATUS_OFFSET,
    };
    return s;
  }
}

RegisterFile::RegisterFile() {
  reset();
}

void RegisterFile::reset() {
  storage_.clear();
  // sufficiency finding F-004: codegen does not elaborate per-register
  // reset values — for now reset to 0 universally.
  for (auto off : known_offsets()) storage_[off] = 0;
  last_irq_ = false;
  last_rw1c_clear_ = false;
}

bool RegisterFile::is_mapped_(uint32_t offset) const {
  return known_offsets().count(offset) != 0;
}
bool RegisterFile::is_wo_(uint32_t /*offset*/) const {
  return false;  // F-006: codegen needs to elaborate access mode per offset
}
bool RegisterFile::is_rw1c_(uint32_t /*offset*/) const {
  return false;  // F-006 same
}

AbiResponse RegisterFile::read32(uint32_t offset) {
  // Check misalignment first: spec distinguishes misaligned vs unmapped,
  // and mapped offsets are all word-aligned, so a misaligned offset can
  // never be a mapped one — checking unmapped first would mis-classify
  // misaligned accesses as unmapped (DecErr) instead of misaligned (SlvErr).
  if (offset % 4 != 0) {
    if constexpr (ni::regs::csr_policy::MISALIGNED_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    } else {
      return {AbiResult::DecErr, 0};  // misaligned = lower-aligned/decerr fallback
    }
  }
  if (!is_mapped_(offset)) {
    // policy: unmapped_read = decerr
    if constexpr (ni::regs::csr_policy::UNMAPPED_READ_IS_DECERR) {
      return {AbiResult::DecErr, 0};
    } else {
      return {AbiResult::Ok, 0};  // unmapped_read = zero (fallback)
    }
  }
  return {AbiResult::Ok, storage_[offset]};
}

AbiResponse RegisterFile::write32(uint32_t offset, uint32_t value, uint8_t wstrb) {
  // Same ordering as read32: misalignment is checked before mapping.
  if (offset % 4 != 0) {
    if constexpr (ni::regs::csr_policy::MISALIGNED_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    }
    return {AbiResult::DecErr, 0};
  }
  if (!is_mapped_(offset)) {
    if constexpr (ni::regs::csr_policy::UNMAPPED_READ_IS_DECERR) {
      // write follows same policy as read for unmapped (no separate spec field)
      return {AbiResult::DecErr, 0};
    }
    return {AbiResult::Ok, 0};
  }
  if (wstrb != 0b1111) {
    if constexpr (ni::regs::csr_policy::SUB_WORD_WRITE_IS_SLVERR) {
      return {AbiResult::SlvErr, 0};
    }
    // sub_word_write = ignored: silently drop, return Ok
    return {AbiResult::Ok, 0};
  }
  storage_[offset] = value;
  last_irq_ = false;
  last_rw1c_clear_ = false;
  return {AbiResult::Ok, 0};
}

uint32_t RegisterFile::read_field(uint32_t offset, uint32_t mask) const {
  auto it = storage_.find(offset);
  uint32_t val = (it == storage_.end()) ? 0 : it->second;
  int shift = 0;
  while (shift < 32 && !((mask >> shift) & 1)) ++shift;
  return (val & mask) >> shift;
}

void RegisterFile::write_field(uint32_t offset, uint32_t mask, uint32_t value) {
  uint32_t v = storage_[offset];
  int shift = 0;
  while (shift < 32 && !((mask >> shift) & 1)) ++shift;
  v = (v & ~mask) | ((value << shift) & mask);
  storage_[offset] = v;
}

} // namespace ni::cmodel
