### 1. Industry pattern survey

- **SystemRDL / PeakRDL**: Source stays symbolic/structural; compiler elaborates an address-map model. SystemRDL parameters can reference other parameters and are re-evaluated after overrides; PeakRDL uses `systemrdl-compiler` to interpret/elaborate input, and `Node.get_property()` returns default/implied/derived values. Adopt: `get_*` / resolver API over raw dict access; expose derived values through an elaborated model, not stored JSON snapshots. Sources: [SystemRDL params](https://accellera.org/images/downloads/standards/systemrdl/SystemRDL_2.0_Jan2018.pdf), [PeakRDL elaboration](https://peakrdl.readthedocs.io/en/latest/processing-input.html), [Node properties](https://systemrdl-compiler.readthedocs.io/en/stable/properties.html).

- **IP-XACT / IEEE 1685**: Mixed symbolic metadata plus explicit parameter-resolution policy. Expressions can be translated across design/component parameter namespaces, and some values are intentionally resolved by the design environment at runtime. Adopt: explicit `resolve`/namespace discipline; do not blur field names, parameter names, and generated constants. Source: [IP-XACT User Guide](https://www.eda.org/images/downloads/standards/ip-xact/IPXACT-2022_user_guide.pdf).

- **OpenAPI / JSON Schema**: Source favors references/components over duplication; tools may dereference/bundle as a build artifact. Adopt: `components`/`$ref` style naming: one canonical namespace, resolver/dereferencer API, optional debug dump. Sources: [OpenAPI components](https://learn.openapis.org/specification/components.html), [JSON Schema `$ref`](https://json-schema.org/understanding-json-schema/structuring).

- **Protocol Buffers**: `.proto` is canonical; generated language files are cached artifacts. No “resolved schema snapshot” is hand-maintained beside source. Adopt: generated output marked do-not-edit; regen-diff CI. Source: [protobuf generated-code refs](https://protobuf.dev/reference/).

- **LLVM TableGen**: Declarative symbolic DSL; TableGen evaluates expressions/classes/records for backends. Adopt: resolver should produce an inspectable elaborated view, e.g. `dump_resolved_packet(spec)`, for review/debug without making it source. Source: [TableGen programmer reference](https://llvm.org/docs/TableGen/ProgRef.html).

- **Linker scripts**: Symbolic integer expressions resolved against the current symbol table/layout at link time; map files are snapshots. Adopt: expression evaluator + symbol table + deterministic diagnostics. Source: [GNU ld expressions](https://www.sourceware.org/binutils/docs/ld/Expressions.html).

- **Verilator / Yosys-CXXRTL**: HDL params are elaborated into generated C++/model artifacts; consumers use generated artifacts, not pre-resolved metadata as source. Adopt: byte-identical generated `.h/.sv` as the compatibility gate. Source: [Verilator C++ output](https://verilator.org/guide/latest/verilating.html).

### 2. Critique of proposed refactor

Design is sound and matches mature spec/codegen practice: canonical symbolic source, centralized elaboration, generated artifacts as compatibility boundary.

Main pitfalls:
- “Constants firewall” must be real. Current emitters still directly read `spec["flit"]`, `f["lsb"]`, `f["msb"]`, `f["width"]`, and `payload_width`.
- Derived values do not disappear; they move from stored JSON to resolver output. Treat them as computed API, not ad hoc recomputation in each emitter.
- Expression grammar must be versioned and tiny. `ast.parse` is fine only with an allowlist, integer-only semantics, no unary surprises, no float division, and clear unknown-symbol errors.
- Zero-width enabled fields and disabled padding fields need explicit resolver semantics.
- Byte-identical output is necessary but not sufficient: also add negative tests for drift cases that are currently impossible once resolved fields are removed.

### 3. Specific suggestions

- Move packet geometry fully behind `ni_spec.constants`. Today [constants.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/constants.py:23) reads `flit.derived`, and [constants.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/constants.py:39) returns stored `lsb/msb`.

- Update packet emitters to stop direct snapshot reads. Current direct accesses are at [cpp_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/cpp_packet.py:54), [cpp_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/cpp_packet.py:70), [cpp_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/cpp_packet.py:81), [sv_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/sv_packet.py:34), [sv_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/sv_packet.py:49), and [sv_packet.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/sv_packet.py:57).

- Replace schema requirements that force denormalization. [ni_packet.schema.json](E:/05_NoC/noc-sim/spec_validate/generated/ni_packet.schema.json:54) requires `width/lsb/msb`; [ni_packet.schema.json](E:/05_NoC/noc-sim/spec_validate/generated/ni_packet.schema.json:83) requires `payload_width`.

- Generator still computes snapshots: [generator.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/generator.py:206) computes `payload_width`; [generator.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/generator.py:287) parses `derived`; [generator.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/generator.py:329) writes `derived`.

- Signals gap: [constants.py](E:/05_NoC/noc-sim/spec_validate/ni_spec/constants.py:161) resolves pin width from `width_expr/default/"1"` and ignores `width_param`; [cpp_signals.py](E:/05_NoC/noc-sim/spec_validate/tools/elaborate/cpp_signals.py:29) punts symbolic widths to `uint64_t`. Make `signal_width(spec, iface, pin)` resolve from interface `port_parameters`.

- Add golden tests: `old_codegen_output == new_codegen_output` after stripping timestamp, plus resolver unit tests for `X_WIDTH + Y_WIDTH`, `derived` payload padding, zero-width `noc_qos`, and disabled padding fields.

### 4. Verdict

NEEDS REVISION

Confidence: high.

Top concern: the proposed architecture is right, but current emitters and signal width handling still bypass the resolver boundary.
