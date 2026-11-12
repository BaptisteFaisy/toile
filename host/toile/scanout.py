"""Scanout table and model invariants (CONTRACT.md §3, §5).

Independent Python implementation of the scanout rules of the frozen C
contract: a scanout displays one resource in one rectangle. The invariant
probes mirror the C ones (core/src/scanout.c) and never fire on a
legitimate sequence.
"""

from __future__ import annotations

from dataclasses import dataclass

from .resource import (
    Model,
    record_apply,
    record_reject,
    resource_find,
    resource_has_scanout,
)
from .verdict import (
    CAUSE_BAD_GEOMETRY,
    CAUSE_SCANOUT_ID_OUT_OF_RANGE,
    CAUSE_SCANOUT_TABLE_FULL,
    CMD_INVALID,
    MODEL_COUNTEREXAMPLE,
    SCANOUT_INVALID,
)
from .vgproto import MAX_PAGES, MAX_RESOURCES, MAX_SCANOUTS, fmt_bpp

__all__ = [
    "Scanout",
    "check_invariants",
    "model_check_invariants",
    "resource_has_scanout",
    "scanout_count",
    "scanout_find",
    "scanout_set",
]


@dataclass(slots=True)
class Scanout:
    """A scanout: the resource it displays and the rectangle it takes."""

    id: int
    resource_id: int
    rect: tuple[int, int, int, int] = (0, 0, 0, 0)


def scanout_find(model: Model, scanout_id: int) -> Scanout | None:
    """Look up an active scanout; None when it does not exist."""
    return model.scanouts.get(scanout_id)


def scanout_count(model: Model) -> int:
    """Number of active scanouts."""
    return len(model.scanouts)


def scanout_set(
    model: Model,
    scanout: int,
    resource_id: int,
    x: int,
    y: int,
    w: int,
    h: int,
) -> int:
    """Point a scanout at a resource; resource_id 0 disables the scanout.

    Guard order (frozen by the C twin): out of range, disable, missing
    resource, null geometry, full table.
    """
    if scanout < 0 or scanout >= MAX_SCANOUTS:
        return record_reject(model, CMD_INVALID, CAUSE_SCANOUT_ID_OUT_OF_RANGE)
    if resource_id == 0:
        model.scanouts.pop(scanout, None)
        return record_apply(model)
    if resource_find(model, resource_id) is None:
        return record_reject(model, SCANOUT_INVALID)
    if w == 0 or h == 0:
        return record_reject(model, CMD_INVALID, CAUSE_BAD_GEOMETRY)
    if scanout not in model.scanouts and len(model.scanouts) >= MAX_SCANOUTS:
        return record_reject(model, CMD_INVALID, CAUSE_SCANOUT_TABLE_FULL)
    model.scanouts[scanout] = Scanout(
        id=scanout, resource_id=resource_id, rect=(x, y, w, h)
    )
    model.scanned_resources.add(resource_id)
    return record_apply(model)


def _inv_i1(model: Model) -> bool:
    """I1: every active scanout references an existing resource."""
    return all(
        scanout.resource_id in model.resources
        for scanout in model.scanouts.values()
    )


def _inv_i2(model: Model) -> bool:
    """I2: only a resource attached to a scanout can be flushed."""
    return all(
        res.flush_count == 0 or res.id in model.scanned_resources
        for res in model.resources.values()
    )


def _inv_i3(model: Model) -> bool:
    """I3: a backing never outlives the resource that carried it."""
    return all(
        not res.pages or res.id in model.backed_resources
        for res in model.resources.values()
    )


def _inv_i4(model: Model) -> bool:
    """I4: no active resource has a null geometry or an unknown format."""
    return all(
        res.width != 0 and res.height != 0 and fmt_bpp(res.fmt) != 0
        for res in model.resources.values()
    )


def _inv_i5(model: Model) -> bool:
    """I5: the tables never overflow their limits."""
    if len(model.resources) > MAX_RESOURCES or len(model.scanouts) > MAX_SCANOUTS:
        return False
    return all(len(res.pages) <= MAX_PAGES for res in model.resources.values())


def _inv_i6(model: Model) -> bool:
    """I6: bytes_transferred is a sum of areas x bpp, so a multiple of 4."""
    return model.bytes_transferred % 4 == 0


def check_invariants(model: Model) -> tuple[int, str]:
    """Check the model invariants; (0, "") or (9, "invariant_iN")."""
    checks = (_inv_i1, _inv_i2, _inv_i3, _inv_i4, _inv_i5, _inv_i6)
    for index, check in enumerate(checks, start=1):
        if not check(model):
            return (MODEL_COUNTEREXAMPLE, f"invariant_i{index}")
    return (0, "")


#: Alias jumeau Python du symbole C (garde api-frozen).
model_check_invariants = check_invariants
