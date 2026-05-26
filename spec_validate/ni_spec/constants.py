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
