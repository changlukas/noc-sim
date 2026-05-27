"""SV emitter for signals domain.

Produces rtl_pkg/ni_signals_pkg.sv.
Uses localparam int unsigned for reset constants (design doc §6.2).
Consumes ni_spec.constants only -- no direct JSON parsing.
"""
from __future__ import annotations
from pathlib import Path
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def emit(signals_json: Path, spec_version: str) -> str:
    """Return SV package body (no provenance banner -- caller prepends it)."""
    spec = load_doc(signals_json)

    # Collect output signals with a defined (non-external_driven) reset value.
    reset_consts: list[tuple[str, str]] = []
    for pin_name in C.signals_pin_names(spec):
        sig = C.signals_signal_by_pin(spec, pin_name)
        if sig is None:
            continue
        rb = sig.get("reset_behavior")
        if rb is None:
            continue
        if rb.get("kind") == "external_driven":
            continue
        value = rb.get("value", "0")
        reset_consts.append((pin_name.upper(), value))

    out: list[str] = []
    out.append("`ifndef NI_SIGNALS_PKG_SVH")
    out.append("`define NI_SIGNALS_PKG_SVH")
    out.append("")
    out.append("package ni_signals_pkg;")
    out.append("")
    out.append("  // Reset initializer constants for output signals.")
    out.append("  // Input signals (external_driven) have no reset value defined here.")
    out.append("")
    if reset_consts:
        for const_name, value in reset_consts:
            out.append(f"  localparam int unsigned {const_name}_RESET = {value};")
    else:
        out.append("  // (No output signals with defined reset values in this spec.)")
    out.append("")
    out.append("endpackage")
    out.append("")
    out.append("`endif // NI_SIGNALS_PKG_SVH")
    return "\n".join(out) + "\n"
