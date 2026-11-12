"""DSL fixture grammar (CONTRACT.md section 4).

One line of a fixture is parsed into the command dict that the processor of
``toile.cmdproc`` consumes; the same grammar as the C parser
``core/src/cmdproc_parse.c``.
"""

from __future__ import annotations

from typing import Any

from .vgproto import CMD_NAMES, MAX_PAGES, fmt_parse

_ALIASES = dict(pair.split(":") for pair in (
    "id:id resource:id format:format width:width w:width height:height "
    "h:height scanout:scanout x:x y:y offset:offset pages:pages "
    "hot_x:hot_x hot_y:hot_y").split())

_REQUIRED = {
    "get_display_info": "",
    "resource_create_2d": "id format width height",
    "resource_unref": "id",
    "set_scanout": "id width height scanout x y",
    "resource_flush": "id",
    "transfer_to_host_2d": "id",
    "resource_attach_backing": "id pages",
    "resource_detach_backing": "id",
    "transfer_from_host": "id",
    "update_cursor": "scanout x y",
    "move_cursor": "scanout x y",
}

_TYPE_BY_NAME = {name: ctype for ctype, name in CMD_NAMES.items()}
_U32_MAX = 0xFFFFFFFF
_U64_MAX = 0xFFFFFFFFFFFFFFFF


def _err(line_no: int, reason: str) -> str:
    """Parse error message: ``<line>:<reason>`` (CONTRACT.md section 4)."""
    return f"{line_no}:{reason}"


def _parse_uint(text: str) -> int | None:
    """Decimal or 0x-prefixed integer; None when the text is not a number."""
    if not text:
        return None
    base, digits = 10, text
    if text[:2].lower() == "0x":
        base, digits = 16, text[2:]
    if not digits:
        return None
    value = 0
    for ch in digits:
        if "0" <= ch <= "9":
            step = ord(ch) - 48
        elif base == 16 and "a" <= ch <= "f":
            step = ord(ch) - 87
        elif base == 16 and "A" <= ch <= "F":
            step = ord(ch) - 55
        else:
            return None
        if step >= base:
            return None
        value = value * base + step
        if value > _U64_MAX:
            return None
    return value


def _parse_pages(raw: str) -> list[tuple[int, int]] | None:
    """Parse ``addr:len[,addr:len]``; None when the list is malformed."""
    pages: list[tuple[int, int]] = []
    for item in raw.split(","):
        addr_text, sep, length_text = item.partition(":")
        addr = _parse_uint(addr_text) if sep else None
        length = _parse_uint(length_text) if sep else None
        if addr is None or length is None:
            return None
        pages.append((addr, length & _U32_MAX))
        if len(pages) > MAX_PAGES:
            return None
    return pages


def _parse_value(key: str, raw: str) -> Any:
    """Value of one ``key=value`` token; None when the value is invalid."""
    if key == "pages":
        return _parse_pages(raw) if raw else None
    if key == "format":
        return fmt_parse(raw)
    value = _parse_uint(raw)
    if value is None:
        return None
    return value if key == "offset" else value & _U32_MAX


def parse_line(line: str, line_no: int) -> tuple[dict[str, Any] | None, str]:
    """Parse one DSL line into a command dict.

    Returns ``(None, "")`` for a blank line or a comment and ``(None, error)``
    with ``error`` shaped ``"<line>:<reason>"`` on a parse error.
    """
    text = line.strip()
    if not text or text.startswith("#"):
        return None, ""
    parts = text.split()
    name = parts[0]
    if name not in _TYPE_BY_NAME:
        return None, _err(line_no, "unknown_command")
    values: dict[str, Any] = {}
    for token in parts[1:]:
        key, sep, raw = token.partition("=")
        canon = _ALIASES.get(key) if sep else None
        if canon is None:
            return None, _err(line_no, "unknown_key")
        if canon in values:
            return None, _err(line_no, "duplicate_key")
        parsed = _parse_value(canon, raw)
        if parsed is None:
            return None, _err(line_no, "bad_value")
        values[canon] = parsed
    if set(_REQUIRED[name].split()) - values.keys():
        return None, _err(line_no, "missing_required_key")
    return _build_cmd(_TYPE_BY_NAME[name], values), ""


def _build_cmd(ctype: int, values: dict[str, Any]) -> dict[str, Any]:
    """Build the command dict of a parsed line.

    The dict carries every key of the grammar; the processor only reads the
    ones the command type uses.
    """
    rect = (values.get("x", 0), values.get("y", 0),
            values.get("width", 0), values.get("height", 0))
    return {
        "type": ctype,
        "flags": 0,
        "fence_id": 0,
        "resource_id": values.get("id", 0),
        "format": values.get("format", 0),
        "width": values.get("width", 0),
        "height": values.get("height", 0),
        "scanout_id": values.get("scanout", 0),
        "x": values.get("x", 0),
        "y": values.get("y", 0),
        "offset": values.get("offset", 0),
        "hot_x": values.get("hot_x", 0),
        "hot_y": values.get("hot_y", 0),
        "rect": rect,
        "pages": [{"addr": addr, "length": length}
                  for addr, length in values.get("pages", [])],
    }
