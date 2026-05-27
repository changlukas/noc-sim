#!/usr/bin/env python
"""DEPRECATED -- use tools/codegen.py --target cpp --domain packet.

This wrapper delegates to the new unified codegen.py and will be removed in a
future minor version.  All arguments are forwarded; --out behaves identically.
"""
from __future__ import annotations
import subprocess
import sys
from pathlib import Path

print(
    "WARNING: gen_cpp_header.py is deprecated; "
    "use 'py -3 tools/codegen.py --target cpp --domain packet --out <dir>' instead.",
    file=sys.stderr,
)

_THIS = Path(__file__).resolve()
_CODEGEN = _THIS.parent / "codegen.py"

# Translate legacy --out <file> to --out <dir> for the new dispatcher.
# Old tool accepted --out <filepath>; new tool accepts --out <dir>.
import argparse as _ap
_p = _ap.ArgumentParser(add_help=False)
_p.add_argument("--out", default=None)
_p.add_argument("--spec-dir", default=None)  # accepted but ignored (new tool reads from generated/)
_known, _extra = _p.parse_known_args()

_cmd = [sys.executable, str(_CODEGEN), "--target", "cpp", "--domain", "packet"]

if _known.out:
    _out_path = Path(_known.out)
    # If --out points to a file, use its parent dir; the new tool always writes
    # to <out_dir>/<fixed_filename>.
    if _out_path.suffix:
        _cmd += ["--out", str(_out_path.parent)]
    else:
        _cmd += ["--out", str(_out_path)]

sys.exit(subprocess.call(_cmd))
