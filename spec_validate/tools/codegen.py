#!/usr/bin/env python
"""Unified codegen entry point.

Usage:
    py -3 tools/codegen.py --target cpp --domain packet --out include/
    py -3 tools/codegen.py --target cpp --domain signals --out include/
    py -3 tools/codegen.py --target cpp --domain registers --out include/
    py -3 tools/codegen.py --target cpp --domain blocks --out include/
    py -3 tools/codegen.py --check        # regen + diff vs committed; exit 1 on drift

SV target raises NotImplementedError (Task 8).
"""
from __future__ import annotations
import argparse
import difflib
import sys
import tempfile
from pathlib import Path

# Ensure spec_validate/ is on the import path.
SPEC_VALIDATE = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(SPEC_VALIDATE))
# Ensure tools/ sub-packages are importable as "tools.emit.*".
TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR.parent))

from ni_spec.loader import load_spec_version
from tools.emit import common
from tools.emit import cpp_packet, cpp_signals, cpp_registers, cpp_blocks


# Maps (target, domain) -> (emitter_func, output_filename, source_json_name)
# source_json_name is relative to SPEC_VALIDATE/ (for blocks) or
# SPEC_VALIDATE/generated/ (for the other three).
DOMAIN_TO_EMITTER: dict[tuple[str, str], tuple] = {
    ("cpp", "packet"):    (cpp_packet.emit,    "ni_flit_constants.h", "generated/ni_packet.json"),
    ("cpp", "signals"):   (cpp_signals.emit,   "ni_signals.h",        "generated/ni_signals.json"),
    ("cpp", "registers"): (cpp_registers.emit, "ni_regs.h",           "generated/ni_registers.json"),
    ("cpp", "blocks"):    (cpp_blocks.emit,    "ni_blocks.h",         "ni_function_blocks.json"),
}


def _resolve_source(src_rel: str) -> Path:
    """Resolve a source JSON path relative to SPEC_VALIDATE/."""
    return SPEC_VALIDATE / src_rel


def run_emit(target: str, domain: str, out_dir: Path) -> Path:
    """Run one emitter and write the output file.  Returns the written path."""
    if target == "sv":
        raise NotImplementedError("Task 8")

    key = (target, domain)
    if key not in DOMAIN_TO_EMITTER:
        raise ValueError(f"Unknown (target, domain): {key}")

    emitter_fn, out_name, src_rel = DOMAIN_TO_EMITTER[key]
    src_path = _resolve_source(src_rel)

    spec_version = load_spec_version()
    body = emitter_fn(src_path, spec_version)
    banner = common.provenance_banner(src_path)

    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / out_name
    out_path.write_text(banner + body, encoding="ascii", errors="strict")
    return out_path


def _strip_timestamp(lines: list[str]) -> list[str]:
    """Remove the '// Generated at:' line so timestamps don't cause false drift."""
    return [l for l in lines if not l.startswith("// Generated at:")]


def cmd_emit(args: argparse.Namespace) -> int:
    if args.target == "sv":
        print("ERROR: --target sv not implemented yet (Task 8)", file=sys.stderr)
        return 1
    if not args.domain:
        print("ERROR: --domain is required with --target", file=sys.stderr)
        return 2

    out_dir = Path(args.out) if args.out else SPEC_VALIDATE / "include"
    try:
        written = run_emit(args.target, args.domain, out_dir)
        print(f"wrote {written}", file=sys.stderr)
        return 0
    except FileNotFoundError as exc:
        print(f"ERROR: source JSON not found: {exc}", file=sys.stderr)
        return 1
    except NotImplementedError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


def cmd_check(_args: argparse.Namespace) -> int:
    """Regen all cpp targets to a temp dir and diff vs committed include/.

    The timestamp line in the banner is excluded from comparison.
    Exits 0 if all headers match, 1 if any drift is detected.
    """
    committed_dir = SPEC_VALIDATE / "include"
    all_ok = True

    with tempfile.TemporaryDirectory() as tmp:
        fresh_dir = Path(tmp)
        for (target, domain), (_, out_name, src_rel) in DOMAIN_TO_EMITTER.items():
            if target != "cpp":
                continue  # SV check deferred to Task 8

            src_path = _resolve_source(src_rel)
            if not src_path.exists():
                print(f"[skip] {domain}: source JSON not found ({src_rel})", file=sys.stderr)
                continue

            try:
                fresh_path = run_emit(target, domain, fresh_dir)
            except Exception as exc:
                print(f"[error] {domain}: {exc}", file=sys.stderr)
                all_ok = False
                continue

            committed_path = committed_dir / fresh_path.name
            if not committed_path.exists():
                print(f"[missing committed] {fresh_path.name}")
                all_ok = False
                continue

            fresh_lines   = _strip_timestamp(fresh_path.read_text(encoding="ascii").splitlines())
            committed_lines = _strip_timestamp(committed_path.read_text(encoding="ascii").splitlines())

            if fresh_lines != committed_lines:
                all_ok = False
                diff = list(difflib.unified_diff(
                    committed_lines,
                    fresh_lines,
                    fromfile=f"committed/{fresh_path.name}",
                    tofile=f"regen/{fresh_path.name}",
                    lineterm="",
                ))
                print(f"[drift] {fresh_path.name}:")
                print("\n".join(diff[:40]))

    return 0 if all_ok else 1


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Unified codegen for NI spec (C++ headers; SV in Task 8)."
    )
    parser.add_argument(
        "--target", choices=["cpp", "sv"], default="cpp",
        help="output language target (default: cpp)",
    )
    parser.add_argument(
        "--domain", choices=["packet", "signals", "registers", "blocks"],
        help="spec domain to emit",
    )
    parser.add_argument(
        "--out", default=None,
        help="output directory (default: spec_validate/include/)",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="regen to scratch dir and diff vs committed; exit 1 on drift",
    )
    args = parser.parse_args()

    if args.check:
        return cmd_check(args)
    return cmd_emit(args)


if __name__ == "__main__":
    sys.exit(main())
