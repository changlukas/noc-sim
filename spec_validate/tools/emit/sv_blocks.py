"""SV emitter for function blocks domain.

Produces rtl_pkg/ni_blocks_pkg.sv.
Uses typedef enum logic[N-1:0] for mode enums (design doc §6.2 strong typing).
Uses localparam int unsigned for compile-time parameter constants.
Consumes ni_spec.constants only -- no direct JSON parsing.
"""
from __future__ import annotations
import math
import re
from pathlib import Path
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def _to_enum_member(name: str) -> str:
    """Convert a mode/feature string to a valid SV enum member identifier."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    s = re.sub(r"_+", "_", s).strip("_")
    return s.upper()


def _to_const_name(name: str) -> str:
    """Convert a param key to UPPER_SNAKE_CASE SV identifier."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", str(name))
    s = re.sub(r"_+", "_", s).strip("_")
    return s.upper()


def _clog2(n: int) -> int:
    """Compute ceil(log2(n)), equivalent to SystemVerilog $clog2.

    Returns at least 1 even for n=1 (so enums with 1 member get logic[0:0]).
    Returns 0 for n=0 (edge case -- caller should guard).
    """
    if n <= 1:
        return 1
    return math.ceil(math.log2(n))


def emit(blocks_json: Path, spec_version: str) -> str:
    """Return SV package body (no provenance banner -- caller prepends it)."""
    spec = load_doc(blocks_json)

    block_names = C.blocks_function_block_names(spec)
    compile_params = C.blocks_compile_time_params(spec)

    out: list[str] = []
    out.append("`ifndef NI_BLOCKS_PKG_SVH")
    out.append("`define NI_BLOCKS_PKG_SVH")
    out.append("")
    out.append("package ni_blocks_pkg;")
    out.append("")

    # FunctionBlock enum -- typedef enum logic[N-1:0]
    n_blocks = len(block_names)
    fb_width = _clog2(n_blocks)
    out.append("  // --- function block inventory ---")
    out.append(f"  typedef enum logic [{fb_width - 1}:0] {{")
    for i, name in enumerate(block_names):
        member = _to_enum_member(name)
        suffix = "," if i < n_blocks - 1 else ""
        out.append(f"    {member} = {fb_width}'d{i}{suffix}")
    out.append("  } function_block_e;")
    out.append("")

    # Per-feature mode enums
    out.append("  // --- per-feature mode enums ---")
    mode_enums_emitted = False
    for block_name in block_names:
        modes_by_feature: dict[str, list[str]] = {}
        for feat_id, mode in C.blocks_modes_of(spec, block_name):
            modes_by_feature.setdefault(feat_id, []).append(mode)
        for feat_id, modes in modes_by_feature.items():
            if not modes:
                continue
            # Derive enum type name: e.g. FEAT-NMU-ROB -> rob_mode_e
            short = feat_id.split("-")[-1] if "-" in feat_id else feat_id
            enum_type = f"{short.lower()}_mode_e"
            n_modes = len(modes)
            width = _clog2(n_modes)
            out.append(f"  typedef enum logic [{width - 1}:0] {{")
            for i, m in enumerate(modes):
                member = _to_enum_member(m)
                # Prefix with short block name + feature short to avoid conflicts
                # e.g. ROB_MODE_NOROB = 2'd0
                suffix = "," if i < n_modes - 1 else ""
                out.append(f"    {_to_enum_member(short)}_MODE_{member} = {width}'d{i}{suffix}")
            out.append(f"  }} {enum_type};")
            mode_enums_emitted = True
    if not mode_enums_emitted:
        out.append("  // (No feature mode enums in this spec.)")
    out.append("")

    # Compile-time parameter constants
    out.append("  // --- compile-time parameters ---")
    if compile_params:
        for param_name, default_val in compile_params.items():
            const_name = _to_const_name(param_name)
            if isinstance(default_val, int):
                out.append(f"  localparam int unsigned {const_name} = {default_val};")
            elif isinstance(default_val, str):
                try:
                    ival = int(default_val)
                    out.append(f"  localparam int unsigned {const_name} = {ival};")
                except ValueError:
                    # String value (e.g. "XYRouting") -- emit as comment only
                    out.append(
                        f"  // {const_name} = \"{default_val}\"  (string param, no SV localparam)"
                    )
            else:
                out.append(f"  // {const_name} = {default_val!r}  (non-integer)")
    else:
        out.append("  // (No compile-time parameters in this spec.)")
    out.append("")

    out.append("endpackage")
    out.append("")
    out.append("`endif // NI_BLOCKS_PKG_SVH")
    return "\n".join(out) + "\n"
