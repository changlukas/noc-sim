# c_model First-Round Sufficiency Findings

Each finding: what gap exists, where surfaced, workaround used, fix-direction proposal.

## F-001 — codegen does not elaborate `ni::header::HeaderField` enum
- Surfaced: `Flit::set_header_field` in Task 10
- Workaround: hand-rolled string-name dispatch in `flit.hpp::detail::header_field_pos`
- Fix-direction: add `enum class HeaderField` to `cpp_packet.py` elaborator, then
  `header_field_pos()` becomes a table lookup.

## F-002 — codegen does not elaborate padding-field list
- Surfaced: `Flit::check_padding_is_zero` in Task 10
- Workaround: returns true unconditionally (stub).
- Fix-direction: cpp_packet.py emit `constexpr int PADDING_FIELDS[]` listing fields
  whose `enabled: false`.

## F-003 — codegen does not elaborate per-channel payload field positions
- Surfaced: `Flit::set_payload_channel` / `get_payload_channel` in Task 10
- Workaround: returns empty / no-op stub.
- Fix-direction: cpp_packet.py emit `ni::payload::<CH>::<FIELD>_LSB/_MSB` family.

## F-004 — codegen does not elaborate per-register reset value
- Surfaced: `RegisterFile::reset` in Task 12
- Workaround: reset all registers to 0
- Fix-direction: cpp_registers.py emit `constexpr int <REG>_RESET = N;`

## F-005 — codegen does not elaborate REGISTER_OFFSETS[] array
- Surfaced: `RegisterFile::known_offsets_` in Task 12 (hand-maintained list)
- Workaround: hardcode offset list in source file
- Fix-direction: cpp_registers.py emit `constexpr uint32_t ALL_OFFSETS[] = {...};`

## F-006 — codegen does not elaborate per-register access mode (RW1C / WO etc.)
- Surfaced: `RegisterFile::is_wo_` / `is_rw1c_` in Task 12 (stubs returning false)
- Workaround: no policy enforcement for now
- Fix-direction: cpp_registers.py emit `constexpr ni::regs::AccessMode <REG>_ACCESS;`

## F-007 — codegen does not elaborate csr_policy as enum (only string in Task 5)
- Surfaced: ABI dispatch in `RegisterFile::read32` / `write32`
- Workaround: use Task 5's `CSR_POLICY_*` boolean constants (now plain `*` names after Task 5 fix)
- Fix-direction: cpp_registers.py emit `enum class SubWordWritePolicy` etc.
