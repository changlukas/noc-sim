"""CLI 入口: python -m ni_spec <md_dir>

Path B 流程：
  1. generator 讀 <md_dir>/packet_format.md → generated/ni_packet.json
  2. generator 讀 <md_dir>/signal_interface.md → generated/ni_signals.json
  3. Layer 1 (JSON Schema) 驗各 generated JSON
  4. Layer 2 (semantic / arithmetic) 驗
  5. 印 report、回傳 exit code
"""

from __future__ import annotations
import sys
from pathlib import Path

from .generator import write_generated_json, write_generated_signals_json
from .loader import load_doc
from .invariants import check_schema, check_flit_arithmetic
from .report import print_report

for _s in (sys.stdout, sys.stderr):
    if hasattr(_s, "reconfigure"):
        _s.reconfigure(encoding="utf-8", errors="replace")


SPEC_VALIDATE = Path(__file__).resolve().parent.parent
GENERATED_DIR = SPEC_VALIDATE / "generated"

PACKET_JSON = GENERATED_DIR / "ni_packet.json"
PACKET_SCHEMA = GENERATED_DIR / "ni_packet.schema.json"

SIGNALS_JSON = GENERATED_DIR / "ni_signals.json"
SIGNALS_SCHEMA = GENERATED_DIR / "ni_signals.schema.json"


def main() -> int:
    if len(sys.argv) < 2:
        print("用法: python -m ni_spec <md_dir>", file=sys.stderr)
        print("  md_dir: 含 packet_format.md + signal_interface.md 的目錄", file=sys.stderr)
        print(f"           例（從 noc-sim/spec_validate/ 跑）: ../spec/ni/doc", file=sys.stderr)
        print(f"  輸出: {GENERATED_DIR.relative_to(SPEC_VALIDATE)}/ni_*.json", file=sys.stderr)
        return 2

    md_dir = sys.argv[1]

    # Step 1: generate packet JSON
    try:
        packet = write_generated_json(md_dir, PACKET_JSON)
    except FileNotFoundError as e:
        print(f"[FATAL] packet generator: {e}", file=sys.stderr)
        return 2

    # Step 2: generate signals JSON
    try:
        signals = write_generated_signals_json(md_dir, SIGNALS_JSON)
    except FileNotFoundError as e:
        print(f"[FATAL] signals generator: {e}", file=sys.stderr)
        return 2

    # Step 3-4: validate each
    issues = []

    packet_schema = load_doc(PACKET_SCHEMA) if PACKET_SCHEMA.exists() else None
    issues += check_schema(packet, packet_schema)
    issues += check_flit_arithmetic(packet)

    signals_schema = load_doc(SIGNALS_SCHEMA) if SIGNALS_SCHEMA.exists() else None
    # Layer 1 only for signals (Layer 2 invariants for signals 待加)
    if signals_schema is not None:
        from .invariants import Issue
        import jsonschema
        validator = jsonschema.Draft202012Validator(signals_schema)
        for e in sorted(validator.iter_errors(signals), key=lambda e: list(e.absolute_path)):
            loc = "/".join(str(p) for p in e.absolute_path) or "(root)"
            issues.append(Issue("ERROR", "L1-SIG-SCHEMA", f"{loc}: {e.message}"))

    has_l1_err = any(i.check in ("L1-SCHEMA", "L1-SIG-SCHEMA") and i.severity == "ERROR" for i in issues)
    has_l1_skip = (signals_schema is None or
                   any(i.check == "L1-SCHEMA" and i.severity == "WARN" for i in issues))
    has_l2_err = any(i.check.startswith("L2") and i.severity == "ERROR" for i in issues)

    layers = {
        "Generator (MD -> JSON)":      f"OK (ni_packet.json + ni_signals.json)",
        "Layer 1 (JSON Schema, both)": "SKIPPED" if has_l1_skip else ("FAIL" if has_l1_err else "PASS"),
        "Layer 2 (packet arithmetic)": "FAIL" if has_l2_err else "PASS",
    }
    return print_report(issues, target_name="ni_packet.json + ni_signals.json",
                        show_layers=layers)


if __name__ == "__main__":
    sys.exit(main())
