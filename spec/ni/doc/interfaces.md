# Hardware Interfaces

`TODO(designer):` Source documents describe ports as **typedef-typed bundles** (`axi_in_req_t`, `noc_req_t`, etc.) rather than enumerated signals. The signal-level table below is therefore presented at the **bundle granularity** that the source actually documents. Before D1 you must either (a) decompose each bundle to its constituent signals (per AXI4 spec for `axi_*_t`, per `02_flit.md` §4 for `noc_*_t`) or (b) state explicitly that the integration contract is at the bundle/typedef level and refer to the typedef's defining package.

Naming convention: `_i` = input, `_o` = output. `_n` infix denotes active-low. Internal-only `_q` (registered) and `_d` (next-state) signals are not part of this interface.
<!-- source: 04_network_interface.md §4 -->

## Parameters

### AXI Configuration (`AXI_CFG`)

| Name | Type | Default | Constraint | Description |
|---|---|---|---|---|
| `ADDR_WIDTH` | int | 64 | `TODO(designer): range` | AXI address width (bits). |
| `DATA_WIDTH` | int | 256 | `TODO(designer): must be a multiple of ECC_GRANULE_WIDTH (=64)` | AXI data width (bits, = 32 bytes). |
| `USER_WIDTH` | int | 8 | `TODO(designer): range` | AXI user signal width. |
| `IN_ID_WIDTH` | int | 8 | `TODO(designer): max as a function of MAX_TXNS / MAX_UNIQUE_IDS` | Incoming AXI master transaction ID width. |
| `OUT_ID_WIDTH` | int | 8 | `TODO(designer): range` | Outgoing AXI subordinate transaction ID width. |

### NI Configuration (`NI_CFG`)

| Name | Type | Default | Constraint | Description |
|---|---|---|---|---|
| `EN_SBR_PORT` | bool | `true` | — | Enable NSU (AXI subordinate side). |
| `EN_MGR_PORT` | bool | `true` | — | Enable NMU (AXI manager side). At least one of `EN_SBR_PORT` / `EN_MGR_PORT` must be `true` (`TODO(designer): confirm`). |
| `MAX_TXNS` | int | 32 | ≤ `2^ROB_IDX_WIDTH` | Maximum outstanding transactions across all IDs. |
| `MAX_UNIQUE_IDS` | int | 1 | ≤ `2^IN_ID_WIDTH` | Number of unique transaction IDs `ni` issues downstream. |
| `MAX_TXNS_PER_ID` | int | 32 | ≤ `MAX_TXNS` | Maximum outstanding transactions per AXI ID. |
| `B_ROB_TYPE` | enum `rob_type_e` | `NoRoB` | `{NormalRoB, SimpleRoB, NoRoB}` | RoB policy for B responses. |
| `B_ROB_SIZE` | int | 0 | `TODO(designer): max` | B RoB depth. Required > 0 when `B_ROB_TYPE != NoRoB`. |
| `R_ROB_TYPE` | enum `rob_type_e` | `NoRoB` | as above | RoB policy for R responses. |
| `R_ROB_SIZE` | int | 0 | `TODO(designer): max` | R RoB depth. |
| `CUT_AX` | bool | `false` | — | Insert spill register on AW/AR path (adds 1 cycle, breaks timing). |
| `CUT_RSP` | bool | `false` | — | Insert spill register on response path. |

### Route Configuration (`ROUTE_CFG`)

| Name | Type | Default | Constraint | Description |
|---|---|---|---|---|
| `ROUTE_ALGO` | enum | `XYRouting` | `{XYRouting, SourceRouting}` (`TODO(designer): confirm full enum`) | Routing algorithm. |
| `USE_ID_TABLE` | bool | `false` | — | Use SAM table to derive `dst_id`. When `false`, decode directly from address using `XY_ADDR_OFFSET_X/Y`. |
| `XY_ADDR_OFFSET_X` | int | 32 | `< ADDR_WIDTH - X_WIDTH` | Bit offset of `dst_x` in AXI address. |
| `XY_ADDR_OFFSET_Y` | int | 36 | `< ADDR_WIDTH - Y_WIDTH` | Bit offset of `dst_y` in AXI address. |
| `NUM_SAM_RULES` | int | 0 | required > 0 when `USE_ID_TABLE = true` | Number of SAM rules. |

### Type parameters

| Name | Description | Origin |
|---|---|---|
| `flit_t` | Flit data type, width = `FLIT_WIDTH` (default 400 bits) | `02_flit.md` |
| `hdr_t` | Flit header type, width = `HEADER_WIDTH` (default 48 bits) | `02_flit.md` |
| `id_t` | Node ID type, width = `X_WIDTH + Y_WIDTH` (default 8 bits) | derived |
| `noc_req_t` | Request link bundle (`{valid, ready, flit}`) | derived |
| `noc_rsp_t` | Response link bundle | derived |
| `axi_in_req_t` / `axi_in_rsp_t` | AXI manager-side request/response bundle | AXI4 typedef |
| `axi_out_req_t` / `axi_out_rsp_t` | AXI subordinate-side request/response bundle | AXI4 typedef |
| `sam_rule_t` | SAM address rule type | required when `USE_ID_TABLE = true` |
<!-- source: 04_network_interface.md §3.1, §3.2, §3.3, §3.5 -->

### Simulation-only parameters (C++ model)

| Name | Source | Default | Description |
|---|---|---|---|
| `NMU_BUFFER_DEPTH` | `NocConfig` | 2 | NMU injection buffer depth (cycles of decoupling between FlitPack and Router). |
| `MAX_OUTSTANDING` | `NocConfig` | 8 | TrafficManager simultaneous in-flight count. Constraint: ≤ `MAX_TXNS`. |
| `MAX_BURST_LEN` | `NocConfig` | 16 | AXI burst maximum beats (= NSU reassembly depth). |

These parameters apply to the C++ behavior model only and may not appear in the synthesizable RTL. `TODO(designer):` confirm which (if any) of these become RTL parameters.
<!-- source: 04_network_interface.md §3.6 -->

## Clocks

| Signal | Direction | Description |
|---|---|---|
| `clk_i` | input | Functional clock for both NMU and NSU. `TODO(designer):` confirm whether AXI side and NoC side share this single clock or whether the design supports independent AXI and NoC clocks. |
<!-- source: implicit from 01_overview.md (single-clock assumption); ni-specific clock list NOT in source -->

## Resets

`TODO(designer):` Reset signal name(s), active level(s), and synchronicity are **not enumerated** in the source. The `02_flit.md`/`04_network_interface.md` documents do not include a `Resets` section for `ni`. Add this table before D1; the conventional names would be `rst_ni` (active-low, synchronous to `clk_i`).

| Signal | Direction | Active | Sync | Description |
|---|---|---|---|---|
| `rst_ni` | input | low | sync | `TODO(designer): confirm name, active level, and sync/async per project convention` |

## Bus interfaces

`ni` exposes four bus-style ports: two on the AXI side and two on the NoC side. The source documents describe each as a single typed bundle; signal-level decomposition must be added before D1.

### `axi_in` (AXI4 manager port — accepts requests from the connected AXI master)

- Protocol: AXI4 full.
- Role: subordinate (this port is driven by an external AXI master).
- Address width: `AXI_CFG.ADDR_WIDTH` (default 64).
- Data width: `AXI_CFG.DATA_WIDTH` (default 256).
- ID width: `AXI_CFG.IN_ID_WIDTH` (default 8).
- User width: `AXI_CFG.USER_WIDTH` (default 8).
- All five AXI channels (AW/W/AR/B/R) are supported. AXI4-Stream and atomic / exclusive operations are **not** supported.

| Signal bundle | Direction | Description |
|---|---|---|
| `axi_in_req_i` | input | AW, W, AR channel signals plus B/R `ready`. Type: `axi_in_req_t`. |
| `axi_in_rsp_o` | output | B, R channel signals plus AW/W/AR `ready`. Type: `axi_in_rsp_t`. |

`TODO(designer):` Decompose to AXI4-spec signal granularity (awvalid, awready, awid, awaddr, awlen, awsize, awburst, awcache, awlock, awprot, awregion, awqos, awuser, wvalid, wready, wdata, wstrb, wlast, wuser, ...).
<!-- source: 04_network_interface.md §4.1; 02_flit.md §7 (atomics OOS) -->

### `axi_out` (AXI4 subordinate port — drives the connected AXI slave)

- Protocol: AXI4 full.
- Role: master (this port drives an external AXI subordinate, typically a local memory).
- Address / data / ID / user widths: same configuration as `axi_in` per `AXI_CFG`.

| Signal bundle | Direction | Description |
|---|---|---|
| `axi_out_req_o` | output | AW, W, AR channel signals plus B/R `ready`. Type: `axi_out_req_t`. |
| `axi_out_rsp_i` | input | B, R channel signals plus AW/W/AR `ready`. Type: `axi_out_rsp_t`. |

`TODO(designer):` Decompose as for `axi_in`.
<!-- source: 04_network_interface.md §4.1 -->

### `noc_req` (NoC request link, identical interface to Router-Router links)

- Width: `LINK_WIDTH` = `FLIT_WIDTH + 2` = 402 bits at default configuration.
- Carries: AW, W, AR flits (`axi_ch ∈ {0, 1, 2}`).

| Signal bundle | Direction | Description |
|---|---|---|
| `noc_req_o` | output | NMU → Router; injects request flits. Sub-fields: `valid`, `ready`, `flit` (= `flit_t`). |
| `noc_req_i` | input | Router → NSU; receives request flits. |

### `noc_rsp` (NoC response link)

- Width: `LINK_WIDTH` = 402 bits at default.
- Carries: B, R flits (`axi_ch ∈ {3, 4}`).

| Signal bundle | Direction | Description |
|---|---|---|
| `noc_rsp_o` | output | NSU → Router; injects response flits. |
| `noc_rsp_i` | input | Router → NMU; receives response flits. |

`TODO(designer):` Decompose `noc_req_*`/`noc_rsp_*` to the wire-level `valid` (1 bit), `ready` (1 bit), `flit` (`FLIT_WIDTH` bits) triplet on each direction.
<!-- source: 04_network_interface.md §4.2; 02_flit.md §4 -->

## Sideband signals

| Signal | Direction | Width | Description |
|---|---|---|---|
| `id_i` | input | `X_WIDTH + Y_WIDTH` (default 8) | This `ni`'s Node ID (XY coordinate). Strap-style: held stable during operation. `TODO(designer):` confirm whether this is a one-shot strap or live-rebindable. |
| `route_table_i` | input | `route_t[]` (`TODO(designer): width — depends on NUM_SAM_RULES and route table format`) | Routing table; consulted by AddrTrans when `ROUTE_CFG.ROUTE_ALGO = SourceRouting` or `USE_ID_TABLE = true`. |
<!-- source: 04_network_interface.md §4.3 -->

## Interrupts

`ni` exposes **no top-level interrupt outputs** in the source documents. Errors are surfaced via:

- AXI `bresp` / `rresp` (= `SLVERR`) for ECC uncorrectable.
- CSR `ERR_STATUS` / counter registers (see `registers.md`).

`TODO(designer):` If a system-level interrupt aggregator expects an `intr_*_o` from `ni` for ECC / timeout reporting, it must be added here. The current source describes only CSR-readable error state; software polling is implied.

## Alerts

`ni` has no alert outputs. Not on a security-critical path per `theory_of_operation.md` §Security countermeasures.

## Inter-IP signals

`ni` connects to a Router exclusively via `noc_req_*` / `noc_rsp_*`. There are no other inter-IP direct connections (no entropy bus, no key bus, no clock-manager idle hint, etc.) documented in the source.
