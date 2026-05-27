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
        out.append(f"constexpr int {n}_LSB   = {f['lsb']};")
        out.append(f"constexpr int {n}_MSB   = {f['msb']};")
        out.append(f"constexpr int {n}_WIDTH = {f['width']};")
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

    out.append("}  // namespace ni")
    return "\n".join(out) + "\n"
