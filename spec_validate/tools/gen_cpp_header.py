#!/usr/bin/env python
"""從 ni_packet.json 產 C++ 常數 header。

讀 spec 的方式透過 ni_spec.constants（重用 validator 的 spec-loading 邏輯，
不重寫一遍 JSON 取值）。輸出純文字 .h，C++ 編譯後完全不依賴 Python。

用法:
    python tools/gen_cpp_header.py                       # 寫到 stdout
    python tools/gen_cpp_header.py --out FILE            # 直接寫檔（避開 PowerShell BOM 問題）
    python tools/gen_cpp_header.py --spec-dir DIR --out FILE
"""

from __future__ import annotations
import argparse
import sys
from pathlib import Path

# tools/ → spec_validate/ → spec_validate/ni_spec/ 可被 import
SPEC_VALIDATE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))

# Path B：codegen 吃 generator 產出的 generated/ni_packet.json
DEFAULT_SPEC_DIR = SPEC_VALIDATE / "generated"

from ni_spec import load_spec_bundle, constants as C


def emit(spec_dir: str) -> str:
    bundle = load_spec_bundle(spec_dir)
    spec = bundle.packet

    out = []
    out.append("// AUTO-GENERATED from ni_packet.json by tools/gen_cpp_header.py")
    out.append("// Do not edit by hand. Re-run the generator after spec changes.")
    out.append(f"// Spec version: {spec['meta']['spec_version']}")
    out.append("")
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


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--spec-dir", default=str(DEFAULT_SPEC_DIR), help="spec 資料夾（含 ni_packet.json）")
    p.add_argument("--out", default=None, help="輸出路徑；省略則寫 stdout")
    args = p.parse_args()

    text = emit(args.spec_dir)
    if args.out:
        Path(args.out).parent.mkdir(parents=True, exist_ok=True)
        Path(args.out).write_text(text, encoding="ascii")
        print(f"wrote {args.out} ({len(text)} bytes)", file=sys.stderr)
    else:
        sys.stdout.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
