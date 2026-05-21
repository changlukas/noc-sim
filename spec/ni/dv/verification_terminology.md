# Verification Terminology and OSS Precedents

本檔收 `test_environment.md` 用到的標準術語與 OSS 先例對映，與主文件分開，讓主文件保持乾淨。

## 標準術語

本文件使用下列常見 DV 術語：`co-simulation`、`lockstep / step-and-compare`、`differential testing`、`directed / synthetic microbenchmark validation`、`trace-based validation`、`calibration / correlation`、`golden / reference model checking`、`regression`。

**Verification vs Validation**：Verification 是符合 spec，Validation 是符合現實/RTL（perf 對得上）。本方法論兩者都做。

## OSS 先例（按軸對映）

本設計是 hybrid（cycle-accurate NoC perf model 又當 RTL reference），無單一 OSS 全等。

| 軸 | 先例 | 借用 | 差異 |
|---|---|---|---|
| 功能 reference + RTL co-sim 結構 | **Ibex / Spike lockstep** | reference 與 RTL 在明確同步點（transaction 完成 / 介面事件，非每內部 cycle）differential | Spike 是功能 ISS、無 timing |
| reference 自身取信（無外部 golden）| **Spike 取信法** | 靠 spec（`protocol_rules`）衍生 ABV + analytic oracle + frozen vectors | DRAMSim 有 vendor golden，本設計沒有，更靠 spec-derived |
| timing / perf fidelity | **gem5 Garnet** | synthetic traffic + latency-throughput curve + correlation | Garnet 不做 RTL-reference co-sim、不模 AXI |
| 介面 per-cycle 協定合法性（AXI handshake / credit / VC）| **DRAMSim / Ramulator trace-to-RTL** | C model 產 trace，replay 過 RTL 檢查介面合法 | DRAMSim 信 RTL 驗 C（方向相反）|

本設計概括：以 spec-derived oracle/ABV 取信的 cycle-accurate NoC C++ reference model，與 RTL 做 differential co-simulation。功能在 transaction 與介面同步點 cycle-exact 比對，bulk timing 用 synthetic-traffic latency-throughput correlation 校準。

OSS 對 cycle accuracy 的覆蓋：多數（gem5、Garnet、BookSim、Noxim）不追全系統 cycle-exact，只驗 latency-throughput fidelity。僅在介面協定逐 cycle contract（AXI handshake、credit/VC）才 cycle-exact，bulk 用 correlation。

## 驗證資源 OSS（功能正確性來源）

| 角色 | OSS | 語言 / License | 可信度 |
|---|---|---|---|
| AXI master | pulp `axi_test::axi_rand_master` / `axi_file_master`，cocotbext-axi `AxiMaster` | SV / SHL-0.51，Python / MIT | FlooNoC 已用、PULP 專案有多次 silicon 使用紀錄 |
| AXI protocol checker | **YosysHQ-GmbH/SVA-AXI4-FVIP**，ZipCPU `wb2axip` faxi，verilaxi | SVA / ISC、Apache | assertion-based，對照 IHI0022E |
| AXI slave | pulp `axi_rand_slave`，cocotbext `AxiSlave` | SV，Python | 同上 |
| memory / DDR | 功能：pulp `axi_sim_mem` / cocotbext `AxiRam`，DDR 時序：DRAMSim3 / Ramulator | SV / Python，C++ | DRAMSim3 trace→vendor-model 自驗 |
| flit driver/monitor | FlooNoC `tb_floo_router` / `floo_axi_test_node`，BookSim/Garnet（pattern 靈感）| SV，C++ | FlooNoC 本機已有 |
| C/RTL 共用 stimulus | cocotbext-axi（Python 一份驅兩 DUT），Xilinx libsystemctlm-soc（SystemC TLM AXI + tlm2axi bridge）| Python / MIT，C++ / Apache | co-sim 廣用 |

**OSS reuse 注意事項**：
- FlooNoC `gen_jobs.py`：fork/template（script 常數、無 injection_rate、不產 mem init）。
- 讀 job 的是 `floo_dma_test_node`（iDMA flow）非 `floo_axi_test_node`（random）。
- OpenTitan `secded_gen.py`：限 `k≤120` < whole-flit ~396-bit，需 fork。
- SVA-AXI4-FVIP / verilaxi 在 Verilator 下 SVA 支援有限，VCS 較完整。

## 使用範圍與限制

OSS 可提供 AXI master stimulus、AXI 協定合法性檢查（SVA）、AXI slave/memory/DDR model、NoC traffic pattern 參考。OSS 不提供客製 NMU 的 flit-encoding golden。客製 flit golden 可用 NMU+NSU loopback 的 AXI-in == AXI-out 比對（FlooNoC `axi_reorder_compare` 法）降低依賴，或由 spec 推導 / C model（取信後）/ FlooNoC chimney 共通子集產生。

## 必讀

Ibex cosim docs（co-sim 結構，硬體 DV 最易上手）→ gem5 Garnet（NoC timing 驗證）→ DRAMSim2 paper（trace-to-RTL 介面技術）。

## Sources

- pulp-platform/axi（`axi_test.sv`）、YosysHQ-GmbH/SVA-AXI4-FVIP、alexforencich/cocotbext-axi、Xilinx/libsystemctlm-soc、FlooNoC、DRAMSim3 / Ramulator、gem5 Garnet、BookSim2、OpenTitan secded_gen / dvsim。
