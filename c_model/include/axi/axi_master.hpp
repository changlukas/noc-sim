// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include "axi/scenario_parser.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ni::cmodel::axi {

// WriteResult / ReadResult carry the ORIGINAL user txn.addr (the address the
// scenario asked the master to access), plus enough AXI4 burst geometry to let
// the scoreboard re-derive per-beat lane offsets under lane-positioned bus
// semantics. Per-beat byte_lane = (txn.addr + beat*(1<<size)) mod DATA_BYTES;
// the AW.addr on the wire is still aligned DOWN by the master.
struct WriteResult {
  uint64_t addr;                       // original user txn.addr
  uint8_t  size;                       // log2(bytes_per_beat)
  uint8_t  len;                        // beats - 1
  Burst    burst;
  std::vector<uint8_t> data;           // packed user bytes, (len+1)*(1<<size)
  std::vector<uint32_t> strb_per_beat; // bus-level WSTRB per beat (lane-positioned)
  Resp resp;
  uint8_t id;
  std::size_t scenario_line;
};

struct ReadResult {
  uint64_t addr;                       // original user txn.addr
  uint8_t  size;
  uint8_t  len;
  Burst    burst;
  std::vector<uint8_t> data;           // packed user bytes, (len+1)*(1<<size)
  Resp resp;
  uint8_t id;
  std::size_t scenario_line;
};

template<typename SlaveT>
class AxiMasterT {
public:
  AxiMasterT(const std::string& scenario_yaml,
             SlaveT& slave,
             const std::string& read_dump_path,
             std::size_t max_outstanding_write = 1,
             std::size_t max_outstanding_read  = 1)
      : slave_(slave),
        max_out_w_(max_outstanding_write),
        max_out_r_(max_outstanding_read) {
    sc_ = load_scenario(scenario_yaml);
    read_dump_.open(read_dump_path);
    if (!read_dump_.is_open())
      throw std::runtime_error("AxiMaster: cannot open read_dump_path: " + read_dump_path);
  }

  void tick() {
    // Drain B responses
    while (auto b = slave_.pop_b()) {
      auto it = active_writes_.find(b->id);
      if (it == active_writes_.end()) continue;
      // WriteResult carries the ORIGINAL user txn.addr plus AXI4 burst
      // geometry. The scoreboard reconstructs per-beat addr + byte_lane from
      // (addr, size, len, burst). AW.addr on the wire stays aligned-down.
      if (wcb_) wcb_(WriteResult{it->second.txn.addr,
                                  it->second.txn.size,
                                  it->second.txn.len,
                                  it->second.txn.burst,
                                  it->second.data,
                                  it->second.strb_per_beat,
                                  b->resp,
                                  b->id,
                                  it->second.txn.scenario_line});
      active_writes_.erase(it);
    }
    // Drain R responses
    while (auto r = slave_.pop_r()) {
      auto it = active_reads_.find(r->id);
      if (it == active_reads_.end()) continue;
      auto& rs = it->second;
      const std::size_t bpb = 1ull << rs.txn.size;
      // Lane-positioned bus: byte j on the bus is at lane (byte_lane + j),
      // where byte_lane = (per-beat addr) mod DATA_BYTES. For INCR the per-beat
      // addr advances by bpb; for FIXED it stays at txn.addr. Lane room caps
      // the per-beat payload at DATA_BYTES - byte_lane; any trailing user
      // bytes that would have fallen off the bus are zero-padded so downstream
      // packed-buffer offsets stay aligned at (beat * bpb).
      uint64_t beat_addr = rs.txn.addr + rs.beats_observed * bpb;
      if (rs.txn.burst == Burst::FIXED) beat_addr = rs.txn.addr;
      const std::size_t byte_lane =
          static_cast<std::size_t>(beat_addr & (DATA_BYTES - 1));
      const std::size_t lane_room =
          (byte_lane < DATA_BYTES) ? (DATA_BYTES - byte_lane) : 0;
      const std::size_t copy_bytes = std::min(bpb, lane_room);
      for (std::size_t i = 0; i < copy_bytes; ++i)
        rs.accumulator.push_back(r->data[byte_lane + i]);
      for (std::size_t i = copy_bytes; i < bpb; ++i)
        rs.accumulator.push_back(0);
      ++rs.beats_observed;
      if (r->last) {
        // Read dump records the packed user bytes (bpb per beat). For aligned
        // size=5 this matches the historical full-beat dump format.
        const std::size_t total = rs.accumulator.size();
        const std::size_t lines = (bpb > 0) ? (total / bpb) : 0;
        for (std::size_t line = 0; line < lines; ++line) {
          for (std::size_t j = 0; j < bpb; ++j) {
            if (j > 0) read_dump_ << ' ';
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%02X",
                          rs.accumulator[line * bpb + j]);
            read_dump_ << buf;
          }
          read_dump_ << '\n';
        }
        read_dump_.flush();
        if (rcb_) rcb_(ReadResult{rs.txn.addr,
                                    rs.txn.size,
                                    rs.txn.len,
                                    rs.txn.burst,
                                    rs.accumulator,
                                    r->resp, r->id, rs.txn.scenario_line});
        active_reads_.erase(it);
      }
    }
    // Admission: start next transaction if room
    while (next_txn_idx_ < sc_.transactions.size()) {
      const auto& txn = sc_.transactions[next_txn_idx_];
      if (txn.op == ScenarioTransaction::Op::Write) {
        if (active_writes_.size() >= max_out_w_) break;
        if (active_writes_.count(txn.id)) break;
        WriteState ws;
        ws.txn = txn;
        ws.data = load_write_data_(txn.data_file,
                                    static_cast<std::size_t>(txn.len + 1u) * (1ull << txn.size));
        ws.strb_per_beat = load_strb_file_(txn.strb_file,
                                            static_cast<std::size_t>(txn.len + 1u));
        active_writes_.emplace(txn.id, std::move(ws));
      } else {
        if (active_reads_.size() >= max_out_r_) break;
        if (active_reads_.count(txn.id)) break;
        ReadState rs;
        rs.txn = txn;
        active_reads_.emplace(txn.id, std::move(rs));
      }
      ++next_txn_idx_;
    }
    // Push AW + W beats for active writes
    for (auto& [id, ws] : active_writes_) {
      // AXI4 INCR unaligned start (IHI 0022, AMBA AXI Protocol Specification):
      //   AW.addr is aligned DOWN to the (1<<size) transfer boundary; the
      //   unaligned-prefix bytes are skipped via WSTRB on the first beat.
      //   The byte lane carrying the first valid byte is determined by
      //   addr mod DATA_BYTES (the bus byte-lane convention), not (1<<size).
      // Two-alignment scheme: aligned_addr uses (1<<size) for AW.addr (transfer-
      // size alignment); byte_lane below uses DATA_BYTES (bus-lane alignment).
      // For Phase A size=5, both coincide; for B-3b narrow they diverge.
      const uint64_t aligned_addr = ws.txn.addr & ~((1ull << ws.txn.size) - 1);
      if (ws.aw_pushed_ == 0) {
        AwBeat aw{};
        aw.id = id; aw.addr = aligned_addr; aw.len = ws.txn.len; aw.size = ws.txn.size;
        aw.burst = ws.txn.burst;
        if (!slave_.push_aw(aw)) continue;
        ws.aw_pushed_ = 1;
      }
      while (ws.w_pushed_ <= ws.txn.len) {
        WBeat w{};
        const std::size_t bpb = 1ull << ws.txn.size;
        // Lane-positioned bus: byte j of the user payload for this beat is
        // placed at bus lane (byte_lane + j), where byte_lane is derived from
        // the per-beat address. For aligned size=5 Phase A, byte_lane=0 and
        // this collapses to the historical compact placement.
        uint64_t beat_addr = ws.txn.addr + ws.w_pushed_ * bpb;
        if (ws.txn.burst == Burst::FIXED) beat_addr = ws.txn.addr;
        const std::size_t byte_lane =
            static_cast<std::size_t>(beat_addr & (DATA_BYTES - 1));
        w.data.fill(0);
        // ws.data is packed user bytes: beat b contributes bytes
        // [b*bpb, (b+1)*bpb). They land on bus lanes [byte_lane, byte_lane+bpb)
        // but never past DATA_BYTES (excess trails are discarded).
        const std::size_t writable = (byte_lane < DATA_BYTES)
            ? std::min<std::size_t>(bpb, DATA_BYTES - byte_lane)
            : 0;
        for (std::size_t j = 0; j < writable; ++j) {
          const std::size_t off = ws.w_pushed_ * bpb + j;
          w.data[byte_lane + j] = (off < ws.data.size()) ? ws.data[off] : 0;
        }
        // Lane mask: bus lanes [byte_lane, byte_lane + bpb) are enabled. For
        // size=5 (bpb=32) the shift width matches uint32 — `((1ull<<32)-1)<<0`
        // equals 0xFFFFFFFF after uint32 truncation, so the formula collapses
        // to the historical pass-through. For size<5 narrow (aligned OR
        // unaligned first beat), this is the correct AXI4 lane-positioned
        // semantic: only lanes serving the current beat are active.
        const uint32_t lane_mask =
            static_cast<uint32_t>(((1ull << bpb) - 1) << byte_lane);
        w.strb = ws.strb_per_beat[ws.w_pushed_] & lane_mask;
        w.last = (ws.w_pushed_ == ws.txn.len);
        if (!slave_.push_w(w)) break;
        ++ws.w_pushed_;
      }
    }
    // Push AR for active reads
    for (auto& [id, rs] : active_reads_) {
      if (rs.ar_pushed_ == 0) {
        ArBeat ar{};
        ar.id = id;
        // Align AR addr DOWN to (1<<size) boundary, symmetric with AW.
        ar.addr = rs.txn.addr & ~((1ull << rs.txn.size) - 1);
        ar.len = rs.txn.len; ar.size = rs.txn.size;
        ar.burst = rs.txn.burst;
        if (!slave_.push_ar(ar)) continue;
        rs.ar_pushed_ = 1;
      }
    }
  }

  bool done() const {
    return next_txn_idx_ >= sc_.transactions.size()
        && active_writes_.empty() && active_reads_.empty();
  }

  void on_write_completed(std::function<void(const WriteResult&)> cb) { wcb_ = std::move(cb); }
  void on_read_observed  (std::function<void(const ReadResult&)>  cb) { rcb_ = std::move(cb); }

private:
  static std::vector<uint8_t> load_write_data_(const std::string& path, std::size_t expected_bytes) {
    std::ifstream f(path);
    if (!f.is_open())
      throw std::runtime_error("AxiMaster: cannot open data_file: " + path);
    std::vector<uint8_t> bytes;
    std::string tok;
    while (f >> tok) bytes.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
    if (bytes.size() < expected_bytes)
      throw std::runtime_error("AxiMaster: data_file too short (" + std::to_string(bytes.size())
                               + " < " + std::to_string(expected_bytes) + "): " + path);
    return bytes;
  }

  static std::vector<uint32_t> load_strb_file_(const std::string& path,
                                                std::size_t expected_beats,
                                                uint32_t default_full = 0xFFFF'FFFFu) {
    if (path.empty()) {
      return std::vector<uint32_t>(expected_beats, default_full);
    }
    std::ifstream f(path);
    if (!f.is_open())
      throw std::runtime_error("AxiMaster: cannot open strb_file: " + path);
    std::vector<uint32_t> strbs;
    std::string tok;
    while (f >> tok) {
      unsigned long long v = std::stoull(tok, nullptr, 16);
      if (v > 0xFFFFFFFFull)
        throw std::runtime_error("AxiMaster: strb_file token out of uint32_t range: " + tok);
      strbs.push_back(static_cast<uint32_t>(v));
    }
    if (strbs.size() != expected_beats)
      throw std::runtime_error("AxiMaster: strb_file line count " + std::to_string(strbs.size())
                                + " != expected beats " + std::to_string(expected_beats)
                                + ": " + path);
    return strbs;
  }

  struct WriteState {
    ScenarioTransaction txn;
    std::vector<uint8_t> data;
    std::vector<uint32_t> strb_per_beat;  // size = txn.len + 1
    std::size_t aw_pushed_ = 0;
    std::size_t w_pushed_  = 0;
  };
  struct ReadState {
    ScenarioTransaction txn;
    std::size_t ar_pushed_ = 0;
    std::vector<uint8_t> accumulator;
    std::size_t beats_observed = 0;
  };

  Scenario   sc_;
  SlaveT&    slave_;
  std::size_t max_out_w_, max_out_r_;
  std::size_t next_txn_idx_ = 0;
  std::ofstream read_dump_;
  std::function<void(const WriteResult&)> wcb_;
  std::function<void(const ReadResult&)>  rcb_;
  std::map<uint8_t, WriteState> active_writes_;
  std::map<uint8_t, ReadState>  active_reads_;
};

class AxiSlave;
using AxiMaster = AxiMasterT<AxiSlave>;

}
