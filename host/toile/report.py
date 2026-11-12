"""Output reports of the Toile model (CONTRACT.md section 7).

Three formats, frozen byte for byte and produced identically by the C core
``core/src/report.c``: ``text`` (default), ``json`` (one line) and ``md``
(audit).  The differential test compares them octet for octet.
"""

from __future__ import annotations

import json
from typing import Any

from .resource import Model
from .scanout import scanout_count
from .verdict import (MODEL_COUNTEREXAMPLE, TRANSFER_OK, Verdict, name,
                      severity)
from .vgproto import ID_NONE

#: Empty cell of the markdown table: en dash (U+2013).
DASH = "\u2013"

#: Prefix of the cause in the text payload (CONTRACT.md §7.1: "cause=<chaîne>").
CAUSE_PREFIX = "cause="

FORMATS = ("text", "json", "md")


def _rect_str(rect: tuple[int, int, int, int]) -> str:
    """Rectangle as ``x,y,w,h``."""
    x, y, w, h = rect
    return f"{x},{y},{w},{h}"


def _state(m: Model) -> dict[str, Any]:
    """State of the model as the reports see it (CONTRACT.md section 7)."""
    cursor = m.cursor
    return {
        "resources": m.resource_count(),
        "scanouts": scanout_count(m),
        "hash": f"{m.state_hash():016x}",
        "applied": m.cmds_applied,
        "rejected": m.cmds_rejected,
        "flushes": m.flushes,
        "bytes": m.bytes_transferred,
        "cursor": {
            "resource": cursor.resource_id,
            "scanout": cursor.scanout_id,
            "x": cursor.x,
            "y": cursor.y,
            "hot_x": cursor.hot_x,
            "hot_y": cursor.hot_y,
            "visible": 1 if cursor.visible else 0,
        },
    }


def _cursor_str(cursor: dict[str, Any]) -> str:
    """Cursor of the state line: resource, scanout, x, y, hot_x, hot_y."""
    return ",".join(str(cursor[key]) for key in
                    ("resource", "scanout", "x", "y", "hot_x", "hot_y"))


def _payload(v: Verdict, state: dict[str, Any]) -> str:
    """Payload of a verdict: the fields that apply, in the frozen order."""
    parts: list[str] = []
    if v.cmd_type != ID_NONE:
        parts.append(f"cmd=0x{v.cmd_type:04x}")
    if v.resource_id != ID_NONE:
        parts.append(f"resource={v.resource_id}")
    if v.scanout_id != ID_NONE:
        parts.append(f"scanout={v.scanout_id}")
    if v.code == TRANSFER_OK:
        parts.append(f"rect={_rect_str(v.rect)}")
    if v.cause:
        parts.append(f"{CAUSE_PREFIX}{v.cause}")
    if v.code == MODEL_COUNTEREXAMPLE:
        parts.append(f"state={state['resources']},{state['scanouts']}")
    return " ".join(parts)


def _text_line(index: int, v: Verdict, state: dict[str, Any]) -> str:
    """One verdict line: ``%04u %-26s %-7s`` then the payload, if any.

    The padding of the severity is dropped, exactly like the C core: the line
    carries no trailing space.
    """
    head = f"{index:04d} {name(v.code):<26} {severity(v.code)}"
    payload = _payload(v, state)
    return f"{head} {payload}" if payload else head


def report_text(m: Model, verdicts: list[Verdict], source: str) -> str:
    """Text report (CONTRACT.md section 7.1)."""
    state = _state(m)
    lines = ["toile v1 text"]
    lines.extend(_text_line(index, v, state)
                 for index, v in enumerate(verdicts))
    lines.append(
        "# state resources={r} scanouts={s} hash={h} applied={a} "
        "rejected={j} flushes={f} bytes={b} cursor={c}".format(
            r=state["resources"], s=state["scanouts"], h=state["hash"],
            a=state["applied"], j=state["rejected"], f=state["flushes"],
            b=state["bytes"], c=_cursor_str(state["cursor"])))
    return "\n".join(lines) + "\n"


def _verdict_json(index: int, v: Verdict) -> dict[str, Any]:
    """JSON object of one verdict; the fields that do not apply are absent."""
    out: dict[str, Any] = {
        "i": index,
        "code": name(v.code),
        "sev": severity(v.code),
    }
    if v.cmd_type != ID_NONE:
        out["cmd"] = f"0x{v.cmd_type:04x}"
    if v.resource_id != ID_NONE:
        out["resource"] = v.resource_id
    if v.scanout_id != ID_NONE:
        out["scanout"] = v.scanout_id
    if v.code == TRANSFER_OK:
        out["rect"] = _rect_str(v.rect)
    if v.cause:
        out["cause"] = v.cause
    return out


def report_json(m: Model, verdicts: list[Verdict], source: str) -> str:
    """JSON report (CONTRACT.md section 7.2).

    One line plus a newline, no space outside the strings, frozen key order.
    """
    doc = {
        "toile": "1",
        "state": _state(m),
        "verdicts": [_verdict_json(index, v)
                     for index, v in enumerate(verdicts)],
    }
    return json.dumps(doc, separators=(",", ":")) + "\n"


def _md_cell(text: str) -> str:
    """One cell of the markdown table; an empty cell is an en dash."""
    return text if text else DASH


def _md_row(index: int, v: Verdict) -> str:
    """One row of the markdown table."""
    cells = [
        f"{index:04d}",
        name(v.code),
        severity(v.code),
        f"0x{v.cmd_type:04x}" if v.cmd_type != ID_NONE else "",
        str(v.resource_id) if v.resource_id != ID_NONE else "",
        str(v.scanout_id) if v.scanout_id != ID_NONE else "",
        _rect_str(v.rect) if v.code == TRANSFER_OK else "",
        v.cause,
    ]
    return "| " + " | ".join(_md_cell(cell) for cell in cells) + " |"


def _counts(verdicts: list[Verdict]) -> tuple[int, int, int]:
    """Number of verdicts per severity: (errors, warnings, info)."""
    errors = warnings = info = 0
    for v in verdicts:
        level = severity(v.code)
        if level == "error":
            errors += 1
        elif level == "warning":
            warnings += 1
        else:
            info += 1
    return errors, warnings, info


def report_md(m: Model, verdicts: list[Verdict], source: str) -> str:
    """Markdown audit report (CONTRACT.md section 7.3)."""
    state = _state(m)
    errors, warnings, info = _counts(verdicts)
    lines = [
        "# Rapport d'audit Toile",
        "",
        f"- fixture : `{source}`",
        f"- verdicts : {len(verdicts)} (erreurs {errors}, "
        f"avertissements {warnings}, info {info})",
        f"- état : {state['resources']} ressources, "
        f"{state['scanouts']} scanouts, hash `{state['hash']}`",
        "",
        "| # | code | sévérité | cmd | ressource | scanout | rect | cause |",
        "|---|---|---|---|---|---|---|---|",
    ]
    lines.extend(_md_row(index, v) for index, v in enumerate(verdicts))
    return "\n".join(lines) + "\n"


def report(fmt: str, m: Model, verdicts: list[Verdict], source: str) -> str:
    """Report in the requested format; ValueError when the format is unknown."""
    if fmt == "text":
        return report_text(m, verdicts, source)
    if fmt == "json":
        return report_json(m, verdicts, source)
    if fmt == "md":
        return report_md(m, verdicts, source)
    raise ValueError(f"unknown report format: {fmt}")
