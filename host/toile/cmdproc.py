"""Command processor of the Toile model (CONTRACT.md sections 5 and 6).

The driver applies the commands to the model, checks the invariants after each
one and stops at the first counter-example (fail fast).  The grammar of the
fixtures lives in ``toile.cmdproc_parse``; ``parse_line`` is re-exported here
because it belongs to the frozen API of this module.
"""

from __future__ import annotations

from typing import Any

from .cmdproc_parse import parse_line
from .resource import (Model, resource_attach_backing, resource_create,
                       resource_detach_backing, resource_flush, resource_find,
                       resource_transfer_from_host, resource_transfer_to_host,
                       resource_unref)
from .scanout import check_invariants, scanout_set
from .verdict import (CMD_INVALID, MODEL_CONSISTENT, MODEL_COUNTEREXAMPLE, OK,
                      TRANSFER_OK, Verdict)
from .vgproto import (CMD_GET_DISPLAY_INFO, CMD_MOVE_CURSOR, CMD_NAMES,
                      CMD_RESOURCE_ATTACH_BACKING, CMD_RESOURCE_CREATE_2D,
                      CMD_RESOURCE_DETACH_BACKING, CMD_RESOURCE_FLUSH,
                      CMD_RESOURCE_UNREF, CMD_SET_SCANOUT,
                      CMD_TRANSFER_FROM_HOST, CMD_TRANSFER_TO_HOST_2D,
                      CMD_UPDATE_CURSOR, ID_NONE)

__all__ = ["parse_line", "run"]

Rect = tuple[int, int, int, int]
NO_RECT: Rect = (0, 0, 0, 0)


def _rect_of(cmd: dict[str, Any]) -> Rect:
    """Rectangle of a command; an omitted rectangle is (0, 0, 0, 0)."""
    x, y, w, h = cmd.get("rect") or NO_RECT
    return (int(x), int(y), int(w), int(h))


def _develop_rect(m: Model, resource_id: int, rect: Rect) -> Rect:
    """Expand a rectangle whose width and height are both zero."""
    x, y, w, h = rect
    if w != 0 or h != 0:
        return rect
    res = resource_find(m, resource_id)
    if res is None:
        return rect
    return (0, 0, int(res.width), int(res.height))


def _cmd_targets(cmd: dict[str, Any], m: Model) -> tuple[int, int]:
    """Resource and scanout the command refers to; ID_NONE when absent."""
    ctype = int(cmd["type"])
    rid = ID_NONE
    if ctype == CMD_UPDATE_CURSOR:
        rid = int(cmd.get("resource_id", 0))
        rid = rid if rid else ID_NONE
    elif ctype in (CMD_RESOURCE_CREATE_2D, CMD_RESOURCE_UNREF, CMD_SET_SCANOUT,
                   CMD_RESOURCE_FLUSH, CMD_TRANSFER_TO_HOST_2D,
                   CMD_TRANSFER_FROM_HOST, CMD_RESOURCE_ATTACH_BACKING,
                   CMD_RESOURCE_DETACH_BACKING):
        rid = int(cmd.get("resource_id", 0))
    sid = ID_NONE
    if ctype in (CMD_SET_SCANOUT, CMD_UPDATE_CURSOR, CMD_MOVE_CURSOR):
        sid = int(cmd.get("scanout_id", 0))
    elif ctype == CMD_RESOURCE_UNREF:
        for key in sorted(m.scanouts):
            if int(m.scanouts[key].resource_id) == rid:
                sid = int(key)
                break
    return rid, sid


def _cursor_cmd(m: Model, cmd: dict[str, Any], visible: bool) -> int:
    """Apply update_cursor (visible) or move_cursor to the cursor."""
    m.cursor.scanout_id = int(cmd.get("scanout_id", 0))
    m.cursor.x = int(cmd.get("x", 0))
    m.cursor.y = int(cmd.get("y", 0))
    if visible:
        if int(cmd.get("resource_id", 0)):
            m.cursor.resource_id = int(cmd["resource_id"])
            m.cursor.hot_x = int(cmd.get("hot_x", 0))
            m.cursor.hot_y = int(cmd.get("hot_y", 0))
        m.cursor.visible = True
    m.cmds_applied += 1
    m.last_cause = ""
    return OK


def _apply_cmd(m: Model, cmd: dict[str, Any]) -> tuple[int, Rect]:
    """Apply one command to the model; returns the verdict code and the rect."""
    ctype = int(cmd["type"])
    rid = int(cmd.get("resource_id", 0))
    if ctype == CMD_GET_DISPLAY_INFO:
        return OK, NO_RECT
    if ctype == CMD_UPDATE_CURSOR:
        return _cursor_cmd(m, cmd, True), NO_RECT
    if ctype == CMD_MOVE_CURSOR:
        return _cursor_cmd(m, cmd, False), NO_RECT
    if ctype == CMD_RESOURCE_CREATE_2D:
        return resource_create(m, rid, int(cmd["format"]), int(cmd["width"]),
                               int(cmd["height"])), NO_RECT
    if ctype == CMD_RESOURCE_UNREF:
        return resource_unref(m, rid), NO_RECT
    if ctype == CMD_SET_SCANOUT:
        x, y, w, h = _rect_of(cmd)
        return scanout_set(m, int(cmd["scanout_id"]), rid, x, y, w, h), NO_RECT
    if ctype == CMD_RESOURCE_FLUSH:
        rect = _develop_rect(m, rid, _rect_of(cmd))
        return resource_flush(m, rid, rect), rect
    if ctype in (CMD_TRANSFER_TO_HOST_2D, CMD_TRANSFER_FROM_HOST):
        rect = _develop_rect(m, rid, _rect_of(cmd))
        offset = int(cmd.get("offset", 0))
        if ctype == CMD_TRANSFER_TO_HOST_2D:
            return resource_transfer_to_host(m, rid, rect, offset), rect
        return resource_transfer_from_host(m, rid, rect, offset), rect
    if ctype == CMD_RESOURCE_ATTACH_BACKING:
        pages = [(int(p["addr"]), int(p["length"]))
                 for p in (cmd.get("pages") or [])]
        return resource_attach_backing(m, rid, pages), NO_RECT
    if ctype == CMD_RESOURCE_DETACH_BACKING:
        return resource_detach_backing(m, rid), NO_RECT
    return CMD_INVALID, NO_RECT


def _verdict(code: int, index: int, ctype: int, resource_id: int = ID_NONE,
             scanout_id: int = ID_NONE, rect: Rect = NO_RECT,
             cause: str = "") -> Verdict:
    """Build a verdict; the fields that do not apply carry ID_NONE."""
    return Verdict(code=code, cmd_index=index, cmd_type=ctype,
                   resource_id=resource_id, scanout_id=scanout_id,
                   rect=rect, cause=cause)


def _apply_one(index: int, cmd: dict[str, Any],
               m: Model) -> tuple[list[Verdict], bool]:
    """Apply one command; returns its verdicts and the fail-fast flag."""
    ctype = int(cmd.get("type", 0))
    if ctype not in CMD_NAMES:
        m.cmds_rejected += 1
        return [_verdict(CMD_INVALID, index, ctype,
                         cause="unknown_command_type")], False
    if ctype == CMD_MOVE_CURSOR and not m.cursor.visible:
        m.cmds_rejected += 1
        return [_verdict(CMD_INVALID, index, ctype,
                         scanout_id=int(cmd.get("scanout_id", 0)),
                         cause="cursor_not_visible")], False
    rid, sid = _cmd_targets(cmd, m)
    code, rect = _apply_cmd(m, cmd)
    cause = m.last_cause if code == CMD_INVALID else ""
    verdict = _verdict(code, index, ctype, rid, sid,
                       rect if code == TRANSFER_OK else NO_RECT, cause)
    bad, invariant = check_invariants(m)
    if bad != 0:
        return [verdict, _verdict(MODEL_COUNTEREXAMPLE, index, ctype,
                                  cause=invariant)], True
    return [verdict], False


def run(cmds: list[dict[str, Any]], m: Model) -> list[Verdict]:
    """Apply a command sequence to the model and return the verdicts.

    The invariants are checked after each command; the first counter-example
    stops the sequence (fail fast) and no TOILE_MODEL_CONSISTENT is emitted.
    An empty sequence produces no verdict at all, like the C core.
    """
    out: list[Verdict] = []
    if not cmds:
        return out
    for index, cmd in enumerate(cmds):
        verdicts, stop = _apply_one(index, cmd, m)
        out.extend(verdicts)
        if stop:
            return out
    out.append(_verdict(MODEL_CONSISTENT, ID_NONE, ID_NONE))
    return out


#: Aliases expected by the CI guard of the frozen API (.github/checks/api-frozen.py).
cmdproc_run = run
cmd_parse_line = parse_line
