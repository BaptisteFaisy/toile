"""Model state: resources, backing, transfers and flush (CONTRACT.md §5).

Independent Python implementation of the frozen C API
(core/include/toile/resource.h). Every state function returns a verdict
code: on failure the state is untouched except ``cmds_rejected``, on
success ``cmds_applied`` grows. ``OK`` and ``TRANSFER_OK`` are successes.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import TYPE_CHECKING, Any, Iterable

from .verdict import (
    BACKING_MISSING, CAUSE_BACKING_ENTRY_COUNT, CAUSE_BAD_FORMAT,
    CAUSE_BAD_GEOMETRY, CAUSE_RESOURCE_REFERENCED, CAUSE_RESOURCE_TABLE_FULL,
    CMD_INVALID, FLUSH_NO_SCANOUT, OK, RESOURCE_EXISTS, RESOURCE_MISSING,
    SCANOUT_INVALID, TRANSFER_OK,
)
from .vgproto import MAX_PAGES, MAX_RESOURCES, fmt_bpp, normalize_pages

if TYPE_CHECKING:  # typing only: keeps the resource/scanout cycle away
    from .scanout import Scanout

Rect = tuple[int, int, int, int]

#: First line of the canonical serialization (CONTRACT.md §8).
STATE_HEADER = "toile-state-v1"

# FNV-1a 64-bit parameters (CONTRACT.md §8).
_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME = 0x100000001B3
_MASK64 = 0xFFFFFFFFFFFFFFFF


def fnv1a64(data: bytes) -> int:
    """FNV-1a 64-bit hash of a byte string, masked to 64 bits."""
    digest = _FNV_OFFSET
    for byte in data:
        digest ^= byte
        digest = (digest * _FNV_PRIME) & _MASK64
    return digest


@dataclass(slots=True)
class Page:
    """One guest physical page of a backing store."""

    addr: int
    length: int


@dataclass(slots=True)
class Resource:
    """A 2D resource: geometry, format, backing pages and flush count."""

    id: int
    fmt: int
    width: int
    height: int
    pages: list[Page] = field(default_factory=list)
    flush_count: int = 0


@dataclass(slots=True)
class Cursor:
    """Hardware cursor; invisible and zeroed like the initial C model."""

    resource_id: int = 0
    scanout_id: int = 0
    x: int = 0
    y: int = 0
    hot_x: int = 0
    hot_y: int = 0
    visible: bool = False


class Model:
    """Whole model state: tables, cursor and counters."""

    def __init__(self) -> None:
        self.reset()

    def reset(self) -> None:
        """Empty the tables and the counters; the cursor becomes invisible."""
        self.resources: dict[int, Resource] = {}
        self.scanouts: dict[int, Scanout] = {}
        self.cursor = Cursor()
        self.cmds_applied = 0
        self.cmds_rejected = 0
        self.flushes = 0
        self.bytes_transferred = 0
        #: Cause of the last refusal; empty after a success.
        self.last_cause = ""
        #: Ids ever attached to a scanout; bookkeeping for invariant I2.
        self.scanned_resources: set[int] = set()
        #: Ids that ever carried a backing; bookkeeping for invariant I3.
        self.backed_resources: set[int] = set()

    def resource_count(self) -> int:
        """Number of active resources."""
        return len(self.resources)

    def serialize(self) -> str:
        """Canonical ASCII serialization of the state (CONTRACT.md §8)."""
        out = [f"{STATE_HEADER}\n"]
        for rid in sorted(self.resources):
            res = self.resources[rid]
            out.append(
                f"R {res.id} {res.fmt} {res.width} {res.height} {len(res.pages)}\n"
            )
            for page in res.pages:
                out.append(f"P {page.addr:016x} {page.length}\n")
        for sid in sorted(self.scanouts):
            scanout = self.scanouts[sid]
            res = self.resources.get(scanout.resource_id)
            flushes = res.flush_count if res is not None else 0
            x, y, w, h = scanout.rect
            out.append(
                f"S {scanout.id} {scanout.resource_id} {x} {y} {w} {h} {flushes}\n"
            )
        cursor = self.cursor
        out.append(
            f"C {cursor.resource_id} {cursor.scanout_id} {cursor.x} {cursor.y} "
            f"{cursor.hot_x} {cursor.hot_y} {1 if cursor.visible else 0}\n"
        )
        out.append(
            f"K {self.cmds_applied} {self.cmds_rejected} "
            f"{self.flushes} {self.bytes_transferred}\n"
        )
        return "".join(out)

    def state_hash(self) -> int:
        """FNV-1a 64-bit hash of the canonical serialization."""
        return fnv1a64(self.serialize().encode("ascii"))


def model_init(model: Model | None = None) -> Model:
    """Fresh model (or reset ``model``) with empty tables, invisible cursor."""
    if model is None:
        return Model()
    model.reset()
    return model


def model_state_hash(model: Model) -> int:
    """FNV-1a 64-bit hash of the canonical state (C twin name)."""
    return model.state_hash()


def record_reject(model: Model, code: int, cause: str = "") -> int:
    """Record a refusal: the state stays untouched except the counter."""
    model.cmds_rejected += 1
    model.last_cause = cause
    return code


def record_apply(model: Model) -> int:
    """Record an applied command and clear the last cause."""
    model.cmds_applied += 1
    model.last_cause = ""
    return OK


def resource_find(model: Model, resource_id: int) -> Resource | None:
    """Look up an active resource; None when it does not exist."""
    return model.resources.get(resource_id)


def resource_has_scanout(model: Model, resource_id: int) -> bool:
    """True when an active scanout displays the resource (never for id 0)."""
    if resource_id == 0:
        return False
    return any(s.resource_id == resource_id for s in model.scanouts.values())


def resource_create(
    model: Model, resource_id: int, fmt: int, width: int, height: int
) -> int:
    """Create a resource; refusals mirror the C guard order."""
    if resource_id in model.resources:
        return record_reject(model, RESOURCE_EXISTS)
    if fmt_bpp(fmt) == 0:
        return record_reject(model, CMD_INVALID, CAUSE_BAD_FORMAT)
    if width == 0 or height == 0:
        return record_reject(model, CMD_INVALID, CAUSE_BAD_GEOMETRY)
    if len(model.resources) >= MAX_RESOURCES:
        return record_reject(model, CMD_INVALID, CAUSE_RESOURCE_TABLE_FULL)
    model.resources[resource_id] = Resource(
        id=resource_id, fmt=fmt, width=width, height=height
    )
    return record_apply(model)


def resource_unref(model: Model, resource_id: int) -> int:
    """Destroy a resource; refused when a scanout still displays it."""
    if resource_find(model, resource_id) is None:
        return record_reject(model, RESOURCE_MISSING)
    if resource_has_scanout(model, resource_id):
        return record_reject(model, SCANOUT_INVALID, CAUSE_RESOURCE_REFERENCED)
    del model.resources[resource_id]
    return record_apply(model)


def resource_attach_backing(
    model: Model, resource_id: int, pages: Iterable[Any]
) -> int:
    """Attach guest pages (1..MAX_PAGES), replacing any previous backing."""
    res = resource_find(model, resource_id)
    if res is None:
        return record_reject(model, RESOURCE_MISSING)
    entries = [Page(addr=a, length=l) for a, l in normalize_pages(pages)]
    if not entries or len(entries) > MAX_PAGES:
        return record_reject(model, CMD_INVALID, CAUSE_BACKING_ENTRY_COUNT)
    res.pages = entries
    model.backed_resources.add(resource_id)
    return record_apply(model)


def resource_detach_backing(model: Model, resource_id: int) -> int:
    """Detach the backing pages; a no-op when none was attached."""
    res = resource_find(model, resource_id)
    if res is None:
        return record_reject(model, RESOURCE_MISSING)
    res.pages = []
    return record_apply(model)


def resource_has_backing(model: Model, resource_id: int) -> bool:
    """True when the resource exists and carries at least one page."""
    res = resource_find(model, resource_id)
    return res is not None and bool(res.pages)


def resource_flush(model: Model, resource_id: int, rect: Rect | None = None) -> int:
    """Host "display" effect; FLUSH_NO_SCANOUT when no scanout shows it."""
    res = resource_find(model, resource_id)
    if res is None:
        return record_reject(model, RESOURCE_MISSING)
    if not resource_has_scanout(model, resource_id):
        return record_reject(model, FLUSH_NO_SCANOUT)
    res.flush_count += 1
    model.flushes += 1
    return record_apply(model)


def _effective_rect(res: Resource, rect: Rect | None) -> Rect:
    """Expand an omitted or empty rectangle to the whole resource."""
    if rect is None:
        return (0, 0, res.width, res.height)
    x, y, w, h = rect
    if w == 0 and h == 0:
        return (0, 0, res.width, res.height)
    return (x, y, w, h)


def resource_transfer_to_host(
    model: Model, resource_id: int, rect: Rect | None = None, offset: int = 0
) -> int:
    """Guest to host copy; success is TRANSFER_OK and counts the bytes."""
    res = resource_find(model, resource_id)
    if res is None:
        return record_reject(model, RESOURCE_MISSING)
    if not res.pages:
        return record_reject(model, BACKING_MISSING)
    x, y, w, h = _effective_rect(res, rect)
    model.bytes_transferred += w * h * fmt_bpp(res.fmt)
    model.cmds_applied += 1
    model.last_cause = ""
    return TRANSFER_OK


def resource_transfer_from_host(
    model: Model, resource_id: int, rect: Rect | None = None, offset: int = 0
) -> int:
    """Host to guest read; success is TOILE_OK and counts no bytes."""
    res = resource_find(model, resource_id)
    if res is None:
        return record_reject(model, RESOURCE_MISSING)
    if not res.pages:
        return record_reject(model, BACKING_MISSING)
    return record_apply(model)
