// Algorithms ported from cocotbext-axi (MIT) — see axi/ATTRIBUTION.md
#pragma once
#include "axi/types.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <yaml-cpp/yaml.h>

namespace ni::cmodel::axi {

struct ScenarioConfig {
  uint64_t    memory_base           = 0;
  std::size_t memory_size           = 0x10000;
  std::size_t write_latency         = 1;
  std::size_t read_latency          = 1;
  std::size_t max_outstanding_write = 1;
  std::size_t max_outstanding_read  = 1;
};

struct ScenarioTransaction {
  enum class Op { Write, Read };
  Op       op;
  uint64_t addr;
  uint8_t  id;
  uint8_t  len, size;
  Burst    burst;
  std::string data_file;
  std::string dump_file;
  std::size_t scenario_line;
};

struct Scenario {
  ScenarioConfig config;
  std::vector<ScenarioTransaction> transactions;
};

inline Burst parse_burst(const std::string& s) {
  if (s == "INCR")  return Burst::INCR;
  if (s == "WRAP")  return Burst::WRAP;
  if (s == "FIXED") return Burst::FIXED;
  throw std::runtime_error("scenario: unknown burst '" + s + "' (Phase A only supports INCR)");
}

inline Scenario load_scenario(const std::string& path) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const YAML::Exception& e) {
    throw std::runtime_error("scenario: parse failed for '" + path + "': " + e.what());
  }
  Scenario sc;

  if (root["config"]) {
    auto cfg = root["config"];
    static const std::vector<std::string> known_cfg = {
        "memory_base", "memory_size", "write_latency", "read_latency",
        "max_outstanding_write", "max_outstanding_read"};
    for (auto it = cfg.begin(); it != cfg.end(); ++it) {
      auto key = it->first.as<std::string>();
      bool ok = false;
      for (auto& k : known_cfg) if (k == key) { ok = true; break; }
      if (!ok) throw std::runtime_error("scenario config: unknown field '" + key + "'");
    }
    if (cfg["memory_base"])           sc.config.memory_base           = cfg["memory_base"].as<uint64_t>();
    if (cfg["memory_size"])           sc.config.memory_size           = cfg["memory_size"].as<std::size_t>();
    if (cfg["write_latency"])         sc.config.write_latency         = cfg["write_latency"].as<std::size_t>();
    if (cfg["read_latency"])          sc.config.read_latency          = cfg["read_latency"].as<std::size_t>();
    if (cfg["max_outstanding_write"]) sc.config.max_outstanding_write = cfg["max_outstanding_write"].as<std::size_t>();
    if (cfg["max_outstanding_read"])  sc.config.max_outstanding_read  = cfg["max_outstanding_read"].as<std::size_t>();
  }

  if (!root["transactions"] || !root["transactions"].IsSequence() ||
      root["transactions"].size() == 0) {
    throw std::runtime_error("scenario: 'transactions' must be a non-empty sequence");
  }

  std::size_t line = 0;
  for (const auto& txn : root["transactions"]) {
    ++line;
    ScenarioTransaction t{};
    t.scenario_line = line;
    auto op = txn["op"].as<std::string>();
    if (op == "write")      t.op = ScenarioTransaction::Op::Write;
    else if (op == "read")  t.op = ScenarioTransaction::Op::Read;
    else throw std::runtime_error("scenario txn " + std::to_string(line) +
                                  ": unknown op '" + op + "'");
    t.addr  = txn["addr"].as<uint64_t>();
    t.id    = txn["id"].as<uint8_t>();
    t.len   = txn["len"].as<uint8_t>();
    t.size  = txn["size"].as<uint8_t>();
    if (t.size > 5) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": size must be <= 5 (Phase A max beat = 32 bytes)");
    }
    t.burst = parse_burst(txn["burst"].as<std::string>());
    if (t.burst != Burst::INCR) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": Phase A only supports INCR burst");
    }
    if ((t.addr & ((1ull << t.size) - 1)) != 0) {
      throw std::runtime_error("scenario txn " + std::to_string(line) +
                               ": addr must be aligned to (1<<size) in Phase A");
    }
    if (t.op == ScenarioTransaction::Op::Write) t.data_file = txn["data_file"].as<std::string>();
    if (t.op == ScenarioTransaction::Op::Read)  t.dump_file = txn["dump_file"].as<std::string>();
    sc.transactions.push_back(t);
  }
  return sc;
}

}
