"""Function blocks JSON + validator — Task 5."""
from pathlib import Path
import json
import re
import pytest
from ni_spec import loader, invariants, constants

SPEC_VALIDATE = Path(__file__).resolve().parent.parent
FB_JSON = SPEC_VALIDATE / "ni_function_blocks.json"
FB_SCHEMA = SPEC_VALIDATE / "ni_function_blocks.schema.json"


def test_function_blocks_json_exists():
    assert FB_JSON.exists()


def test_function_blocks_passes_schema():
    import jsonschema
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    schema = json.loads(FB_SCHEMA.read_text(encoding="utf-8"))
    jsonschema.Draft202012Validator(schema).validate(data)


def test_two_blocks_nmu_nsu():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    names = [b["name"] for b in data["blocks"]]
    assert "NMU" in names and "NSU" in names


def test_at_least_one_feature_per_block():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    for block in data["blocks"]:
        assert len(block["features"]) >= 1, f"{block['name']} has no features"


def test_id_pattern_matches_block():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    for block in data["blocks"]:
        block_name = block["name"]
        for feat in block["features"]:
            assert feat["id"].startswith(f"FEAT-{block_name}-"), \
                f"{feat['id']} doesn't start with FEAT-{block_name}-"


def test_summary_length_under_200():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    for block in data["blocks"]:
        for feat in block["features"]:
            assert len(feat["summary"]) <= 200, \
                f"{feat['id']} summary too long ({len(feat['summary'])} chars)"


def test_mode_identifiers_valid_for_cpp_sv():
    data = json.loads(FB_JSON.read_text(encoding="utf-8"))
    pattern = re.compile(r"^[A-Z][A-Za-z0-9_]*$")
    for block in data["blocks"]:
        for feat in block["features"]:
            for mode in feat.get("modes", []):
                assert pattern.match(mode), f"mode {mode!r} not valid identifier"


def test_xref_packet_fields_exist():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    pkt = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_packet.json")
    issues = invariants.check_blocks_xref_packet(fb, pkt)
    err = [i for i in issues if i.severity == "ERROR"]
    assert not err, f"cross-ref errors to packet: {[i.message for i in err]}"


def test_xref_registers_exist():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    regs = loader.load_doc(SPEC_VALIDATE / "generated" / "ni_registers.json")
    issues = invariants.check_blocks_xref_registers(fb, regs)
    err = [i for i in issues if i.severity == "ERROR"]
    assert not err, f"cross-ref errors to registers: {[i.message for i in err]}"


def test_compile_time_params_unique_across_features():
    fb = json.loads(FB_JSON.read_text(encoding="utf-8"))
    issues = invariants.check_blocks_param_uniqueness(fb)
    err = [i for i in issues if i.severity == "ERROR"]
    assert not err, f"param uniqueness: {[i.message for i in err]}"


def test_constants_blocks_function_block_names():
    fb = loader.load_doc(FB_JSON)
    names = constants.blocks_function_block_names(fb)
    assert "NMU" in names and "NSU" in names


def test_constants_blocks_compile_time_params():
    fb = loader.load_doc(FB_JSON)
    params = constants.blocks_compile_time_params(fb)
    assert isinstance(params, dict)
    # Don't hard-code keys; just ensure structure correct
    for k, v in params.items():
        assert isinstance(k, str)


def test_constants_blocks_modes_of():
    """blocks_modes_of returns (feature_id, mode) tuples for a given block."""
    fb = loader.load_doc(FB_JSON)
    nmu_modes = constants.blocks_modes_of(fb, "NMU")
    assert isinstance(nmu_modes, list)
    # Find ROB modes — they should appear in the list
    rob_modes = [m for fid, m in nmu_modes if fid == "FEAT-NMU-ROB"]
    assert set(rob_modes) >= {"NoRoB", "SimpleRoB", "NormalRoB"}, \
        f"ROB modes incomplete: {rob_modes}"
    # NSU should also work
    nsu_modes = constants.blocks_modes_of(fb, "NSU")
    assert isinstance(nsu_modes, list)


def test_mode_enum_names_unique_after_block_prefix():
    """Mode enum names must be unique across all blocks once block prefix applied."""
    from ni_spec.loader import load_doc
    spec = load_doc(FB_JSON)

    names = []
    for block in spec["blocks"]:
        block_name = block["name"]  # "NMU" or "NSU"
        for feat in block["features"]:
            if not feat.get("modes"):
                continue
            # Derive enum name from feature id, then apply block prefix
            # FEAT-NMU-VC_ARB -> NMU_VC_ARBMode
            short = feat["id"].split("-")[-1]
            names.append(f"{block_name}_{short}Mode")

    assert len(names) == len(set(names)), \
        f"duplicate mode enum: {[n for n in names if names.count(n) > 1]}"


def test_l2_mode_enum_unique_check_fires_on_collision():
    """Synthetic test: artificial collision triggers the L2 error."""
    from ni_spec.invariants import check_mode_enum_name_unique
    synthetic = {
        "blocks": [
            {"name": "NMU", "features": [{"id": "FEAT-NMU-VC_ARB", "modes": ["RR"]}]},
            {"name": "NMU", "features": [{"id": "FEAT-NMU-VC_ARB", "modes": ["RR"]}]},
        ]
    }
    errors = check_mode_enum_name_unique(synthetic)
    assert any("collision" in e for e in errors)
