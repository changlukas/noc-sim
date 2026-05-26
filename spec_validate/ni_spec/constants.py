"""Spec 萃取常數的純函式。

C-model 直接 import 這個模組即可取得所有 packet 寬度／bit 位置／enum 編碼，
不該再有任何手抄常數。C++ C-model 透過 codegen 從這層產生 ni_flit_constants.h。
"""

from __future__ import annotations
from typing import Tuple, Dict


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


def header_field_pos(packet_spec, name: str) -> Tuple[int, int]:
    """回 (lsb, msb)。找不到 raise KeyError。"""
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


def all_header_fields(packet_spec) -> Dict[str, Tuple[int, int]]:
    return {f["name"]: (f["lsb"], f["msb"]) for f in packet_spec["flit"]["header_fields"]}


def all_field_widths(packet_spec) -> Dict[str, int]:
    """所有 field_widths 解析後的寬度，給 C-model packer 用。"""
    return dict(_resolved_field_widths(packet_spec))


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


# ---------- function blocks domain (Task 5 will implement) ----------

def blocks_function_block_names(blocks_spec) -> list:
    """Return list of FunctionBlock enum members (ROB, QOS, ...)."""
    raise NotImplementedError("Task 5")


def blocks_modes_of(blocks_spec, block_name: str) -> list:
    """Return list of mode enum members for a given function block."""
    raise NotImplementedError("Task 5")


def blocks_compile_time_params(blocks_spec) -> dict:
    """Return {param_name: int_value} across all features."""
    raise NotImplementedError("Task 5")
