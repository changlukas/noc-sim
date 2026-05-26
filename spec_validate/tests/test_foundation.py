"""Foundation gate tests — Task 1."""
import pytest
from ni_spec import loader, constants


def test_load_spec_version_returns_string():
    """spec_version comes from spec/ni/VERSION single source."""
    v = loader.load_spec_version()
    assert isinstance(v, str)
    assert v.startswith("v")
    assert v.count(".") == 2  # semver


def test_constants_signals_stub_raises():
    """signals_* API is reserved for Task 2; calling now must NotImplementedError."""
    with pytest.raises(NotImplementedError, match="Task 2"):
        constants.signals_pin_names({})


def test_constants_regs_stub_raises():
    """regs_* API is reserved for Task 4."""
    with pytest.raises(NotImplementedError, match="Task 4"):
        constants.regs_offsets({})


def test_constants_blocks_stub_raises():
    """blocks_* API is reserved for Task 5."""
    with pytest.raises(NotImplementedError, match="Task 5"):
        constants.blocks_function_block_names({})
