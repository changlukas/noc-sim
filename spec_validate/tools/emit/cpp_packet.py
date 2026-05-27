"""C++ emitter for packet domain.

Consumes ni_spec.constants only -- no direct JSON parsing.
Refactored from gen_cpp_header.py:emit().
"""
from __future__ import annotations
from pathlib import Path
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def emit(packet_json: Path, spec_version: str) -> str:
    """Return C++ header body (no provenance banner -- caller prepends it)."""
    spec = load_doc(packet_json)

    out: list[str] = []
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ni {")
    out.append("")

    out.append("// --- top-level flit widths (from flit.derived) ---")
    out.append(f"constexpr int FLIT_WIDTH        = {C.flit_width(spec)};")
    out.append(f"constexpr int HEADER_WIDTH      = {C.header_width(spec)};")
    out.append(f"constexpr int PAYLOAD_WIDTH     = {C.payload_width(spec)};")
    out.append(f"constexpr int LINK_WIDTH        = {C.link_width(spec)};")
    derived = spec["flit"]["derived"]
    for k in ("FLIT_DATA_WIDTH", "HEADER_DATA_WIDTH", "WSTRB_WIDTH"):
        if k in derived:
            out.append(f"constexpr int {k:<15} = {derived[k]};")
    out.append("")

    out.append("// --- header field bit positions (from flit.header_fields) ---")
    out.append("namespace header {")
    for f in spec["flit"]["header_fields"]:
        n = f["name"].upper()
        enabled_val = "true" if f.get("enabled", True) else "false"
        if f.get("width", 1) == 0:
            # width=0 reserved placeholder: emit WIDTH=0 + ENABLED only; no LSB/MSB (field not bit-addressable)
            out.append(f"constexpr int  {n}_WIDTH   = 0;  // reserved placeholder (width=0 -- not in flit)")
            out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
        else:
            out.append(f"constexpr int  {n}_LSB     = {f['lsb']};")
            out.append(f"constexpr int  {n}_MSB     = {f['msb']};")
            out.append(f"constexpr int  {n}_WIDTH   = {f['width']};")
            out.append(f"constexpr bool {n}_ENABLED = {enabled_val};")
    out.append("}  // namespace header")
    out.append("")

    out.append("// --- payload widths per channel (from flit.payload_channels) ---")
    out.append("namespace payload {")
    for ch in spec["flit"]["payload_channels"]:
        out.append(f"constexpr int {ch['name']}_WIDTH = {ch['payload_width']};")
    out.append("}  // namespace payload")
    out.append("")

    out.append("// --- all field widths (from flit.field_widths) ---")
    out.append("namespace width {")
    for name, val in spec["flit"].get("field_widths", {}).items():
        out.append(f"constexpr int {name:<22} = {val};")
    out.append("}  // namespace width")
    out.append("")

    # --- static_assert arithmetic invariants (design doc sec 6.4) ---
    # Only equality invariants; no tiling/cross-ref/width_param eval.
    out.append("// --- static_assert: arithmetic equality invariants (design doc sec 6.4) ---")

    out.append(
        "static_assert(FLIT_WIDTH == HEADER_WIDTH + PAYLOAD_WIDTH,"
        " \"Flit width arithmetic inconsistent: HEADER_WIDTH + PAYLOAD_WIDTH must equal FLIT_WIDTH\");"
    )

    # SECDED bound: 2^parity_bits >= data_bits + parity_bits + 1
    # flit_ecc covers FLIT_DATA_WIDTH data bits.
    # We compute the literal check in Python and emit a compile-time boolean constant assertion.
    flit_data = derived.get("FLIT_DATA_WIDTH")
    flit_ecc_w = None
    for f in spec["flit"]["header_fields"]:
        if f["name"] == "flit_ecc":
            flit_ecc_w = f["width"]
            break
    if flit_data is not None and flit_ecc_w is not None:
        # Emit using header:: qualified name so the constant is in scope.
        out.append(
            "static_assert((1 << header::FLIT_ECC_WIDTH) >= FLIT_DATA_WIDTH + header::FLIT_ECC_WIDTH + 1,"
            " \"SECDED bound: 2^parity_bits must be >= data_bits + parity_bits + 1\");"
        )
    out.append("")

    out.append("}  // namespace ni")
    return "\n".join(out) + "\n"
