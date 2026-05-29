// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "ni_signals.h"
#include "ni_flit_constants.h"
#include <array>
#include <cstdint>

namespace ni::cmodel::axi {

constexpr int DATA_BYTES = ni::WSTRB_WIDTH;
constexpr int DATA_WIDTH = DATA_BYTES * 8;

static_assert(DATA_BYTES * 8 == ni::width::NOC_DATA_WIDTH,
              "DATA_BYTES (= WSTRB_WIDTH) * 8 must equal NOC_DATA_WIDTH "
              "for byte-level WSTRB semantics");

enum class Burst : uint8_t { FIXED = 0, INCR = 1, WRAP = 2 };
enum class Resp  : uint8_t { OKAY  = 0, EXOKAY = 1, SLVERR = 2, DECERR = 3 };

struct AwBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size;
  Burst    burst;
  uint8_t  cache, lock, prot, region, user, qos;
};

struct WBeat {
  std::array<uint8_t, DATA_BYTES> data;
  uint32_t strb;
  bool     last;
  uint8_t  user;
};

struct ArBeat {
  uint8_t  id;
  uint64_t addr;
  uint8_t  len, size;
  Burst    burst;
  uint8_t  cache, lock, prot, region, user, qos;
};

struct BBeat {
  uint8_t id;
  Resp    resp;
  uint8_t user;
};

struct RBeat {
  uint8_t  id;
  std::array<uint8_t, DATA_BYTES> data;
  Resp     resp;
  bool     last;
  uint8_t  user;
};

}
