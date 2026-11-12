"""Command line of the Python twin (CONTRACT.md section 11).

    python3 -m toile.cli run    <fixture.tcl> [--format text|json|md] [--expect <golden>]
    python3 -m toile.cli wire   <fixture.hex> [--format text|json|md] [--expect <golden>]
    python3 -m toile.cli golden <fixture.tcl> [--format text|json|md]
    python3 -m toile.cli selftest

Exit codes: 0 no error, 1 an error verdict, 2 usage or invalid fixture,
3 divergence with --expect.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass
from typing import Any

from .cmdproc import run
from .replay import FixtureError, load_fixture, load_wire
from .report import report
from .resource import Model
from .verdict import is_error
from .vgproto import (CMD_RESOURCE_CREATE_2D, FMT_B8G8R8A8_UNORM, decode,
                      encode)

EXIT_OK = 0
EXIT_ERROR = 1
EXIT_USAGE = 2
EXIT_DIVERGE = 3

FORMATS = ("text", "json", "md")

_USAGE = """usage:
  toile-run run    <fixture.tcl> [--format text|json|md] [--expect <golden>]
  toile-run wire   <fixture.hex> [--format text|json|md] [--expect <golden>]
  toile-run golden <fixture.tcl> [--format text|json|md]
  toile-run selftest

codes de sortie : 0 ok, 1 verdict d'erreur, 2 usage/fixture invalide,
                  3 divergence avec --expect
"""


@dataclass
class Options:
    """Options of a CLI call: the fixture, the format and the golden."""

    fixture: str = ""
    fmt: str = "text"
    expect: str | None = None


def _usage(stream: Any = sys.stderr) -> None:
    """Print the usage of the CLI."""
    stream.write(_USAGE)


def _fail(message: str) -> int:
    """Print an error message on stderr and return the usage exit code."""
    print(message, file=sys.stderr)
    return EXIT_USAGE


def _parse_args(argv: list[str]) -> tuple[Options | None, int]:
    """Parse the options of a sub-command; (None, code) on a usage error."""
    opts = Options()
    index = 0
    while index < len(argv):
        arg = argv[index]
        if arg == "--format" and index + 1 < len(argv):
            opts.fmt = argv[index + 1]
            index += 2
            continue
        if arg == "--expect" and index + 1 < len(argv):
            opts.expect = argv[index + 1]
            index += 2
            continue
        if arg.startswith("--"):
            return None, _fail(f"toile: option inconnue ou incomplete: {arg}")
        if not opts.fixture:
            opts.fixture = arg
        else:
            return None, _fail(f"toile: argument en trop: {arg}")
        index += 1
    if not opts.fixture:
        return None, _fail("toile: fixture manquante")
    if opts.fmt not in FORMATS:
        return None, _fail(f"toile: format inconnu: {opts.fmt}")
    return opts, EXIT_OK


def _execute(cmds: list[dict[str, Any]], opts: Options,
             out_path: str | None = None) -> int:
    """Run the commands, write the report and check the golden."""
    model = Model()
    verdicts = run(cmds, model)
    try:
        text = report(opts.fmt, model, verdicts, opts.fixture)
    except ValueError:
        return _fail(f"toile: format de rapport inconnu: {opts.fmt}")
    if out_path is None:
        sys.stdout.write(text)
    else:
        try:
            with open(out_path, "w", encoding="utf-8", newline="") as handle:
                handle.write(text)
        except OSError:
            return _fail(f"toile: ecriture impossible: {out_path}")
    if opts.expect is not None:
        try:
            with open(opts.expect, "rb") as handle:
                expected = handle.read()
        except OSError:
            return _fail(f"toile: golden illisible: {opts.expect}")
        if expected != text.encode("utf-8"):
            print(f"toile: divergence avec le golden {opts.expect}",
                  file=sys.stderr)
            return EXIT_DIVERGE
    if any(is_error(v.code) for v in verdicts):
        return EXIT_ERROR
    return EXIT_OK


def _golden_path(fixture: str, fmt: str) -> str | None:
    """Path of the golden file: ``fixtures/golden/<stem>.<fmt>``."""
    base = os.path.basename(fixture)
    dot = base.rfind(".")
    stem = base[:dot] if dot > 0 else base
    if not stem:
        return None
    return os.path.join("fixtures", "golden", f"{stem}.{fmt}")


def _cmd_run(opts: Options, wire: bool) -> int:
    """Sub-command ``run`` or ``wire``: replay a fixture and report it."""
    try:
        cmds = load_wire(opts.fixture) if wire else load_fixture(opts.fixture)
    except FixtureError as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE
    return _execute(cmds, opts)


def _cmd_golden(opts: Options) -> int:
    """Sub-command ``golden``: write the golden of a fixture."""
    path = _golden_path(opts.fixture, opts.fmt)
    if path is None:
        return _fail("toile: nom de fixture vide")
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
    except OSError:
        return _fail("toile: creation de fixtures/golden impossible")
    try:
        cmds = load_fixture(opts.fixture)
    except FixtureError as exc:
        print(str(exc), file=sys.stderr)
        return EXIT_USAGE
    code = _execute(cmds, opts, out_path=path)
    if code == EXIT_OK:
        print(f"toile: golden ecrit: {path}", file=sys.stderr)
    return code


def _selftest() -> int:
    """Internal checks of the twin; 0 when everything holds."""
    failures = 0
    model = Model()
    if _st_model(model):
        failures += 1
    if _st_roundtrip():
        failures += 1
    if _st_reports(model):
        failures += 1
    if _st_cmdproc(model):
        failures += 1
    if failures:
        print(f"selftest: {failures} echec(s)", file=sys.stderr)
        return EXIT_ERROR
    print("selftest: ok")
    return EXIT_OK


def _st_model(model: Model) -> int:
    """Check that a resource is created and that the state hash is not zero."""
    from .resource import resource_create
    if resource_create(model, 1, FMT_B8G8R8A8_UNORM, 64, 32) != 0:
        print("selftest: creation de ressource refusee", file=sys.stderr)
        return 1
    if model.state_hash() == 0:
        print("selftest: hash d'etat nul", file=sys.stderr)
        return 1
    return 0


def _st_roundtrip() -> int:
    """Check that a command survives the encode/decode round trip."""
    cmd = {"type": CMD_RESOURCE_CREATE_2D, "resource_id": 7,
           "format": FMT_B8G8R8A8_UNORM, "width": 1280, "height": 800}
    try:
        raw = encode(cmd)
        back = decode(raw)
    except ValueError:
        print("selftest: encode/decode echoue", file=sys.stderr)
        return 1
    if (back.get("type") != cmd["type"]
            or back.get("resource_id") != cmd["resource_id"]
            or back.get("width") != cmd["width"]
            or back.get("height") != cmd["height"]):
        print("selftest: aller-retour encode/decode divergent", file=sys.stderr)
        return 1
    return 0


def _st_reports(model: Model) -> int:
    """Check that the three report formats are not empty."""
    for fmt in FORMATS:
        text = report(fmt, model, [], "selftest")
        if not text:
            print(f"selftest: rapport {fmt} vide", file=sys.stderr)
            return 1
    return 0


def _st_cmdproc(model: Model) -> int:
    """Check that the command processor produces verdicts."""
    cmds = [{"type": 0x0100, "flags": 0, "fence_id": 0},
            {"type": CMD_RESOURCE_CREATE_2D, "resource_id": 7,
             "format": FMT_B8G8R8A8_UNORM, "width": 1280, "height": 800}]
    if not run(cmds, model):
        print("selftest: aucun verdict produit", file=sys.stderr)
        return 1
    return 0


def main(argv: list[str]) -> int:
    """Entry point of the CLI; returns the exit code of section 11."""
    if not argv:
        _usage()
        return EXIT_USAGE
    command = argv[0]
    if command == "selftest":
        if len(argv) != 1:
            _fail("toile: selftest ne prend aucun argument")
            _usage()
            return EXIT_USAGE
        return _selftest()
    if command not in ("run", "wire", "golden"):
        _fail(f"toile: sous-commande inconnue: {command}")
        _usage()
        return EXIT_USAGE
    opts, code = _parse_args(argv[1:])
    if opts is None:
        _usage()
        return code
    if command == "run":
        return _cmd_run(opts, wire=False)
    if command == "wire":
        return _cmd_run(opts, wire=True)
    return _cmd_golden(opts)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
