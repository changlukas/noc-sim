"""Foundation gate tests — Task 1."""
from ni_spec import loader


def test_load_spec_version_returns_string():
    """spec_version comes from spec/ni/VERSION single source."""
    v = loader.load_spec_version()
    assert isinstance(v, str)
    assert v.startswith("v")
    assert v.count(".") == 2  # semver
