"""Wire layer for the virtio-gpu 2D protocol (little-endian).

Independent Python implementation of the frozen wire contract
(CONTRACT.md §3, §9); the byte layout matches the C core in core/.
"""

from __future__ import annotations

import struct
from typing import Any, Iterable

# --- command types (enum virtio_gpu_ctrl_type) ---------------------------
CMD_GET_DISPLAY_INFO = 0x0100
CMD_RESOURCE_CREATE_2D = 0x0101
CMD_RESOURCE_UNREF = 0x0102
CMD_SET_SCANOUT = 0x0103
CMD_RESOURCE_FLUSH = 0x0104
CMD_TRANSFER_TO_HOST_2D = 0x0105
CMD_RESOURCE_ATTACH_BACKING = 0x0106
CMD_RESOURCE_DETACH_BACKING = 0x0107
CMD_TRANSFER_FROM_HOST = 0x0206
CMD_UPDATE_CURSOR = 0x0300
CMD_MOVE_CURSOR = 0x0301

# --- limits (CONTRACT.md §3) ---------------------------------------------
MAX_RESOURCES = 64
MAX_SCANOUTS = 16
MAX_PAGES = 32
CAUSE_MAX = 96
ID_NONE = 0xFFFFFFFF
HDR_LEN = 24

# --- pixel formats (4 bytes per pixel) ------------------------------------
FMT_B8G8R8A8_UNORM = 1
FMT_B8G8R8X8_UNORM = 2
FMT_A8R8G8B8_UNORM = 3
FMT_X8R8G8B8_UNORM = 4
FMT_R8G8B8A8_UNORM = 67
FMT_X8B8G8R8_UNORM = 68
FMT_A8B8G8R8_UNORM = 121
FMT_R8G8B8X8_UNORM = 134

_FMT_LIST = (
    ("B8G8R8A8_UNORM", FMT_B8G8R8A8_UNORM),
    ("B8G8R8X8_UNORM", FMT_B8G8R8X8_UNORM),
    ("A8R8G8B8_UNORM", FMT_A8R8G8B8_UNORM),
    ("X8R8G8B8_UNORM", FMT_X8R8G8B8_UNORM),
    ("R8G8B8A8_UNORM", FMT_R8G8B8A8_UNORM),
    ("X8B8G8R8_UNORM", FMT_X8B8G8R8_UNORM),
    ("A8B8G8R8_UNORM", FMT_A8B8G8R8_UNORM),
    ("R8G8B8X8_UNORM", FMT_R8G8B8X8_UNORM),
)
FMT_NAMES: dict[int, str] = {value: name for name, value in _FMT_LIST}
FMT_BY_NAME: dict[str, int] = {name: value for name, value in _FMT_LIST}

CMD_NAMES: dict[int, str] = {
    CMD_GET_DISPLAY_INFO: "get_display_info",
    CMD_RESOURCE_CREATE_2D: "resource_create_2d",
    CMD_RESOURCE_UNREF: "resource_unref",
    CMD_SET_SCANOUT: "set_scanout",
    CMD_RESOURCE_FLUSH: "resource_flush",
    CMD_TRANSFER_TO_HOST_2D: "transfer_to_host_2d",
    CMD_RESOURCE_ATTACH_BACKING: "resource_attach_backing",
    CMD_RESOURCE_DETACH_BACKING: "resource_detach_backing",
    CMD_TRANSFER_FROM_HOST: "transfer_from_host",
    CMD_UPDATE_CURSOR: "update_cursor",
    CMD_MOVE_CURSOR: "move_cursor",
}

# --- wire layouts ----------------------------------------------------------
_HDR = struct.Struct("<IIQIB3x")
_CREATE = struct.Struct("<4I")
_RES_ID = struct.Struct("<2I")
_SCANOUT = struct.Struct("<4I2I")
_FLUSH = struct.Struct("<4I2I")
_XFER = struct.Struct("<4IQ2I")
_BACKING_HDR = struct.Struct("<2I")
_PAGE = struct.Struct("<QII")
_CURSOR_POS = struct.Struct("<4I")
_CURSOR = struct.Struct("<4I4I")


def fmt_bpp(fmt: int) -> int:
    """Bytes per pixel of a valid format; 0 when the format is unknown."""
    return 4 if fmt in FMT_NAMES else 0


def fmt_name(fmt: int) -> str | None:
    """Textual name of a format; None when unknown."""
    return FMT_NAMES.get(fmt)


def fmt_parse(text: str) -> int | None:
    """Parse a format name or number ("B8G8R8A8_UNORM", "1", "0x43")."""
    if not isinstance(text, str):
        return None
    key = text.strip()
    if not key:
        return None
    if key in FMT_BY_NAME:
        return FMT_BY_NAME[key]
    try:
        value = int(key, 0)
    except ValueError:
        return None
    return value if value in FMT_NAMES else None


def cmd_name(cmd_type: int) -> str | None:
    """DSL name of a command type; None when unknown."""
    return CMD_NAMES.get(cmd_type)


def _rect_of(cmd: dict[str, Any]) -> tuple[int, int, int, int]:
    """Rectangle of a command; an omitted rectangle is (0, 0, 0, 0)."""
    rect = cmd.get("rect")
    if not rect:
        return (0, 0, 0, 0)
    x, y, w, h = rect
    return (int(x), int(y), int(w), int(h))


def normalize_pages(pages: Iterable[Any]) -> list[tuple[int, int]]:
    """Backing pages as (addr, length) pairs; accepts mappings, pairs or objects."""
    out: list[tuple[int, int]] = []
    for page in pages:
        if isinstance(page, dict):
            out.append((int(page["addr"]), int(page["length"])))
        elif hasattr(page, "addr") and hasattr(page, "length"):
            out.append((int(page.addr), int(page.length)))
        else:
            addr, length = page
            out.append((int(addr), int(length)))
    return out


def _pack_backing(cmd: dict[str, Any]) -> bytes:
    """Payload of resource_attach_backing: id, count, then the mem entries."""
    pages = normalize_pages(cmd.get("pages") or [])
    if len(pages) > MAX_PAGES:
        raise ValueError("too many backing pages")
    out = bytearray(_BACKING_HDR.pack(int(cmd.get("resource_id", 0)), len(pages)))
    for addr, length in pages:
        out += _PAGE.pack(addr, length, 0)
    return bytes(out)


def _encode_payload(ctype: int, cmd: dict[str, Any]) -> bytes:
    """Serialize the command payload, i.e. what follows the 24-byte header."""
    rid = int(cmd.get("resource_id", 0))
    sid = int(cmd.get("scanout_id", 0))
    x, y, w, h = _rect_of(cmd)
    if ctype == CMD_GET_DISPLAY_INFO:
        return b""
    if ctype == CMD_RESOURCE_CREATE_2D:
        return _CREATE.pack(
            rid,
            int(cmd.get("format", 0)),
            int(cmd.get("width", 0)),
            int(cmd.get("height", 0)),
        )
    if ctype in (CMD_RESOURCE_UNREF, CMD_RESOURCE_DETACH_BACKING):
        return _RES_ID.pack(rid, 0)
    if ctype == CMD_SET_SCANOUT:
        return _SCANOUT.pack(x, y, w, h, sid, rid)
    if ctype == CMD_RESOURCE_FLUSH:
        return _FLUSH.pack(x, y, w, h, rid, 0)
    if ctype in (CMD_TRANSFER_TO_HOST_2D, CMD_TRANSFER_FROM_HOST):
        return _XFER.pack(x, y, w, h, int(cmd.get("offset", 0)), rid, 0)
    if ctype == CMD_RESOURCE_ATTACH_BACKING:
        return _pack_backing(cmd)
    if ctype == CMD_UPDATE_CURSOR:
        return _CURSOR.pack(
            sid,
            int(cmd.get("x", 0)),
            int(cmd.get("y", 0)),
            0,
            rid,
            int(cmd.get("hot_x", 0)),
            int(cmd.get("hot_y", 0)),
            0,
        )
    return _CURSOR_POS.pack(sid, int(cmd.get("x", 0)), int(cmd.get("y", 0)), 0)


def encode(cmd: dict[str, Any]) -> bytes:
    """Serialize a command dict into wire bytes; ValueError when invalid."""
    ctype = int(cmd["type"])
    if ctype not in CMD_NAMES:
        raise ValueError(f"unknown command type: {ctype:#x}")
    hdr = _HDR.pack(ctype, int(cmd.get("flags", 0)), int(cmd.get("fence_id", 0)), 0, 0)
    return hdr + _encode_payload(ctype, cmd)


def _expect(size: int, expected: int) -> None:
    """Raise ValueError when a payload length does not match the layout."""
    if size != expected:
        raise ValueError(f"invalid payload length: {size} != {expected}")


def _unpack_backing(body: bytes, cmd: dict[str, Any]) -> None:
    """Decode a resource_attach_backing payload into the command dict."""
    if len(body) < _BACKING_HDR.size:
        raise ValueError("backing header truncated")
    rid, count = _BACKING_HDR.unpack_from(body, 0)
    if count > MAX_PAGES or len(body) != _BACKING_HDR.size + count * _PAGE.size:
        raise ValueError("backing length mismatch")
    pages = []
    for i in range(count):
        addr, length, _pad = _PAGE.unpack_from(body, _BACKING_HDR.size + i * _PAGE.size)
        pages.append({"addr": addr, "length": length})
    cmd["resource_id"] = rid
    cmd["pages"] = pages


def _decode_payload(ctype: int, body: bytes, cmd: dict[str, Any]) -> None:
    """Decode the command payload into the command dict."""
    if ctype == CMD_GET_DISPLAY_INFO:
        _expect(len(body), 0)
        return
    if ctype == CMD_RESOURCE_CREATE_2D:
        _expect(len(body), _CREATE.size)
        cmd.update(zip(("resource_id", "format", "width", "height"), _CREATE.unpack(body)))
        return
    if ctype in (CMD_RESOURCE_UNREF, CMD_RESOURCE_DETACH_BACKING):
        _expect(len(body), _RES_ID.size)
        cmd["resource_id"] = _RES_ID.unpack(body)[0]
        return
    if ctype == CMD_SET_SCANOUT:
        _expect(len(body), _SCANOUT.size)
        x, y, w, h, sid, rid = _SCANOUT.unpack(body)
        cmd.update(resource_id=rid, scanout_id=sid, rect=(x, y, w, h))
        return
    if ctype == CMD_RESOURCE_FLUSH:
        _expect(len(body), _FLUSH.size)
        x, y, w, h, rid, _pad = _FLUSH.unpack(body)
        cmd.update(resource_id=rid, rect=(x, y, w, h))
        return
    if ctype in (CMD_TRANSFER_TO_HOST_2D, CMD_TRANSFER_FROM_HOST):
        _expect(len(body), _XFER.size)
        x, y, w, h, offset, rid, _pad = _XFER.unpack(body)
        cmd.update(resource_id=rid, rect=(x, y, w, h), offset=offset)
        return
    if ctype == CMD_RESOURCE_ATTACH_BACKING:
        _unpack_backing(body, cmd)
        return
    if ctype == CMD_UPDATE_CURSOR:
        _expect(len(body), _CURSOR.size)
        sid, x, y, _p, rid, hot_x, hot_y, _p2 = _CURSOR.unpack(body)
        cmd.update(
            scanout_id=sid, x=x, y=y, resource_id=rid, hot_x=hot_x, hot_y=hot_y
        )
        return
    _expect(len(body), _CURSOR_POS.size)
    sid, x, y, _pad = _CURSOR_POS.unpack(body)
    cmd.update(scanout_id=sid, x=x, y=y, resource_id=0, hot_x=0, hot_y=0)


def decode(buf: bytes) -> dict[str, Any]:
    """Decode wire bytes into a command dict; ValueError when invalid."""
    data = bytes(buf)
    if len(data) < HDR_LEN:
        raise ValueError("buffer too short")
    ctype, flags, fence_id, _ctx, _ring = _HDR.unpack_from(data, 0)
    if ctype not in CMD_NAMES:
        raise ValueError(f"unknown command type: {ctype:#x}")
    cmd: dict[str, Any] = {"type": ctype, "flags": flags, "fence_id": fence_id}
    _decode_payload(ctype, data[HDR_LEN:], cmd)
    return cmd


#: Alias jumeaux Python des symboles C (garde api-frozen).
vg_encode = encode
vg_decode = decode
