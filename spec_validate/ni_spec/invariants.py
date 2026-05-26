"""Layer 1 + Layer 2 校驗。每個 check 回傳 Issue list，不 print。

Layer 2 不變量同時是 C-model packer 的 runtime assertion 來源：
- width consistency: packer 在打包時必須保證 width == msb-lsb+1
- tile_check: header / payload 不能有 bit hole 或重疊
- SECDED bound: ECC 寬度必須滿足 Hamming inequality
"""

from __future__ import annotations
from dataclasses import dataclass
from typing import List, Optional


@dataclass
class Issue:
    severity: str  # "ERROR" or "WARN"
    check: str
    message: str


def _err(check, msg) -> Issue: return Issue("ERROR", check, msg)
def _warn(check, msg) -> Issue: return Issue("WARN", check, msg)


def _safe_eval(expr, ns):
    if isinstance(expr, (int, float)):
        return expr
    return eval(str(expr), {"__builtins__": {}}, dict(ns))


def _tile_check(check, scope, fields, total, allow_gap=False) -> List[Issue]:
    issues: List[Issue] = []
    occupied = {}
    for f in fields:
        name, lo, hi = f["name"], f["lsb"], f["msb"]
        if hi < lo:
            issues.append(_err(check, f"{scope}: 欄位 '{name}' msb({hi}) < lsb({lo})"))
            continue
        for b in range(lo, hi + 1):
            if b in occupied:
                issues.append(_err(check, f"{scope}: bit {b} 被 '{name}' 與 '{occupied[b]}' 重疊"))
            occupied[b] = name
    if not occupied:
        return issues
    top = max(occupied)
    if top != total - 1:
        issues.append(_err(check, f"{scope}: 最高位 bit{top} 與宣告寬度 {total} 不符 (應為 bit{total-1})"))
    if not allow_gap:
        for b in range(total):
            if b not in occupied:
                issues.append(_err(check, f"{scope}: bit {b} 未被任何欄位覆蓋"))
    return issues


def _width_consistency(check, scope, fields) -> List[Issue]:
    issues: List[Issue] = []
    for f in fields:
        if "width" not in f:
            continue
        span = f["msb"] - f["lsb"] + 1
        if span != f["width"]:
            issues.append(_err(check, f"{scope}: '{f['name']}' width={f['width']} "
                                      f"但 [{f['msb']}:{f['lsb']}] 實佔 {span} bits"))
    return issues


def _resolve_width_param(packet_spec, wp):
    """把 width_param（如 'X_WIDTH + Y_WIDTH'）解析成 int，靠 flit.field_widths 當 namespace。"""
    if wp in (None, "derived"):
        return None
    ns = packet_spec["flit"].get("field_widths", {})
    try:
        return _safe_eval(wp, ns)
    except Exception:
        return None


def check_schema(packet_spec, packet_schema) -> List[Issue]:
    """Layer 1。"""
    if packet_schema is None:
        return [_warn("L1-SCHEMA", "schema 未提供，略過 Layer 1")]
    import jsonschema
    validator = jsonschema.Draft202012Validator(packet_schema)
    issues = []
    for e in sorted(validator.iter_errors(packet_spec), key=lambda e: list(e.absolute_path)):
        loc = "/".join(str(p) for p in e.absolute_path) or "(root)"
        issues.append(_err("L1-SCHEMA", f"{loc}: {e.message}"))
    return issues


def check_flit_arithmetic(packet_spec) -> List[Issue]:
    C = "L2-FLIT"
    issues: List[Issue] = []
    flit = packet_spec["flit"]
    # Path B: spec 不再有 parameters[]，全部寬度從 flit.field_widths 來
    params = dict(flit.get("field_widths", {}))

    hdr = flit["header_fields"]
    issues += _width_consistency(C, "header", hdr)
    declared_hw = flit["derived"].get("HEADER_WIDTH")
    if declared_hw:
        issues += _tile_check(C, "header", hdr, declared_hw)
    hw_sum = sum(f["width"] for f in hdr)

    for f in hdr:
        rv = _resolve_width_param(packet_spec, f.get("width_param"))
        if rv is not None and rv != f["width"]:
            issues.append(_err(C, f"header '{f['name']}': width={f['width']} 與 "
                                  f"width_param '{f['width_param']}' 解析值 {rv} 不一致"))

    chan_widths = {}
    for ch in flit["payload_channels"]:
        scope = f"payload[{ch['name']}]"
        pw = ch["payload_width"]
        chan_widths[ch["name"]] = pw
        issues += _width_consistency(C, scope, ch["fields"])
        issues += _tile_check(C, scope, ch["fields"], pw)
        if sum(f["width"] for f in ch["fields"]) != pw:
            issues.append(_err(C, f"{scope}: 欄位寬度總和 != payload_width {pw}"))
        for f in ch["fields"]:
            rv = _resolve_width_param(packet_spec, f.get("width_param"))
            if rv is not None and rv != f["width"]:
                issues.append(_err(C, f"{scope} '{f['name']}': width={f['width']} 與 "
                                      f"'{f['width_param']}' 解析值 {rv} 不一致"))

    d = flit["derived"]
    ecc = params.get("FLIT_ECC_WIDTH")
    noc_dw = params.get("NOC_DATA_WIDTH")
    payload_w = max(chan_widths.values()) if chan_widths else None

    def expect(name, computed):
        if name in d and computed is not None and d[name] != computed:
            issues.append(_err(C, f"derived.{name}={d[name]} 但由 spec 重算應為 {computed}"))
        return computed

    expect("HEADER_WIDTH", hw_sum)
    expect("PAYLOAD_WIDTH", payload_w)
    flit_w = expect("FLIT_WIDTH", hw_sum + payload_w if payload_w is not None else None)
    hdr_data = expect("HEADER_DATA_WIDTH", hw_sum - ecc if ecc is not None else None)
    flit_data = expect("FLIT_DATA_WIDTH",
                       (hdr_data + payload_w) if (hdr_data is not None and payload_w is not None) else None)
    expect("LINK_WIDTH", flit_w + 1 if flit_w is not None else None)
    expect("WSTRB_WIDTH", noc_dw // 8 if noc_dw else None)

    if ecc is not None and flit_data is not None:
        lhs = 2 ** (ecc - 1)
        rhs = flit_data + ecc + 1
        if lhs < rhs:
            issues.append(_err(C, f"SECDED bound 不成立: 2^({ecc}-1)={lhs} < "
                                  f"FLIT_DATA_WIDTH+ECC+1={rhs}"))
        else:
            margin = ecc
            while 2 ** (margin - 2) >= flit_data + (margin - 1) + 1:
                margin -= 1
            if margin < ecc:
                issues.append(_warn(C, f"FLIT_ECC_WIDTH={ecc} 偏保守，理論最小值為 {margin}"))

    hdr_names = {f["name"] for f in hdr}
    for cov in flit.get("route_par_coverage", []):
        if cov not in hdr_names:
            issues.append(_err(C, f"route_par_coverage 參照 '{cov}' 不存在於 header"))

    return issues


def check_signals_reset_domains(signals_spec) -> List[Issue]:
    """L2: every signal's reset_behavior.domain must be in meta.reset_signals."""
    issues: List[Issue] = []
    legal = set(signals_spec.get("meta", {}).get("reset_signals", []))
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                _check_one_reset(sig, legal, issues)
        for sig in iface.get("signals", []):
            _check_one_reset(sig, legal, issues)
    return issues


def _check_one_reset(sig: dict, legal_domains: set, issues: List[Issue]) -> None:
    rb = sig.get("reset_behavior")
    if rb is None:
        issues.append(_err("L2-SIG-RST", f"signal {sig.get('pin_name')} missing reset_behavior"))
        return
    if rb.get("kind") == "external_driven":
        if "value" in rb:
            issues.append(_err("L2-SIG-RST",
                f"signal {sig.get('pin_name')}: external_driven must not carry value"))
        return
    domain = rb.get("domain")
    if not domain:
        issues.append(_err("L2-SIG-RST",
            f"signal {sig.get('pin_name')}: non-external_driven must specify domain"))
    elif domain not in legal_domains:
        issues.append(_err("L2-SIG-RST",
            f"signal {sig.get('pin_name')}: reset domain {domain!r} not in meta.reset_signals"))


def check_signals_pin_uniqueness(signals_spec) -> List[Issue]:
    """L2: every signal must have a non-null, unique pin_name."""
    issues: List[Issue] = []
    seen: dict = {}
    for iface in signals_spec.get("interfaces", []):
        for ch in iface.get("channels", []):
            for sig in ch.get("signals", []):
                _check_pin_unique(sig, seen, issues)
        for sig in iface.get("signals", []):
            _check_pin_unique(sig, seen, issues)
    return issues


def _check_pin_unique(sig: dict, seen: dict, issues: List[Issue]) -> None:
    pin = sig.get("pin_name")
    if pin is None:
        issues.append(_err("L2-SIG-PIN", f"signal (pin_name=null) has null pin_name"))
        return
    if pin in seen:
        issues.append(_err("L2-SIG-PIN",
            f"pin_name {pin!r} duplicated (also seen at {seen[pin]})"))
    else:
        seen[pin] = pin


def check_csr_offset_alignment(regs_spec) -> List[Issue]:
    """L2: offset must be 4-byte aligned (32-bit registers)."""
    issues: List[Issue] = []
    for r in regs_spec.get("registers", []):
        if r.get("kind") != "register":
            continue
        try:
            ofs = int(r["offset"], 16)
        except (ValueError, KeyError):
            continue
        if ofs % 4 != 0:
            issues.append(_err("L2-REG-ALIGN",
                f"{r.get('name')}: offset {r['offset']} not 4-byte aligned"))
    return issues


def check_csr_offset_unique(regs_spec) -> List[Issue]:
    """L2: no two registers share an offset."""
    issues: List[Issue] = []
    seen: dict = {}
    for r in regs_spec.get("registers", []):
        ofs = r.get("offset")
        if ofs is None:
            continue
        if ofs in seen:
            issues.append(_err("L2-REG-OFS",
                f"offset {ofs} duplicated ({seen[ofs]} and {r.get('name')})"))
        else:
            seen[ofs] = r.get("name")
    return issues


def check_field_bit_tiling(regs_spec) -> List[Issue]:
    """L2: bit ranges within each register must not overlap."""
    issues: List[Issue] = []
    for r in regs_spec.get("registers", []):
        used: dict = {}
        for f in r.get("fields", []):
            try:
                hi, lo = int(f["bit_high"]), int(f["bit_low"])
            except (KeyError, ValueError, TypeError):
                continue
            if hi < lo:
                issues.append(_err("L2-REG-TILE",
                    f"{r.get('name')}.{f.get('name')}: bit_high {hi} < bit_low {lo}"))
                continue
            for b in range(lo, hi + 1):
                if b in used:
                    issues.append(_err("L2-REG-TILE",
                        f"{r.get('name')}: field {f.get('name')} bit {b} overlaps with {used[b]}"))
                else:
                    used[b] = f.get("name")
    return issues


def check_reset_in_data_width(regs_spec, data_width: int = 32) -> List[Issue]:
    """L2: reset_expr literal (when integer) must fit in data_width bits."""
    issues: List[Issue] = []
    for r in regs_spec.get("registers", []):
        if r.get("kind") != "register":
            continue
        rst = r.get("reset_expr")
        if not rst:
            continue
        try:
            val = int(rst, 0)
        except (ValueError, TypeError):
            continue  # symbolic reset (e.g. param expr) is fine for L2
        if val >= (1 << data_width):
            issues.append(_err("L2-REG-RESET",
                f"{r.get('name')}: reset {rst} exceeds {data_width}-bit width"))
    return issues


def check_blocks_xref_packet(fb_spec, pkt_spec) -> List[Issue]:
    """L2: every uses_packet_fields entry must exist in ni_packet.json."""
    issues: List[Issue] = []
    legal = {f["name"] for f in pkt_spec["flit"]["header_fields"]}
    legal |= {c["name"] for c in pkt_spec["flit"]["payload_channels"]}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for ref in feat.get("uses_packet_fields", []):
                if ref not in legal:
                    issues.append(_err("L2-FB-XREF-PKT",
                        f"{feat['id']}: uses_packet_fields {ref!r} not in ni_packet.json"))
    return issues


def check_blocks_xref_registers(fb_spec, regs_spec) -> List[Issue]:
    """L2: every configured_by entry's register name must exist in ni_registers.json."""
    issues: List[Issue] = []
    legal = {r["name"] for r in regs_spec.get("registers", []) if r.get("kind") == "register"}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for ref in feat.get("configured_by", []):
                # configured_by may include "REG.field" form — match register name part
                reg_name = ref.split(".")[0]
                if reg_name not in legal:
                    issues.append(_err("L2-FB-XREF-REG",
                        f"{feat['id']}: configured_by {ref!r} register not in ni_registers.json"))
    return issues


def check_blocks_param_uniqueness(fb_spec) -> List[Issue]:
    """L2: compile_time_params name must be unique across all features."""
    issues: List[Issue] = []
    seen: dict = {}
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            for pname in feat.get("compile_time_params", {}):
                if pname in seen:
                    issues.append(_err("L2-FB-PARAM",
                        f"{pname!r} defined in both {seen[pname]} and {feat['id']}"))
                else:
                    seen[pname] = feat["id"]
    return issues


def check_blocks_related_features_symmetric(fb_spec) -> List[Issue]:
    """L2: if A.related_features contains B, then B.related_features should contain A.
    Issues WARN (not ERROR) since one-way pointers may be intentional."""
    issues: List[Issue] = []
    all_feats: dict = {}  # id -> related_features set
    for block in fb_spec.get("blocks", []):
        for feat in block.get("features", []):
            all_feats[feat["id"]] = set(feat.get("related_features", []))
    for fid, refs in all_feats.items():
        for ref in refs:
            if ref not in all_feats:
                issues.append(_err("L2-FB-REL",
                    f"{fid}: related_features {ref!r} doesn't exist"))
                continue
            if fid not in all_feats[ref]:
                issues.append(_warn("L2-FB-REL",
                    f"{fid} -> {ref} is one-way (not symmetric)"))
    return issues


def check_all(bundle, md_dir: Optional[str] = None) -> List[Issue]:
    """Path B：跑 Layer 1 (schema) + Layer 2 (arithmetic)。

    Layer 3 (cross-check) 在 Path B 下不再存在 — generator 把 MD 直接
    產成 JSON，沒有兩份要對拍。md_dir 參數保留簽名但已無作用。
    """
    issues: List[Issue] = []
    issues += check_schema(bundle.packet, bundle.packet_schema)
    issues += check_flit_arithmetic(bundle.packet)
    return issues
