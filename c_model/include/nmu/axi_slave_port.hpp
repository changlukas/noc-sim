#pragma once
#include "ni_signals.h"         // ni::pins::AxiSlavePortPins
#include "ni_flit_constants.h"  // ni::width::AXI_*_WIDTH, NOC_DATA_WIDTH, WSTRB_WIDTH
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

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

}  // namespace ni::cmodel::nmu
