"""Register parser — Task 4."""
from pathlib import Path
import pytest
from ni_spec import generator

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
MD_DIR = SPEC_VALIDATE.parent / "spec" / "ni" / "doc"


def test_parse_csr_policy_has_four_keys():
    policy = generator.parse_csr_policy(MD_DIR / "registers.md")
    assert set(policy.keys()) >= {"sub_word_write", "unmapped_read", "misaligned", "wo_read"}


def test_parse_register_map_includes_reserved_row():
    """0x110 (reserved for LAST_ERR_INFO_HI) must show up as kind=reserved."""
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    reserved = [r for r in regs if r["offset"] == "0x110"]
    assert len(reserved) == 1
    assert reserved[0]["kind"] == "reserved"
    assert reserved[0].get("access") is None  # em-dash means no access


def test_parse_register_map_handles_rw1c():
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    err_status = next(r for r in regs if r["name"] == "ERR_STATUS")
    assert err_status["access"] == "RW1C"
    assert err_status["reset_expr"] == "0x0"


def test_parse_register_map_skips_section_header_rows():
    """Rows like '**Error Status / IRQ**' must not be parsed as registers."""
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    names = [r["name"] for r in regs]
    assert not any("Error Status" in n for n in names)


def test_parse_register_map_count():
    """All real registers from the map plus the 1 reserved row."""
    regs = generator.parse_register_map(MD_DIR / "registers.md")
    normal = [r for r in regs if r["kind"] == "register"]
    reserved = [r for r in regs if r["kind"] == "reserved"]
    # registers.md has 40 real registers + 1 reserved placeholder (0x110)
    assert len(normal) == 40
    assert len(reserved) == 1


def test_parse_register_fields_base_qos():
    """BASE_QOS register must have at least BASE_QOS and URGENCY_STEP fields."""
    fields = generator.parse_register_fields(MD_DIR / "registers.md", "BASE_QOS")
    field_names = [f["name"] for f in fields]
    assert "BASE_QOS" in field_names
    assert "URGENCY_STEP" in field_names
