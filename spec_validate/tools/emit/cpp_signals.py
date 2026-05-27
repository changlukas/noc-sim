"""C++ emitter for signals domain.

Emits reset initializer constants for output signals (non-external_driven).
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
    """Return C++ header body (no provenance banner -- caller prepends it)."""
    spec = load_doc(signals_json)

    # Collect all signals with non-null, non-external_driven reset_behavior.
    # These are the output signals that have a defined reset value.
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
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ni {")
    out.append("namespace signals {")
    out.append("")
    out.append("// Reset initializer constants for output signals.")
    out.append("// Input signals (external_driven) have no reset value defined here.")
    out.append("")
    if reset_consts:
        for const_name, value in reset_consts:
            out.append(f"constexpr int {const_name}_RESET = {value};")
    else:
        out.append("// (No output signals with defined reset values in this spec.)")
    out.append("")
    out.append("}  // namespace signals")
    out.append("}  // namespace ni")
    return "\n".join(out) + "\n"
