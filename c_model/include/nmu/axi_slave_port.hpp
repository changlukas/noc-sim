#pragma once
#include "ni_signals.h"         // ni::pins::AxiSlavePortPins
#include "ni_flit_constants.h"  // ni::width::AXI_*_WIDTH, NOC_DATA_WIDTH, WSTRB_WIDTH
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

// Drift-safe invariant: WBeat::data is sized via WSTRB_WIDTH (one byte per
// strobe lane). That sizing only matches the AXI data bus if NOC_DATA_WIDTH is
// exactly WSTRB_WIDTH bytes wide.
static_assert(ni::WSTRB_WIDTH * 8 == ni::width::NOC_DATA_WIDTH,
              "WBeat::data sized via WSTRB_WIDTH assumes NOC_DATA_WIDTH/8 byte lanes");

namespace ni::cmodel::nmu {

struct AwBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size, burst, cache, lock, prot, region, user, qos;
};

struct ArBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size, burst, cache, lock, prot, region, user, qos;
};

struct WBeat {
  std::array<uint8_t, ni::WSTRB_WIDTH> data;
  uint32_t strb;
  uint8_t  last, user;
};

struct BBeat { uint8_t id, resp, user; };

struct RBeat {
  uint8_t  id;
  std::array<uint8_t, ni::WSTRB_WIDTH> data;
  uint8_t  resp, last, user;
};

enum class ChannelMask : uint8_t {
  None = 0,
  Aw   = 1u << 0,
  W    = 1u << 1,
  Ar   = 1u << 2,
  B    = 1u << 3,
  R    = 1u << 4,
};

constexpr ChannelMask operator|(ChannelMask a, ChannelMask b) {
  return static_cast<ChannelMask>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
constexpr ChannelMask operator&(ChannelMask a, ChannelMask b) {
  return static_cast<ChannelMask>(static_cast<uint8_t>(a) & static_cast<uint8_t>(b));
}
constexpr bool any(ChannelMask m) { return static_cast<uint8_t>(m) != 0; }

struct QueueDepths {
  std::size_t aw = 16;
  std::size_t w  = 16;
  std::size_t ar = 16;
  std::size_t b  = 16;
  std::size_t r  = 16;
};

// NOTE: assert messages remain inline string literals (not named constants).
// The standard `assert(cond && "msg")` macro stringifies the expression and
// emits *that* on failure — replacing the literal with a `constexpr const char*`
// makes the assert print the variable name instead of the message, which
// breaks every EXPECT_DEATH regex. Keep the literals; tests own matching
// regexes in the .cpp. (Stage 3 reviewer finding #2 deferred.)
class AxiSlavePort {
public:
  explicit AxiSlavePort(QueueDepths depths = {}) : depths_(depths) {}

  // External interface
  bool push_inbound_pins(const ni::pins::AxiSlavePortPins& p, ChannelMask mask);
  std::optional<BBeat> pop_outbound_b();
  std::optional<RBeat> pop_outbound_r();

  // Internal interface
  std::optional<AwBeat> pop_aw();
  std::optional<WBeat>  pop_w();
  std::optional<ArBeat> pop_ar();
  bool push_b(const BBeat&);
  bool push_r(const RBeat&);

  // Lifecycle / observation
  void tick() {}
  std::size_t aw_q_size() const { return aw_q_.size(); }
  std::size_t w_q_size()  const { return w_q_.size();  }
  std::size_t ar_q_size() const { return ar_q_.size(); }
  std::size_t b_q_size()  const { return b_q_.size();  }
  std::size_t r_q_size()  const { return r_q_.size();  }
  std::size_t aw_q_capacity() const { return depths_.aw; }
  std::size_t w_q_capacity()  const { return depths_.w;  }
  std::size_t ar_q_capacity() const { return depths_.ar; }
  std::size_t b_q_capacity()  const { return depths_.b;  }
  std::size_t r_q_capacity() const { return depths_.r;  }

private:
  // Stateless AXI4 protocol field checks (no cross-beat tracking).
  // SIZE_BOUND uses max beat = 32 B (1<<5), matching NOC_DATA_WIDTH=256b lanes.
  // 4 KB constant per AXI4 spec: no INCR burst may cross a 4 KB boundary.
  static constexpr uint8_t kBurstFixed = 0;
  static constexpr uint8_t kBurstIncr  = 1;
  static constexpr uint8_t kBurstWrap  = 2;

  static bool check_addr_beat_(uint8_t burst, uint64_t addr, uint8_t len, uint8_t size) {
    if (burst > 2)                              return false;  // BURST_ENCODING
    if (size > 5)                               return false;  // SIZE_BOUND (max 32 B)
    if (burst == kBurstWrap && len > 15)        return false;  // LEN_ENCODING for WRAP
    if (burst == kBurstWrap && (addr & ((1ull << size) - 1))) return false;  // WRAP_ALIGN
    if (burst == kBurstIncr) {
      uint64_t bytes_per_beat = 1ull << size;
      uint64_t total = bytes_per_beat * (static_cast<uint64_t>(len) + 1);
      if ((addr & 0xFFF) + total > 0x1000) return false;  // 4KB cross
    }
    return true;
  }
  static bool check_strb_(uint32_t strb) {
    // Guard against UB when WSTRB_WIDTH == 32 (1u << 32 is undefined).
    constexpr uint32_t kMaxStrb = (ni::WSTRB_WIDTH >= 32) ? 0xFFFF'FFFFu : ((1u << ni::WSTRB_WIDTH) - 1);
    return (strb & ~kMaxStrb) == 0;
  }
  static bool check_resp_(uint8_t resp) { return resp < 4; }

  std::deque<AwBeat> aw_q_;
  std::deque<WBeat>  w_q_;
  std::deque<ArBeat> ar_q_;
  std::deque<BBeat>  b_q_;
  std::deque<RBeat>  r_q_;
  QueueDepths        depths_;
};

inline bool AxiSlavePort::push_inbound_pins(const ni::pins::AxiSlavePortPins& p, ChannelMask mask) {
  ChannelMask in_only = mask & (ChannelMask::Aw | ChannelMask::W | ChannelMask::Ar);
  if (!any(in_only)) return false;

  // All-or-nothing capacity check
  if (any(in_only & ChannelMask::Aw) && aw_q_.size() >= depths_.aw) return false;
  if (any(in_only & ChannelMask::W)  && w_q_.size()  >= depths_.w)  return false;
  if (any(in_only & ChannelMask::Ar) && ar_q_.size() >= depths_.ar) return false;

  if (any(in_only & ChannelMask::Aw)) {
    assert(check_addr_beat_(p.axi_awburst_i, p.axi_awaddr_i, p.axi_awlen_i, p.axi_awsize_i)
           && "AXI4_AW_* protocol violation");
    aw_q_.push_back(AwBeat{p.axi_awid_i, p.axi_awaddr_i, p.axi_awlen_i,
                            p.axi_awsize_i, p.axi_awburst_i, p.axi_awcache_i,
                            p.axi_awlock_i, p.axi_awprot_i, p.axi_awregion_i,
                            p.axi_awuser_i, p.axi_awqos_i});
  }
  if (any(in_only & ChannelMask::W)) {
    assert(check_strb_(p.axi_wstrb_i) && "AXI4_W_STRB_VALID violation");
    WBeat wb;
    wb.data = p.axi_wdata_i;
    wb.strb = p.axi_wstrb_i;
    wb.last = p.axi_wlast_i;
    wb.user = p.axi_wuser_i;
    w_q_.push_back(wb);
  }
  if (any(in_only & ChannelMask::Ar)) {
    assert(check_addr_beat_(p.axi_arburst_i, p.axi_araddr_i, p.axi_arlen_i, p.axi_arsize_i)
           && "AXI4_AR_* protocol violation");
    ar_q_.push_back(ArBeat{p.axi_arid_i, p.axi_araddr_i, p.axi_arlen_i,
                             p.axi_arsize_i, p.axi_arburst_i, p.axi_arcache_i,
                             p.axi_arlock_i, p.axi_arprot_i, p.axi_arregion_i,
                             p.axi_aruser_i, p.axi_arqos_i});
  }
  return true;
}

inline std::optional<AwBeat> AxiSlavePort::pop_aw() {
  if (aw_q_.empty()) return std::nullopt;
  AwBeat front = aw_q_.front();
  aw_q_.pop_front();
  return front;
}

inline std::optional<WBeat>  AxiSlavePort::pop_w()  { if (w_q_.empty())  return std::nullopt; auto f = w_q_.front();  w_q_.pop_front();  return f; }
inline std::optional<ArBeat> AxiSlavePort::pop_ar() { if (ar_q_.empty()) return std::nullopt; auto f = ar_q_.front(); ar_q_.pop_front(); return f; }
inline std::optional<BBeat>  AxiSlavePort::pop_outbound_b() { if (b_q_.empty()) return std::nullopt; auto f = b_q_.front(); b_q_.pop_front(); return f; }
inline std::optional<RBeat>  AxiSlavePort::pop_outbound_r() { if (r_q_.empty()) return std::nullopt; auto f = r_q_.front(); r_q_.pop_front(); return f; }
inline bool AxiSlavePort::push_b(const BBeat& b) {
  assert(check_resp_(b.resp) && "AXI4_B_RESP_ENCODING violation");
  if (b_q_.size() >= depths_.b) return false;
  b_q_.push_back(b);
  return true;
}
inline bool AxiSlavePort::push_r(const RBeat& r) {
  assert(check_resp_(r.resp) && "AXI4_R_RESP_ENCODING violation");
  if (r_q_.size() >= depths_.r) return false;
  r_q_.push_back(r);
  return true;
}

}  // namespace ni::cmodel::nmu
