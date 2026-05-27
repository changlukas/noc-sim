"""C++ emitter for function blocks domain.

Emits FunctionBlock enum class, per-feature mode enums, and compile-time
parameter constants. Consumes ni_spec.constants only -- no direct JSON parsing.
"""
from __future__ import annotations
from pathlib import Path
import re
import sys

SPEC_VALIDATE = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

from ni_spec import constants as C
from ni_spec.loader import load_doc


def _to_enum_member(name: str) -> str:
    """Convert a mode/feature string to a valid C++ enum member identifier."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", name)
    s = re.sub(r"_+", "_", s).strip("_")
    return s


def _to_const_name(name: str) -> str:
    """Convert a param key to UPPER_SNAKE_CASE C++ identifier."""
    s = re.sub(r"[^A-Za-z0-9_]", "_", str(name))
    s = re.sub(r"_+", "_", s).strip("_")
    return s.upper()


def emit(blocks_json: Path, spec_version: str) -> str:
    """Return C++ header body (no provenance banner -- caller prepends it)."""
    spec = load_doc(blocks_json)

    block_names = C.blocks_function_block_names(spec)
    compile_params = C.blocks_compile_time_params(spec)

    out: list[str] = []
    out.append("#pragma once")
    out.append("#include <cstdint>")
    out.append("")
    out.append("namespace ni {")
    out.append("namespace blocks {")
    out.append("")

    # FunctionBlock enum class listing all blocks
    out.append("// --- function block inventory ---")
    out.append("enum class FunctionBlock {")
    for name in block_names:
        out.append(f"  {_to_enum_member(name)},")
    out.append("};")
    out.append("")

    # Per-feature mode enums
    out.append("// --- per-feature mode enums ---")
    mode_enums_emitted = False
    for block_name in block_names:
        # blocks_modes_of returns list of (feature_id, mode) tuples
        modes_by_feature: dict[str, list[str]] = {}
        for feat_id, mode in C.blocks_modes_of(spec, block_name):
            modes_by_feature.setdefault(feat_id, []).append(mode)
        for feat_id, modes in modes_by_feature.items():
            if not modes:
                continue
            # Derive enum class name: e.g. FEAT-NMU-ROB -> ROBMode
            # Extract the last segment after the final dash
            short = feat_id.split("-")[-1] if "-" in feat_id else feat_id
            enum_name = f"{_to_enum_member(short)}Mode"
            members = [_to_enum_member(m) for m in modes]
            out.append(f"enum class {enum_name} {{")
            for m in members:
                out.append(f"  {m},")
            out.append("};")
            mode_enums_emitted = True
    if not mode_enums_emitted:
        out.append("// (No feature mode enums in this spec.)")
    out.append("")

    # Compile-time parameter constants
    out.append("// --- compile-time parameters ---")
    if compile_params:
        for param_name, default_val in compile_params.items():
            const_name = _to_const_name(param_name)
            # Emit as int if integer, otherwise as a comment with string value
            if isinstance(default_val, int):
                out.append(f"constexpr int {const_name} = {default_val};")
            elif isinstance(default_val, str):
                try:
                    ival = int(default_val)
                    out.append(f"constexpr int {const_name} = {ival};")
                except ValueError:
                    # String value (e.g. "XYRouting") -- emit as comment only
                    out.append(f"// {const_name} = \"{default_val}\"  (string param, no C++ constexpr)")
            else:
                out.append(f"// {const_name} = {default_val!r}  (non-integer)")
    else:
        out.append("// (No compile-time parameters in this spec.)")
    out.append("")

    out.append("}  // namespace blocks")
    out.append("}  // namespace ni")
    return "\n".join(out) + "\n"
