// (Scoreboard pattern is independent; see ATTRIBUTION.md)
#pragma once
#include "axi/axi_master.hpp"
#include <iomanip>
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

}
