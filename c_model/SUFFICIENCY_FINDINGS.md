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
