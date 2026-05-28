"""Spec 萃取常數的純函式。

C-model 直接 import 這個模組即可取得所有 packet 寬度／bit 位置／enum 編碼，
不該再有任何手抄常數。C++ C-model 透過 codegen 從這層產生 ni_flit_constants.h。
"""

from __future__ import annotations
from typing import Tuple, Dict, Optional


def _int_params(packet_spec) -> dict:
    """parameters[] 中 type=int 的 name→default。"""
    return {p["name"]: p["default"]
            for p in packet_spec.get("parameters", [])
            if p["type"] == "int"}


def _resolved_field_widths(packet_spec) -> dict:
    """flit.field_widths 覆蓋 parameters[]，給寬度運算式做求值用。"""
    return {**_int_params(packet_spec), **packet_spec["flit"].get("field_widths", {})}


def flit_width(packet_spec) -> int:
    return packet_spec["flit"]["derived"]["FLIT_WIDTH"]


def header_width(packet_spec) -> int:
    return packet_spec["flit"]["derived"]["HEADER_WIDTH"]


def payload_width(packet_spec) -> int:
    return packet_spec["flit"]["derived"]["PAYLOAD_WIDTH"]


def link_width(packet_spec) -> int:
    return packet_spec["flit"]["derived"]["LINK_WIDTH"]


def header_field_pos(packet_spec, name: str) -> Tuple[Optional[int], Optional[int]]:
    """回 (lsb, msb)。找不到 raise KeyError。

    For width=0 reserved placeholder fields, returns (None, None) to signal
    the field is not bit-addressable in the current flit layout.
    """
    for f in packet_spec["flit"]["header_fields"]:
        if f["name"] == name:
            return (f["lsb"], f["msb"])
    raise KeyError(f"header field {name!r} 不存在")


def payload_field_pos(packet_spec, channel: str, name: str) -> Tuple[int, int]:
    for ch in packet_spec["flit"]["payload_channels"]:
        if ch["name"] == channel:
            for f in ch["fields"]:
                if f["name"] == name:
                    return (f["lsb"], f["msb"])
            raise KeyError(f"payload field {channel}/{name} 不存在")
    raise KeyError(f"payload channel {channel!r} 不存在")


def all_header_fields(packet_spec) -> Dict[str, Tuple[Optional[int], Optional[int]]]:
    """Return {name: (lsb, msb)} for all header fields.

    Width=0 reserved placeholder fields have (None, None) as lsb/msb.
    """
    return {f["name"]: (f["lsb"], f["msb"]) for f in packet_spec["flit"]["header_fields"]}


def all_field_widths(packet_spec) -> Dict[str, int]:
    """所有 field_widths 解析後的寬度，給 C-model packer 用。"""
    return dict(_resolved_field_widths(packet_spec))


def header_field_enabled(packet_spec, field_name: str) -> bool:
    """Return whether header field is functional (True) or padding (False).

    A field is functional when its ``enabled`` property is True (the default).
    Padding fields are currently stubbed to 0 and not driven by hardware.
    """
    for f in packet_spec["flit"]["header_fields"]:
        if f["name"] == field_name:
            return f.get("enabled", True)
    raise KeyError(f"header field {field_name!r} not in spec")


def header_fields_padding(packet_spec) -> list:
    """Return list of field names marked enabled=false (padding/stubbed)."""
    return [f["name"] for f in packet_spec["flit"]["header_fields"]
            if not f.get("enabled", True)]


def axi_channel_encoding(packet_spec) -> Dict[str, int]:
    """axi_ch 欄位的 {channel_name: value}。沒有 encoding 欄位則回 {}。"""
    for f in packet_spec["flit"]["header_fields"]:
        if f["name"] == "axi_ch" and "encoding" in f:
            return {name: int(v) for v, name in f["encoding"].items()}
    return {}


def field_encoding(packet_spec, field_name: str) -> Dict[str, int]:
    """通用：任何 header field 上的 encoding 表，{name: value}。"""
    for f in packet_spec["flit"]["header_fields"]:
        if f["name"] == field_name and "encoding" in f:
            return {name: int(v) for v, name in f["encoding"].items()}
    return {}


# ---------- signals domain ----------

def signals_pin_names(signals_spec) -> list:
    """Return list of all non-null pin_name across all signals."""
    out = []
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                if sig.get("pin_name"):
                    out.append(sig["pin_name"])
        for sig in iface.get("signals", []):
            if sig.get("pin_name"):
                out.append(sig["pin_name"])
    return out


def signals_reset_domains(signals_spec) -> set:
    """Return set of legal reset signal names from meta.reset_signals[]."""
    return set(signals_spec.get("meta", {}).get("reset_signals", []))


def signals_signal_by_pin(signals_spec, pin_name: str) -> dict:
    """Lookup signal entry by RTL-level pin_name. Returns None if not found."""
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                if sig.get("pin_name") == pin_name:
                    return sig
        for sig in iface.get("signals", []):
            if sig.get("pin_name") == pin_name:
                return sig
    return None


def signals_pins_by_interface(signals_spec) -> dict:
    """Return {interface_name: [signal_dict, ...]} for pin-bundle elaboration.

    Each entry exposes the fields a C++/SV emitter needs to materialise an
    interface bundle struct: pin_name, direction, width (numeric default
    or ``width_expr``), and reset_behavior. Direction for channeled
    signals is inherited from the channel; interface-level (NoC link)
    signals carry their own ``direction`` field.
    """
    out: dict = {}
    for iface in signals_spec.get("interfaces", []):
        name = iface["name"]
        pins: list = []
        for ch in iface.get("channels", []):
            ch_dir = ch.get("direction")
            for sig in ch.get("signals", []):
                pins.append({
                    "pin_name":       sig["pin_name"],
                    "direction":      sig.get("direction") or ch_dir,
                    "width_expr":     sig.get("width_expr") or sig.get("default") or "1",
                    "reset_behavior": sig.get("reset_behavior"),
                })
        for sig in iface.get("signals", []):
            pins.append({
                "pin_name":       sig["pin_name"],
                "direction":      sig.get("direction"),
                "width_expr":     sig.get("width_expr") or sig.get("default") or "1",
                "reset_behavior": sig.get("reset_behavior"),
            })
        out[name] = pins
    return out


# ---------- registers domain (Task 4 will implement) ----------

def regs_offsets(regs_spec) -> dict:
    """Return {register_name: offset_int} for kind=register entries."""
    return {r["name"]: int(r["offset"], 16)
            for r in regs_spec.get("registers", [])
            if r.get("kind") == "register"}


def regs_field_mask(regs_spec, reg_name: str, field_name: str) -> int:
    """Return bit mask for a register field. Raises KeyError if not found."""
    for r in regs_spec.get("registers", []):
        if r.get("name") != reg_name:
            continue
        for f in r.get("fields", []):
            if f.get("name") == field_name:
                hi, lo = int(f["bit_high"]), int(f["bit_low"])
                return ((1 << (hi - lo + 1)) - 1) << lo
    raise KeyError(f"{reg_name}.{field_name}")


def regs_access_mode(regs_spec, reg_name: str) -> str:
    """Return access mode (RO/RW/RW1C/WO/WC) for a register. Raises KeyError if not found."""
    for r in regs_spec.get("registers", []):
        if r.get("kind") != "register":
            continue
        if r.get("name") == reg_name:
            return r.get("access")
    raise KeyError(reg_name)


# ---------- pure-parameterization elaborator helpers (PP-2) ----------
#
# These compute the same values currently stored as `derived` / `lsb` / `msb`
# in ni_packet.json. They are introduced here so codegen + tests can switch
# to them in subsequent tasks (PP-3+), at which point the stored fields
# can be removed from the JSON entirely (PP-6).

import ast as _ast
from typing import Mapping as _Mapping
from .exceptions import (
    ExprSyntaxError, ExprNameError, ExprNotAllowedError, FieldNotFoundError,
)


_ALLOWED_BINOPS = {_ast.Add, _ast.Sub, _ast.Mult, _ast.FloorDiv, _ast.Mod}
_ALLOWED_UNARYOPS = {_ast.UAdd, _ast.USub}


def _eval_ast(node, namespace: _Mapping[str, int]) -> int:
    if isinstance(node, _ast.Constant):
        if isinstance(node.value, int):
            return node.value
        raise ExprNotAllowedError(f"only integer literals allowed, got {type(node.value).__name__}")
    if isinstance(node, _ast.Name):
        if node.id in namespace:
            return int(namespace[node.id])
        raise ExprNameError(f"symbol '{node.id}' not found in namespace")
    if isinstance(node, _ast.BinOp):
        if type(node.op) not in _ALLOWED_BINOPS:
            raise ExprNotAllowedError(f"forbidden binop {type(node.op).__name__}")
        l = _eval_ast(node.left,  namespace)
        r = _eval_ast(node.right, namespace)
        op_map = {_ast.Add: int.__add__, _ast.Sub: int.__sub__, _ast.Mult: int.__mul__,
                  _ast.FloorDiv: int.__floordiv__, _ast.Mod: int.__mod__}
        return op_map[type(node.op)](l, r)
    if isinstance(node, _ast.UnaryOp):
        if type(node.op) not in _ALLOWED_UNARYOPS:
            raise ExprNotAllowedError(f"forbidden unaryop {type(node.op).__name__}")
        v = _eval_ast(node.operand, namespace)
        return v if isinstance(node.op, _ast.UAdd) else -v
    raise ExprNotAllowedError(f"forbidden ast node {type(node).__name__}")


def packet_eval_expr(spec: dict, expr) -> int:
    """Evaluate a width_param expression in the packet field_widths namespace.

    Handles:
      - integer literal (returned as-is)
      - the special string "derived" -> caller must handle this case
        before calling packet_eval_expr (raises ExprNotAllowedError otherwise)
      - any other string: parsed with ast and walked with allowlist
    """
    if isinstance(expr, int):
        return expr
    if expr == "derived":
        raise ExprNotAllowedError(
            "width_param='derived' must be resolved by payload_field_width, not packet_eval_expr"
        )
    if not isinstance(expr, str):
        raise ExprSyntaxError(f"width_param must be str or int, got {type(expr).__name__}")
    try:
        tree = _ast.parse(expr, mode="eval")
    except SyntaxError as e:
        raise ExprSyntaxError(f"cannot parse '{expr}': {e}") from e
    namespace = spec.get("flit", {}).get("field_widths", {})
    return _eval_ast(tree.body, namespace)


def packet_param_value(spec: dict, name: str) -> int:
    """Look up a parameter in flit.field_widths."""
    fw = spec.get("flit", {}).get("field_widths", {})
    if name not in fw:
        raise ExprNameError(f"parameter '{name}' not in field_widths")
    return int(fw[name])


def _find_header_field(spec: dict, name: str) -> dict:
    for f in spec["flit"]["header_fields"]:
        if f["name"] == name:
            return f
    raise FieldNotFoundError(f"header field '{name}' not found")


def header_field_width(spec: dict, name: str) -> int:
    """Resolve width by evaluating width_param against field_widths."""
    f = _find_header_field(spec, name)
    return packet_eval_expr(spec, f["width_param"])


def header_field_position(spec: dict, name: str):
    """(lsb, msb) computed cumulatively in declaration order.
    Returns None for width-0 placeholders."""
    cumulative = 0
    for f in spec["flit"]["header_fields"]:
        w = header_field_width(spec, f["name"])
        if f["name"] == name:
            return None if w == 0 else (cumulative, cumulative + w - 1)
        cumulative += w
    raise FieldNotFoundError(f"header field '{name}' not found")


def _find_channel(spec: dict, channel: str) -> dict:
    for ch in spec["flit"]["payload_channels"]:
        if ch["name"] == channel:
            return ch
    raise FieldNotFoundError(f"channel '{channel}' not found")


def payload_channel_width(spec: dict, channel: str) -> int:
    """Authored channel-level metadata."""
    return int(_find_channel(spec, channel)["payload_width"])


def payload_field_width(spec: dict, channel: str, name: str) -> int:
    """Resolve width. Special case: width_param='derived' ->
    payload_width(channel) - sum of all other fields' widths.

    At most one field per channel may declare width_param='derived'.
    """
    ch = _find_channel(spec, channel)
    derived_count = sum(1 for f in ch["fields"] if f["width_param"] == "derived")
    if derived_count > 1:
        raise ExprNotAllowedError(
            f"channel '{channel}' has multiple 'derived' fields; "
            f"only one allowed per channel"
        )
    target = None
    others_sum = 0
    for f in ch["fields"]:
        if f["name"] == name:
            target = f
            continue
        wp = f["width_param"]
        if wp == "derived":
            # The channel's lone derived field contributes the remainder; it
            # cannot itself appear in the running sum used to compute that
            # remainder. Skip it here.
            continue
        others_sum += packet_eval_expr(spec, wp)
    if target is None:
        raise FieldNotFoundError(f"payload field '{name}' not in channel '{channel}'")
    if target["width_param"] == "derived":
        return payload_channel_width(spec, channel) - others_sum
    return packet_eval_expr(spec, target["width_param"])


def payload_field_position(spec: dict, channel: str, name: str):
    """(lsb, msb) within the channel's payload, cumulative declaration order."""
    ch = _find_channel(spec, channel)
    cumulative = 0
    for f in ch["fields"]:
        w = payload_field_width(spec, channel, f["name"])
        if f["name"] == name:
            return None if w == 0 else (cumulative, cumulative + w - 1)
        cumulative += w
    raise FieldNotFoundError(f"payload field '{name}' not in channel '{channel}'")


# ---------- derived totals (computed from helpers above) ----------
#
# These intentionally use distinct names (`*_resolved`) from the legacy thin
# getters at the top of this file so that PP-2 does NOT change current
# generator output. The legacy `flit_width` / `header_width` /
# `payload_width` / `link_width` still read from `flit.derived`. PP-3
# switches the generators to call these resolved variants and re-baselines
# goldens; PP-6 drops the legacy getters once the JSON resolved fields are
# removed.

def header_width_resolved(spec: dict) -> int:
    """Sum of all header field widths (regardless of enabled)."""
    return sum(header_field_width(spec, f["name"])
               for f in spec["flit"]["header_fields"])


def payload_width_resolved(spec: dict) -> int:
    """Max of all payload_channels' payload_width (channels are union-typed
    by axi_ch encoding; flit allocates max channel width)."""
    return max(payload_channel_width(spec, ch["name"])
               for ch in spec["flit"]["payload_channels"])


def flit_width_resolved(spec: dict) -> int:
    return header_width_resolved(spec) + payload_width_resolved(spec)


def link_width_resolved(spec: dict) -> int:
    """LINK_WIDTH = FLIT_WIDTH + 1 (valid signal). Per spec/ni/doc/packet_format.md:112."""
    return flit_width_resolved(spec) + 1


def flit_data_width_resolved(spec: dict) -> int:
    """FLIT_DATA_WIDTH = HEADER_WIDTH - FLIT_ECC_WIDTH + PAYLOAD_WIDTH"""
    fw = spec.get("flit", {}).get("field_widths", {})
    ecc_w = int(fw.get("FLIT_ECC_WIDTH", 0))
    return header_width_resolved(spec) - ecc_w + payload_width_resolved(spec)


def header_data_width_resolved(spec: dict) -> int:
    """HEADER_DATA_WIDTH = HEADER_WIDTH - FLIT_ECC_WIDTH"""
    fw = spec.get("flit", {}).get("field_widths", {})
    return header_width_resolved(spec) - int(fw.get("FLIT_ECC_WIDTH", 0))


def wstrb_width_resolved(spec: dict) -> int:
    """WSTRB_WIDTH = NOC_DATA_WIDTH / 8"""
    fw = spec.get("flit", {}).get("field_widths", {})
    return int(fw.get("NOC_DATA_WIDTH", 0)) // 8

