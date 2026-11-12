"""Verdict codes, severities and causes (CONTRACT.md §6).

Values are frozen and identical to the C core (``core/include/toile/verdict.h``).
"""

from __future__ import annotations

from dataclasses import dataclass

from .vgproto import ID_NONE

OK = 0
CMD_INVALID = 1
RESOURCE_EXISTS = 2
RESOURCE_MISSING = 3
BACKING_MISSING = 4
SCANOUT_INVALID = 5
FLUSH_NO_SCANOUT = 6
TRANSFER_OK = 7
MODEL_CONSISTENT = 8
MODEL_COUNTEREXAMPLE = 9

UNKNOWN = "TOILE_UNKNOWN"

NAMES: dict[int, str] = {
    OK: "TOILE_OK",
    CMD_INVALID: "TOILE_CMD_INVALID",
    RESOURCE_EXISTS: "TOILE_RESOURCE_EXISTS",
    RESOURCE_MISSING: "TOILE_RESOURCE_MISSING",
    BACKING_MISSING: "TOILE_BACKING_MISSING",
    SCANOUT_INVALID: "TOILE_SCANOUT_INVALID",
    FLUSH_NO_SCANOUT: "TOILE_FLUSH_NO_SCANOUT",
    TRANSFER_OK: "TOILE_TRANSFER_OK",
    MODEL_CONSISTENT: "TOILE_MODEL_CONSISTENT",
    MODEL_COUNTEREXAMPLE: "TOILE_MODEL_COUNTEREXAMPLE",
}

SEVERITIES: dict[int, str] = {
    OK: "info",
    CMD_INVALID: "error",
    RESOURCE_EXISTS: "warning",
    RESOURCE_MISSING: "error",
    BACKING_MISSING: "error",
    SCANOUT_INVALID: "error",
    FLUSH_NO_SCANOUT: "warning",
    TRANSFER_OK: "info",
    MODEL_CONSISTENT: "info",
    MODEL_COUNTEREXAMPLE: "error",
}

# --- frozen causes (CONTRACT.md §6) ---------------------------------------
CAUSE_UNKNOWN_COMMAND_TYPE = "unknown_command_type"
CAUSE_BAD_FORMAT = "bad_format"
CAUSE_BAD_GEOMETRY = "bad_geometry"
CAUSE_RESOURCE_TABLE_FULL = "resource_table_full"
CAUSE_SCANOUT_TABLE_FULL = "scanout_table_full"
CAUSE_BACKING_ENTRY_COUNT = "backing_entry_count"
CAUSE_RESOURCE_REFERENCED = "resource_referenced_by_scanout"
CAUSE_CURSOR_NOT_VISIBLE = "cursor_not_visible"
CAUSE_SCANOUT_ID_OUT_OF_RANGE = "scanout_id_out_of_range"


def name(code: int) -> str:
    """Exact enum name of a verdict code; "TOILE_UNKNOWN" when out of range."""
    return NAMES.get(code, UNKNOWN)


def severity(code: int) -> str:
    """Severity of a verdict code: "info", "warning" or "error"."""
    return SEVERITIES.get(code, "error")


def is_error(code: int) -> bool:
    """True when the severity of a verdict code is "error"."""
    return severity(code) == "error"


@dataclass(slots=True)
class Verdict:
    """Trace of one decision; unset fields carry ID_NONE or an empty cause."""

    code: int = OK
    cmd_index: int = ID_NONE
    cmd_type: int = ID_NONE
    resource_id: int = ID_NONE
    scanout_id: int = ID_NONE
    rect: tuple[int, int, int, int] = (0, 0, 0, 0)
    cause: str = ""

    @property
    def code_name(self) -> str:
        """Enum name of the verdict code."""
        return name(self.code)

    @property
    def level(self) -> str:
        """Severity of the verdict code."""
        return severity(self.code)


#: Alias jumeaux Python des symboles C (garde api-frozen).
verdict_name = name
verdict_severity = severity
verdict_is_error = is_error


def make_verdict(
    code: int = OK,
    cmd_index: int = ID_NONE,
    cmd_type: int = ID_NONE,
    resource_id: int = ID_NONE,
    scanout_id: int = ID_NONE,
    rect: tuple[int, int, int, int] = (0, 0, 0, 0),
    cause: str = "",
) -> Verdict:
    """Build a verdict; unset fields default to ID_NONE and an empty cause."""
    return Verdict(
        code=code,
        cmd_index=cmd_index,
        cmd_type=cmd_type,
        resource_id=resource_id,
        scanout_id=scanout_id,
        rect=tuple(rect),
        cause=cause,
    )
