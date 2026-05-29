// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include "axi/memory_port.hpp"
#include <deque>
#include <map>
#include <optional>

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
  void set_memory_bounds(uint64_t base, std::size_t size) {
    bounds_base_ = base; bounds_size_ = size; bounds_set_ = true;
  }

  std::size_t aw_q_size() const { return aw_q_.size(); }
  std::size_t w_q_size()  const { return w_q_.size();  }
  std::size_t ar_q_size() const { return ar_q_.size(); }
  std::size_t b_q_size()  const { return b_q_.size();  }
  std::size_t r_q_size()  const { return r_q_.size();  }

private:
  struct WriteBurstState {
    AwBeat aw;
    std::size_t beats_submitted = 0;
    std::size_t beats_completed = 0;
    Resp worst_resp = Resp::OKAY;  // accumulate worst across burst
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
  std::deque<uint8_t> aw_issue_order_;  // oldest first; for W matching per AXI4
  std::map<uint8_t, WriteBurstState> active_writes_;
  std::map<uint8_t, ReadBurstState>  active_reads_;
  uint64_t bounds_base_ = 0;
  std::size_t bounds_size_ = 0;
  bool bounds_set_ = false;
};

inline void AxiSlave::tick() {}  // stub; real impl in Task 3.2+

}  // namespace ni::cmodel::axi
