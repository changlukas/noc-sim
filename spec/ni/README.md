# Network Interface (ni)

## Overview

This document specifies the Network Interface (`ni`) hardware IP for the noc-sim project. The `ni` block bridges an AXI4 master/subordinate pair to the NoC packet substrate (Req/Rsp dual physical link), performing protocol conversion in both directions: AXI requests are packed into flits and injected into the request network; flits arriving from the response network are unpacked back into AXI responses. Each `ni` instance contains two independently-enableable units, **NMU** (Network Master Unit, AXI-slave-side; originates NoC transactions) and **NSU** (Network Slave Unit, AXI-master-side; terminates NoC transactions). The naming convention follows AMD Versal style.
<!-- source: 04_network_interface.md §1.1, §1.3 -->

`ni` connects to a Router's LOCAL port and uses the **same port interface** as Router-to-Router links — `valid`/`ready`/`flit` triplets on Req and Rsp networks.
<!-- source: 04_network_interface.md §1.2 -->

## Features

- AXI4 full protocol conversion, all five channels (AW/W/AR/B/R).
- Configurable NMU-only / NSU-only / full instantiation via `NI_CFG.EN_MGR_PORT` and `NI_CFG.EN_SBR_PORT`.
- Reorder Buffer (RoB) with three policies (`NormalRoB`, `SimpleRoB`, `NoRoB`) selectable per response channel; preserves AXI per-ID ordering.
- End-to-end SECDED ECC over `wdata` and `rdata`. Source NI generates; destination NI checks. Routers are pass-through.
- QoS Generator with four modes: `Bypass`, `Fixed`, `Limiter`, `Regulator`.
- Address translation supports XY-routing direct decoding (`XY_ADDR_OFFSET_X` / `XY_ADDR_OFFSET_Y`) or System Address Map (SAM) lookup (`USE_ID_TABLE=1`).
- Optional spill registers on AW/AR (`CUT_AX`) and response (`CUT_RSP`) paths to break timing.
- Performance probes (Packet Probe and Transaction Probe) report bandwidth and latency histograms via CSR.
<!-- source: 04_network_interface.md §1.4, §3.1-§3.5; 06_qos.md §3 -->

## Description

`ni` sits between a single AXI4 master/subordinate pair and a single Router LOCAL port. On the AXI side, the **manager port** (`axi_in_*`) accepts AW/W/AR transactions from a connected AXI master and returns B/R; the **subordinate port** (`axi_out_*`) drives an attached AXI slave (typically a local memory) with AW/W/AR and absorbs B/R. On the NoC side, two independent links, `noc_req` and `noc_rsp`, carry flits to/from the connected Router; both links use a `valid`/`ready`/`flit` handshake identical to Router-to-Router links.

Internally, NMU's pipeline is: AddrTrans → QoSGen → FlitPack (AW/W/AR) → ECC Gen (W) → InjectionBuffer → `noc_req_o` for outgoing requests; and `noc_rsp_i` → ECC Check (R) → FlitUnpack → RoB → AXI B/R for returning responses. NSU's pipeline mirrors the NMU but in the opposite role: `noc_req_i` → FlitUnpack → ReqInfoStore + W Reassembly → ECC Check (W) → AXI AW/W/AR to local memory; and AXI B/R from local memory → ECC Gen (R) → FlitPack → `noc_rsp_o`.

A single `ni` is paired with a single Router; multiple `ni` instances on the same Router are distinguished by `port_id` (2 bits, 4 possible values) carried in flit headers.
<!-- source: 04_network_interface.md §2; 02_flit.md §2.2.4 -->

## Compatibility

`ni` follows the AMD Versal NoC NMU/NSU split for naming. Sub-field widths (address, data, ID, user) follow the AXI4 specification. Pin-level integration is project-specific; downstream consumers must observe the configurable parameters in `interfaces.md`.

`TODO(designer):` Compatibility table with specific industry register sets (e.g., AMBA NIC-400, Versal NoC NMU) is **not** claimed by the source documents. Either confirm and add concrete cross-references or drop this section.
<!-- source: 04_network_interface.md §1.3 (Versal naming only); compatibility claim not in source -->

## Further Reading

- [Theory of Operation](./doc/theory_of_operation.md)
- [Programmer's Guide](./doc/programmers_guide.md)
- [Hardware Interfaces](./doc/interfaces.md)
- [Registers](./doc/registers.md)
- [Design Verification Plan](./dv/plan.md)
- [Import Report](./IMPORT_REPORT.md)
