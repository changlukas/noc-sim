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

inline void AxiSlave::tick() {
  // 1. Drain memory write responses → match by id
  while (auto resp = memory_port_.pop_write_resp()) {
    auto it = active_writes_.find(resp->id);
    if (it == active_writes_.end()) continue;
    auto& st = it->second;
    ++st.beats_completed;
    if (static_cast<uint8_t>(resp->resp) > static_cast<uint8_t>(st.worst_resp))
      st.worst_resp = resp->resp;
    if (st.beats_completed == static_cast<std::size_t>(st.aw.len) + 1) {
      b_q_.push_back(BBeat{st.aw.id, st.worst_resp, 0});
      active_writes_.erase(it);
      // Note: aw_issue_order_.front() was popped when the burst's W beats were
      // fully submitted to memory (step 4), so the W routing for the NEXT
      // queued burst is correct even while this one waits on memory latency.
    }
  }

  // 2. Drain memory read responses → push R beats
  while (auto rresp = memory_port_.pop_read_resp()) {
    auto it = active_reads_.find(rresp->id);
    if (it == active_reads_.end()) continue;
    auto& st = it->second;
    RBeat rb{};
    rb.id = st.ar.id; rb.data = rresp->data; rb.resp = rresp->resp;
    rb.last = (st.beats_returned + 1 == static_cast<std::size_t>(st.ar.len) + 1);
    rb.user = 0;
    r_q_.push_back(rb);
    ++st.beats_returned;
    if (rb.last) active_reads_.erase(it);
  }

  // 3. Start new AW (with burst-atomic OOB pre-check)
  while (!aw_q_.empty()) {
    auto& aw = aw_q_.front();
    if (active_writes_.count(aw.id)) break;  // wait for same-ID to finish
    if (bounds_set_) {
      std::size_t bpb = 1ull << aw.size;
      std::size_t total = bpb * (static_cast<std::size_t>(aw.len) + 1);
      // WRAP confines all beats to [wrap_lower, wrap_upper); check that
      // window rather than the linear [addr, addr+total). FIXED/INCR span
      // the linear range.
      bool oob = false;
      if (aw.burst == Burst::WRAP) {
        const uint64_t wrap_lower =
            aw.addr & ~(static_cast<uint64_t>(total) - 1u);
        const uint64_t wrap_upper = wrap_lower + total;
        oob = (wrap_lower < bounds_base_) ||
              (wrap_upper > bounds_base_ + bounds_size_);
      } else {
        oob = (aw.addr < bounds_base_) ||
              (aw.addr + total > bounds_base_ + bounds_size_);
      }
      if (oob) {
        b_q_.push_back(BBeat{aw.id, Resp::DECERR, 0});
        // Discard the W beats corresponding to this burst
        for (std::size_t i = 0; i < static_cast<std::size_t>(aw.len) + 1; ++i) {
          if (w_q_.empty()) break;
          w_q_.pop_front();
        }
        aw_q_.pop_front();
        continue;
      }
    }
    active_writes_[aw.id] = WriteBurstState{aw, 0, 0, Resp::OKAY};
    aw_issue_order_.push_back(aw.id);
    aw_q_.pop_front();
  }

  // 4. Submit W beats for oldest-issued active write
  while (!w_q_.empty() && !aw_issue_order_.empty()) {
    uint8_t front_id = aw_issue_order_.front();
    auto& st = active_writes_[front_id];
    std::size_t beat_idx = st.beats_submitted;
    MemWriteReq req{};
    req.addr = beat_addr(st.aw.addr, st.aw.len, st.aw.size, st.aw.burst, beat_idx);
    req.data = w_q_.front().data;
    req.strb = w_q_.front().strb;
    req.id   = st.aw.id;
    req.last = w_q_.front().last;
    req.tag  = (static_cast<uint64_t>(front_id) << 32) | beat_idx;
    if (!memory_port_.submit_write(req)) break;  // retry next tick
    ++st.beats_submitted;
    w_q_.pop_front();
    if (st.beats_submitted == static_cast<std::size_t>(st.aw.len) + 1) {
      // Burst's W beats fully forwarded to memory. Free up W routing so the
      // next queued AW (if any) can take ownership of subsequent W beats,
      // even while this burst's B response is still pending in memory.
      aw_issue_order_.pop_front();
      // Continue the loop: a subsequent burst may have W beats already queued.
    }
  }

  // 5. Start new AR (with burst-atomic OOB pre-check)
  while (!ar_q_.empty()) {
    auto& ar = ar_q_.front();
    if (active_reads_.count(ar.id)) break;
    if (bounds_set_) {
      std::size_t bpb = 1ull << ar.size;
      std::size_t total = bpb * (static_cast<std::size_t>(ar.len) + 1);
      bool oob = false;
      if (ar.burst == Burst::WRAP) {
        const uint64_t wrap_lower =
            ar.addr & ~(static_cast<uint64_t>(total) - 1u);
        const uint64_t wrap_upper = wrap_lower + total;
        oob = (wrap_lower < bounds_base_) ||
              (wrap_upper > bounds_base_ + bounds_size_);
      } else {
        oob = (ar.addr < bounds_base_) ||
              (ar.addr + total > bounds_base_ + bounds_size_);
      }
      if (oob) {
        for (uint8_t i = 0; i < ar.len + 1; ++i) {
          RBeat rb{}; rb.id = ar.id;
          rb.data.fill(0); rb.resp = Resp::DECERR;
          rb.last = (i == ar.len); rb.user = 0;
          r_q_.push_back(rb);
        }
        ar_q_.pop_front();
        continue;
      }
    }
    active_reads_[ar.id] = ReadBurstState{ar, 0, 0};
    ar_q_.pop_front();
  }

  // 6. Submit AR beats to memory
  for (auto& [id, st] : active_reads_) {
    while (st.beats_submitted < static_cast<std::size_t>(st.ar.len) + 1) {
      MemReadReq req{};
      req.addr = beat_addr(st.ar.addr, st.ar.len, st.ar.size, st.ar.burst,
                           st.beats_submitted);
      req.size = st.ar.size; req.id = st.ar.id;
      req.last = (st.beats_submitted == static_cast<std::size_t>(st.ar.len));
      req.tag  = (static_cast<uint64_t>(id) << 32) | st.beats_submitted;
      if (!memory_port_.submit_read(req)) break;
      ++st.beats_submitted;
    }
  }
}

}  // namespace ni::cmodel::axi
