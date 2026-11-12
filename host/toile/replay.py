"""Fixture loading and replay (CONTRACT.md sections 4 and 5).

A fixture is a DSL file (``.tcl``) or a wire file (``.hex``); both give the
same command dicts, so they feed the same processor.
"""

from __future__ import annotations

from typing import Any

from .cmdproc import parse_line, run
from .resource import Model
from .verdict import Verdict
from .vgproto import decode

_HEX_DIGITS = "0123456789abcdefABCDEF"


class FixtureError(Exception):
    """A fixture cannot be read or parsed; the message is the CLI text."""


def _read_text(path: str) -> str:
    """Read a fixture file; FixtureError when it cannot be read."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read()
    except OSError as exc:
        raise FixtureError(f"{path}: fixture illisible") from exc


def _error(path: str, line_no: int, reason: str) -> FixtureError:
    """Fixture error with the frozen message shape ``<path>:<line>: <reason>``."""
    _, _, tail = reason.partition(":")
    return FixtureError(f"{path}:{line_no}: {tail or reason}")


def load_fixture(path: str) -> list[dict[str, Any]]:
    """Commands of a DSL fixture; FixtureError on the first parse error."""
    cmds: list[dict[str, Any]] = []
    for line_no, line in enumerate(_read_text(path).splitlines(), start=1):
        cmd, err = parse_line(line, line_no)
        if err:
            raise _error(path, line_no, err)
        if cmd is not None:
            cmds.append(cmd)
    return cmds


def _hex_bytes(line: str) -> bytes | None:
    """Bytes of one hexadecimal line; None when the line is malformed."""
    digits: list[str] = []
    for ch in line:
        if ch == "#":
            break
        if ch in " \t\r":
            continue
        if ch not in _HEX_DIGITS:
            return None
        digits.append(ch)
    if len(digits) % 2:
        return None
    return bytes.fromhex("".join(digits))


def load_wire(path: str) -> list[dict[str, Any]]:
    """Commands of a wire fixture; FixtureError on the first bad line."""
    cmds: list[dict[str, Any]] = []
    for line_no, line in enumerate(_read_text(path).splitlines(), start=1):
        raw = _hex_bytes(line)
        if raw is None:
            raise _error(path, line_no, "ligne hexadecimale invalide")
        if not raw:
            continue
        try:
            cmds.append(decode(raw))
        except ValueError as exc:
            raise _error(path, line_no, "commande fil invalide") from exc
    return cmds


def replay(path: str, wire: bool = False) -> tuple[Model, list[Verdict]]:
    """Replay a fixture on a fresh model; returns the model and the verdicts."""
    cmds = load_wire(path) if wire else load_fixture(path)
    model = Model()
    return model, run(cmds, model)
