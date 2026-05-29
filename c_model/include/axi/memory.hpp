// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/memory_port.hpp"
#include <cstddef>
#include <deque>
#include <vector>

namespace ni::cmodel::axi {

class Memory : public IMemoryPort {
public:
  Memory(uint64_t base_addr, std::size_t size_bytes,
         std::size_t write_latency_ticks, std::size_t read_latency_ticks,
         std::size_t pending_queue_depth = 32,
         uint8_t fill_byte = 0x00)
      : base_(base_addr), size_(size_bytes),
        write_lat_(write_latency_ticks), read_lat_(read_latency_ticks),
        pending_depth_(pending_queue_depth),
        storage_(size_bytes, fill_byte) {}

  bool submit_write(const MemWriteReq& req) override {
    if (pending_writes_.size() >= pending_depth_) return false;
    pending_writes_.push_back({req, write_lat_});
    return true;
  }
  bool submit_read(const MemReadReq& req) override {
    if (pending_reads_.size() >= pending_depth_) return false;
    pending_reads_.push_back({req, read_lat_});
    return true;
  }
  std::optional<MemWriteResp> pop_write_resp() override {
    if (write_resp_q_.empty()) return std::nullopt;
    auto r = write_resp_q_.front(); write_resp_q_.pop_front(); return r;
  }
  std::optional<MemReadResp> pop_read_resp() override {
    if (read_resp_q_.empty()) return std::nullopt;
    auto r = read_resp_q_.front(); read_resp_q_.pop_front(); return r;
  }

  void tick() {
    // Fire on Nth tick (cocotbext-axi convention): N=0 fires on 1st tick, N=5 fires on 5th tick
    for (auto it = pending_writes_.begin(); it != pending_writes_.end(); ) {
      bool fire = false;
      if (it->ticks == 0) {
        fire = true;
      } else {
        --it->ticks;
        if (it->ticks == 0) fire = true;
      }
      if (fire) {
        write_resp_q_.push_back(perform_write_(it->req));
        it = pending_writes_.erase(it);
      } else {
        ++it;
      }
    }
    for (auto it = pending_reads_.begin(); it != pending_reads_.end(); ) {
      bool fire = false;
      if (it->ticks == 0) {
        fire = true;
      } else {
        --it->ticks;
        if (it->ticks == 0) fire = true;
      }
      if (fire) {
        read_resp_q_.push_back(perform_read_(it->req));
        it = pending_reads_.erase(it);
      } else {
        ++it;
      }
    }
  }

  uint8_t peek(uint64_t addr) const {
    return in_bounds_(addr, 1) ? storage_[addr - base_] : 0u;
  }
  std::size_t pending_writes() const { return pending_writes_.size(); }
  std::size_t pending_reads()  const { return pending_reads_.size();  }

private:
  bool in_bounds_(uint64_t addr, std::size_t bytes) const {
    return addr >= base_ && (addr + bytes) <= (base_ + size_);
  }

  MemWriteResp perform_write_(const MemWriteReq& req) {
    std::size_t bytes_per_beat = DATA_BYTES;
    if (!in_bounds_(req.addr, bytes_per_beat)) {
      return MemWriteResp{req.id, Resp::DECERR, req.tag};
    }
    for (std::size_t i = 0; i < bytes_per_beat; ++i) {
      if ((req.strb >> i) & 0x1u) {
        storage_[(req.addr - base_) + i] = req.data[i];
      }
    }
    return MemWriteResp{req.id, Resp::OKAY, req.tag};
  }

  MemReadResp perform_read_(const MemReadReq& req) {
    MemReadResp resp{};
    resp.id = req.id; resp.tag = req.tag; resp.last = req.last;
    std::size_t bytes_per_beat = 1u << req.size;
    if (!in_bounds_(req.addr, bytes_per_beat)) {
      resp.resp = Resp::DECERR; resp.data.fill(0x00);
      return resp;
    }
    resp.resp = Resp::OKAY; resp.data.fill(0x00);
    for (std::size_t i = 0; i < bytes_per_beat; ++i) {
      resp.data[i] = storage_[(req.addr - base_) + i];
    }
    return resp;
  }

  struct PendingWrite { MemWriteReq req; std::size_t ticks; };
  struct PendingRead  { MemReadReq  req; std::size_t ticks; };

  uint64_t base_;
  std::size_t size_;
  std::size_t write_lat_, read_lat_;
  std::size_t pending_depth_;
  std::vector<uint8_t> storage_;
  std::deque<PendingWrite> pending_writes_;
  std::deque<PendingRead>  pending_reads_;
  std::deque<MemWriteResp> write_resp_q_;
  std::deque<MemReadResp>  read_resp_q_;
};

}
