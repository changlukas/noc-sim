"""SV emitter for registers domain.

Produces rtl_pkg/ni_regs_pkg.sv.
Uses localparam int unsigned for offsets and field masks (design doc §6.2).
Consumes ni_spec.constants only -- no direct JSON parsing.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def _to_identifier(name: str) -> str:
    """Convert register name to a safe SV identifier (upper-case, special chars removed)."""
    s = name.replace("(", "").replace(")", "").replace("`", "").strip()
    s = re.sub(r"[^A-Za-z0-9_]", "_", s)
    s = re.sub(r"_+", "_", s).strip("_")
    return s.upper()


def emit(registers_json: Path, spec_version: str) -> str:
    """Return SV package body (no provenance banner -- caller prepends it)."""
    spec = load_doc(registers_json)

    offsets = C.regs_offsets(spec)

    out: list[str] = []
    out.append("`ifndef NI_REGS_PKG_SVH")
    out.append("`define NI_REGS_PKG_SVH")
    out.append("")
    out.append("package ni_regs_pkg;")
    out.append("")

    # Offset constants
    out.append("  // --- register offsets ---")
    for reg_name, offset_int in offsets.items():
        ident = _to_identifier(reg_name)
        out.append(f"  localparam int unsigned {ident}_OFFSET = {hex(offset_int)};")
    out.append("")

    # Field masks
    out.append("  // --- field bit masks ---")
    has_fields = False
    for reg in spec.get("registers", []):
        if reg.get("kind") != "register":
            continue
        fields = reg.get("fields", [])
        if not fields:
            continue
        reg_ident = _to_identifier(reg["name"])
        for f in fields:
            field_ident = _to_identifier(f["name"])
            try:
                mask = C.regs_field_mask(spec, reg["name"], f["name"])
            except KeyError:
                continue
            out.append(f"  localparam int unsigned {reg_ident}_{field_ident}_MASK = {hex(mask)};")
            has_fields = True
    if not has_fields:
        out.append("  // (No field mask definitions in this spec.)")
    out.append("")

    out.append("endpackage")
    out.append("")
    out.append("`endif // NI_REGS_PKG_SVH")
    return "\n".join(out) + "\n"
