# Architecture

Two artifact tiers, generated top-down:

```
generated/*.json                <- validated JSON SSoT
ni_function_blocks.json
        |  (tools/emit/*.py, dispatched by tools/codegen.py)
        v
include/*.h                     <- C++ headers for the C-model
rtl_pkg/*.sv                    <- SystemVerilog packages for co-sim
```

## Domains

Four spec domains, each with its own JSON SSoT and a pair of emitters:

| Domain | JSON SSoT | C++ header | SV package |
|--------|-----------|------------|------------|
| `packet` | `generated/ni_packet.json` | `ni_flit_constants.h` | `ni_flit_pkg.sv` |
| `signals` | `generated/ni_signals.json` | `ni_signals.h` | `ni_signals_pkg.sv` |
| `registers` | `generated/ni_registers.json` | `ni_regs.h` | `ni_regs_pkg.sv` |
| `blocks` | `ni_function_blocks.json` | `ni_blocks.h` | `ni_blocks_pkg.sv` |

The `blocks` SSoT is hand-written and lives at the `spec_validate/` root, not under `generated/`.

## Layers

| Layer | Module | Responsibility |
|-------|--------|----------------|
| Loader | `ni_spec.loader` | Read JSON SSoT files. Pure I/O. |
| Constants API | `ni_spec.constants` | Pure-function accessors over a loaded spec. The firewall between schema and consumers. |
| Codegen dispatcher | `tools/codegen.py` | Route `(target, domain)` to the right emitter. |
| Emitters | `tools/emit/{cpp,sv}_*.py` | Format the output. Each consumes only `ni_spec.constants` — never raw JSON. |

## Why a firewall?

If the JSON shape changes — say a field is renamed — only `ni_spec.constants` needs an update. All eight emitters and any downstream Python consumer stay untouched.

## Source of truth

The JSON SSoT files are the contract. `--check` enforces that committed `include/` and `rtl_pkg/` exactly match what those JSONs would emit (timestamp aside). Edit the SSoT, run codegen, commit both.

Hand-editing files in `include/` or `rtl_pkg/` will be caught by `--check`. Don't.

## Provenance

Every emitted file carries a five-line banner naming the source JSON, its SHA, the tool version, and the timestamp. See [Artifacts § Provenance banner](artifacts.md#provenance-banner).
