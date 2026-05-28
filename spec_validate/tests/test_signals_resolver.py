"""Unit tests for signals-domain resolvers (PP-7).

Signals reference symbols from two namespaces:
  - the interface's port_parameters (local: NUM_VC, ENABLE_AXI_PARITY)
  - packet domain (cross-domain: FLIT_WIDTH, AXI_*_WIDTH, ...)

Tests pull real interface/pin names from generated/ni_signals.json to
keep the suite aligned with the authored spec.
"""
from __future__ import annotations
import pytest
from pathlib import Path
from ni_spec.loader import load_doc
from ni_spec import constants as C
from ni_spec.exceptions import ExprNameError, FieldNotFoundError

SPEC_VALIDATE = Path(__file__).resolve().parent.parent


@pytest.fixture(scope="module")
def signals_spec():
    return load_doc(SPEC_VALIDATE / "generated" / "ni_signals.json")


@pytest.fixture(scope="module")
def packet_spec():
    return load_doc(SPEC_VALIDATE / "generated" / "ni_packet.json")


# -- accessors -----------------------------------------------------

def test_signal_interfaces_lists_all_in_order(signals_spec):
    ifaces = C.signal_interfaces(signals_spec)
    # All 7 NI top-level interfaces present, in JSON declaration order.
    assert ifaces == [
        "AXI_SLAVE_PORT", "NOC_REQ_OUT", "NOC_RSP_IN", "CSR",
        "NOC_REQ_IN", "AXI_MASTER_PORT", "NOC_RSP_OUT",
    ]


def test_signal_interface_pins_handles_channeled_iface(signals_spec):
    """AXI_SLAVE_PORT pins live under channels[].signals[]; resolver must flatten."""
    pins = C.signal_interface_pins(signals_spec, "AXI_SLAVE_PORT")
    assert len(pins) > 0
    pin_names = {p["pin_name"] for p in pins}
    # AW channel sample
    assert "axi_awid_i"   in pin_names
    assert "axi_awaddr_i" in pin_names
    # W channel sample (proves multi-channel flattening)
    assert "axi_wdata_i"  in pin_names


def test_signal_interface_pins_handles_direct_iface(signals_spec):
    """NOC_REQ_OUT has direct signals[] (no channels)."""
    pins = C.signal_interface_pins(signals_spec, "NOC_REQ_OUT")
    pin_names = {p["pin_name"] for p in pins}
    assert pin_names == {"noc_req_valid_o", "noc_req_flit_o", "noc_req_credit_i"}


def test_signal_interface_pins_unknown_iface_raises(signals_spec):
    with pytest.raises(FieldNotFoundError):
        C.signal_interface_pins(signals_spec, "BOGUS_IFACE_NAME")


# -- pin width resolution -----------------------------------------

def test_pin_width_from_packet_field_widths(signals_spec, packet_spec):
    """axi_awid_i.width_param = AXI_ID_WIDTH; resolved from packet field_widths."""
    expected = packet_spec["flit"]["field_widths"]["AXI_ID_WIDTH"]
    actual = C.signal_pin_width(signals_spec, packet_spec,
                                "AXI_SLAVE_PORT", "axi_awid_i")
    assert actual == expected


def test_pin_width_cross_domain_flit_width(signals_spec, packet_spec):
    """noc_req_flit_o.width_param = FLIT_WIDTH — the cross-domain edge.

    FLIT_WIDTH is NOT in packet.flit.field_widths (PP-6 removed it as a
    stored value). The resolver must compute it via flit_width_resolved.
    """
    expected = C.flit_width_resolved(packet_spec)
    actual = C.signal_pin_width(signals_spec, packet_spec,
                                "NOC_REQ_OUT", "noc_req_flit_o")
    assert actual == expected
    # Sanity: this is the real composed width, not just the legacy default.
    assert actual == 402


def test_pin_width_from_interface_port_parameter(signals_spec, packet_spec):
    """noc_req_credit_i.width_param = NUM_VC; NUM_VC lives in port_parameters.

    NUM_VC is interface-local — not in packet field_widths. Tests namespace
    priority #3 (interface scope).
    """
    actual = C.signal_pin_width(signals_spec, packet_spec,
                                "NOC_REQ_OUT", "noc_req_credit_i")
    assert actual == 1  # NUM_VC default = 1


def test_pin_width_no_width_param_falls_back_to_default(signals_spec, packet_spec):
    """noc_req_valid_o has width_param=null; resolver returns stored default."""
    actual = C.signal_pin_width(signals_spec, packet_spec,
                                "NOC_REQ_OUT", "noc_req_valid_o")
    assert actual == 1


def test_pin_width_unknown_pin_raises(signals_spec, packet_spec):
    with pytest.raises(FieldNotFoundError):
        C.signal_pin_width(signals_spec, packet_spec,
                           "AXI_SLAVE_PORT", "definitely_not_a_pin_xyz")


def test_signal_eval_expr_bogus_symbol_raises(signals_spec, packet_spec):
    with pytest.raises(ExprNameError):
        C.signal_eval_expr(signals_spec, packet_spec,
                           "NOC_REQ_OUT", "BOGUS_UNKNOWN_SYMBOL")


def test_signal_eval_expr_arithmetic(signals_spec, packet_spec):
    """Arithmetic over packet symbols composes in signals namespace."""
    # AXI_ID_WIDTH + AXI_LEN_WIDTH = 8 + 8 = 16
    expected = (packet_spec["flit"]["field_widths"]["AXI_ID_WIDTH"]
                + packet_spec["flit"]["field_widths"]["AXI_LEN_WIDTH"])
    actual = C.signal_eval_expr(signals_spec, packet_spec,
                                "AXI_SLAVE_PORT", "AXI_ID_WIDTH + AXI_LEN_WIDTH")
    assert actual == expected


# -- transition guard: stored defaults still consistent ------------

def test_resolved_widths_match_stored_defaults_where_resolvable(
        signals_spec, packet_spec):
    """Transition guard: whenever the resolver succeeds, it must agree with
    the stored default.

    Some AXI symbols (AXI_QOS_WIDTH, AXI_DATA_WIDTH, AXI_*USER_WIDTH, ...)
    are referenced by signals but not yet defined in packet field_widths;
    those pins raise ExprNameError, which is the correct behaviour. PP-9
    will close that gap (either by adding the symbols to packet spec or
    by introducing interface-local parameters). For now we assert
    consistency only on the subset the namespace can answer.
    """
    mismatches = []
    skipped_for_unknown_symbol = 0
    checked = 0
    for iface_name in C.signal_interfaces(signals_spec):
        for sig in C.signal_interface_pins(signals_spec, iface_name):
            pin = sig.get("pin_name")
            if not pin:
                continue
            stored = sig.get("default")
            if stored is None:
                continue
            try:
                resolved = C.signal_pin_width(signals_spec, packet_spec,
                                              iface_name, pin)
            except ExprNameError:
                skipped_for_unknown_symbol += 1
                continue
            checked += 1
            if resolved != int(stored):
                mismatches.append(
                    f"{iface_name}/{pin}: stored={stored} resolved={resolved}"
                )
    assert not mismatches, "stored/resolved drift:\n  " + "\n  ".join(mismatches)
    # Sanity: we actually verified non-trivially many pins.
    assert checked >= 20, f"transition guard only checked {checked} pins"
