"""Byte-identical regression gate for pure-parameterization refactor.

Captures pre-refactor elaborated artifacts as fixtures.
Re-elaborates each domain and diffs against golden (timestamp line excluded).
Any post-refactor task that changes elaborator output trips this test.
"""
from __future__ import annotations
import re
import subprocess
import sys
from pathlib import Path

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
GOLDEN_DIR    = Path(__file__).resolve().parent / "golden"


def _strip_timestamp(text: str) -> str:
    """Remove the 'Generated at: <UTC>' line (only line that varies between regens)."""
    return re.sub(r"^//\s*Generated at:.*$\n?", "", text, flags=re.MULTILINE)


def _regen(target: str, domain: str, out_dir: Path) -> str:
    """Run codegen for one (target, domain) into a temp dir, return content."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        result = subprocess.run(
            [sys.executable, str(SPEC_VALIDATE / "tools" / "codegen.py"),
             "--target", target, "--domain", domain, "--out", tmp],
            capture_output=True, text=True, cwd=str(SPEC_VALIDATE),
        )
        assert result.returncode == 0, f"codegen failed: {result.stderr}"
        files = list(Path(tmp).iterdir())
        assert len(files) == 1, f"expected single output, got {files}"
        return files[0].read_text(encoding="utf-8")


def _golden(name: str) -> str:
    return (GOLDEN_DIR / f"{name}.golden").read_text(encoding="utf-8")


def test_golden_cpp_packet():
    assert _strip_timestamp(_regen("cpp", "packet", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_flit_constants.h"))


def test_golden_cpp_signals():
    assert _strip_timestamp(_regen("cpp", "signals", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_signals.h"))


def test_golden_cpp_registers():
    """Registers domain is out of scope but must not regress."""
    assert _strip_timestamp(_regen("cpp", "registers", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_regs.h"))


def test_golden_sv_packet():
    assert _strip_timestamp(_regen("sv", "packet", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_flit_pkg.sv"))


def test_golden_sv_signals():
    assert _strip_timestamp(_regen("sv", "signals", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_signals_pkg.sv"))


def test_golden_sv_registers():
    assert _strip_timestamp(_regen("sv", "registers", GOLDEN_DIR)) == \
           _strip_timestamp(_golden("ni_regs_pkg.sv"))
