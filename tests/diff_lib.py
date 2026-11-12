"""Bibliothèque du harnais différentiel C <-> Python (CONTRACT.md §11, §4).

Aucune correction des implémentations ici : on constate et on rapporte.
Le binaire C (`build/toile-run`) est la référence des goldens.
"""

from __future__ import annotations

import os
import shlex
import shutil
import subprocess
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

FORMATS = ("text", "json", "md")
# répertoire de fixtures (relatif à la racine) -> sous-commande CLI (§11)
FIXTURE_DIRS = (
    ("fixtures", "run"),
    ("fixtures/fautes", "run"),
    ("fixtures/invalides", "run"),
    ("fixtures/wire", "wire"),
)
GOLDEN_DIR = "fixtures/golden"
DEFAULT_C_BIN = "build/toile-run"
DEFAULT_PY_CMD = "python3 -m toile.cli"

EXIT_OK = 0
EXIT_DIVERGENCE = 1
EXIT_UNUSABLE = 2


class HarnessError(Exception):
    """Harnais inutilisable (code de sortie 2) : message explicite pour l'user."""


@dataclass(frozen=True)
class Target:
    """Une fixture x un format = une comparaison."""

    rel: str          # chemin POSIX relatif à la racine, ex. fixtures/a.tcl
    fmt: str          # text | json | md
    sub: str          # run | wire

    @property
    def name(self) -> str:
        return f"{self.rel} [{self.fmt}]"

    @property
    def golden_rel(self) -> str:
        """fixtures/golden/<chemin aplati>.<fmt> — unique par fixture.

        Le chemin relatif à `fixtures/` est aplati en tirets : deux fixtures
        de même nom dans des dossiers différents (fixtures/curseur.tcl et
        fixtures/wire/curseur.hex) n'entrent pas en collision.
        """
        parts = list(Path(self.rel).parts)
        if parts and parts[0] == "fixtures":
            parts = parts[1:]
        stem = parts[-1] if parts else "fixture"
        dot = stem.rfind(".")
        if dot > 0:
            stem = stem[:dot]
        flat = "-".join(parts[:-1] + [stem])
        return f"{GOLDEN_DIR}/{flat}.{self.fmt}"


@dataclass
class Outcome:
    """Résultat brut d'une exécution (stdout en octets, jamais décodé)."""

    argv: list
    returncode: int = 0
    stdout: bytes = b""
    stderr: bytes = b""
    error: str = ""

    @property
    def failed(self) -> bool:
        return bool(self.error)

    @property
    def cmd(self) -> str:
        return " ".join(shlex.quote(str(a)) for a in self.argv)


@dataclass
class Comparison:
    target: Target
    c: Outcome
    py: Outcome
    golden: Optional[bytes] = None
    golden_missing: bool = False
    problems: list = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.problems


def discover(root: Path, motif: str = "") -> list:
    """Fixtures *.tcl (run) et wire/*.hex (wire) x 3 formats, triées."""
    targets = []
    for sub_rel, sub in FIXTURE_DIRS:
        base = root / sub_rel
        if not base.is_dir():
            continue
        ext = ".hex" if sub == "wire" else ".tcl"
        for path in sorted(base.glob(f"*{ext}")):
            if not path.is_file():
                continue
            rel = path.relative_to(root).as_posix()
            if motif and motif not in rel:
                continue
            for fmt in FORMATS:
                targets.append(Target(rel=rel, fmt=fmt, sub=sub))
    return targets


def resolve_path(root: Path, raw: str) -> Optional[Path]:
    """Chemin absolu, sinon relatif au cwd, sinon relatif à la racine."""
    p = Path(raw)
    if p.is_absolute():
        return p if p.exists() else None
    for cand in (Path.cwd() / p, root / p):
        if cand.exists():
            return cand
    return None


def _missing_cbin_message(c_bin: str, is_default: bool) -> str:
    if is_default:
        return (
            "harnais inutilisable : binaire C absent "
            f"({c_bin} introuvable).\n"
            "  commande à lancer : make build   (cible gelée, CONTRACT.md §12)"
        )
    return (
        "harnais inutilisable : binaire C absent "
        f"({c_bin} introuvable ou non exécutable, option --c-bin)."
    )


def _missing_py_message(py_cmd: str, is_default: bool, root: Path) -> str:
    if is_default:
        return (
            "harnais inutilisable : jumeau Python absent "
            f"(host/toile/cli.py introuvable sous {root}).\n"
            "  commande à lancer : make build   (puis vérifie le lot host/)\n"
            f"  invocation attendue : PYTHONPATH=host {py_cmd} run <fixture>"
        )
    return (
        "harnais inutilisable : commande Python introuvable "
        f"({py_cmd!r}, option --py-cmd)."
    )


def env_for(root: Path) -> dict:
    """Environnement des deux implémentations : cwd = racine, PYTHONPATH = host/."""
    env = os.environ.copy()
    host = str(root / "host")
    old = env.get("PYTHONPATH", "")
    env["PYTHONPATH"] = f"{host}:{old}" if old else host
    return env


def build_argv_c(c_bin: Path, target: Target) -> list:
    """build/toile-run <run|wire> <fixture> --format <fmt>"""
    return [str(c_bin), target.sub, target.rel, "--format", target.fmt]


def build_argv_py(py_cmd: str, target: Target) -> list:
    """python3 -m toile.cli <run|wire> <fixture> --format <fmt>"""
    return shlex.split(py_cmd) + [target.sub, target.rel, "--format", target.fmt]


def run_one(argv: list, root: Path, env: dict, timeout: int) -> Outcome:
    """Exécution brute : octets + code de sortie, jamais d'exception remontée."""
    out = Outcome(argv=argv)
    try:
        proc = subprocess.run(
            argv, cwd=str(root), env=env, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, timeout=timeout, check=False)
    except FileNotFoundError as exc:
        out.error = f"commande introuvable : {exc}"
        return out
    except subprocess.TimeoutExpired:
        out.error = f"timeout après {timeout} s"
        return out
    except OSError as exc:
        out.error = f"erreur d'exécution : {exc}"
        return out
    out.returncode = proc.returncode
    out.stdout = proc.stdout or b""
    out.stderr = proc.stderr or b""
    return out


def check_harness(root: Path, c_bin_raw: str, py_cmd: str) -> Path:
    """Valide binaire C + jumeau Python ; lève HarnessError (exit 2)."""
    if not root.is_dir():
        raise HarnessError(f"harnais inutilisable : racine introuvable ({root}).")
    default_c = c_bin_raw == DEFAULT_C_BIN
    c_bin = resolve_path(root, c_bin_raw)
    if c_bin is None or not os.access(c_bin, os.X_OK):
        raise HarnessError(_missing_cbin_message(c_bin_raw, default_c))
    if py_cmd == DEFAULT_PY_CMD:
        twin = root / "host" / "toile" / "cli.py"
        if shutil.which("python3") is None or not twin.is_file():
            raise HarnessError(_missing_py_message(py_cmd, True, root))
    else:
        head = shlex.split(py_cmd)[0]
        if "/" in head:
            found = resolve_path(root, head)
            ok = found is not None and os.access(found, os.X_OK)
        else:
            ok = shutil.which(head) is not None
        if not ok:
            raise HarnessError(_missing_py_message(py_cmd, False, root))
    return c_bin


def first_diff(a: bytes, b: bytes) -> Optional[int]:
    """Position (0-based) du premier octet différent ; None si identiques."""
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return i
    if len(a) != len(b):
        return min(len(a), len(b))
    return None


def _golden_bytes(root: Path, target: Target) -> tuple:
    """(octets | None, golden_manquant) — golden actif seulement si le dossier est peuplé."""
    gdir = root / GOLDEN_DIR
    if not gdir.is_dir() or not any(p.is_file() for p in gdir.iterdir()):
        return None, False
    path = root / target.golden_rel
    if not path.is_file():
        return None, True
    return path.read_bytes(), False


def compare_target(root: Path, target: Target, c_bin: Path,
                   py_cmd: str, env: dict, timeout: int) -> Comparison:
    """Une comparaison : C vs Python (+ golden s'il existe)."""
    c = run_one(build_argv_c(c_bin, target), root, env, timeout)
    py = run_one(build_argv_py(py_cmd, target), root, env, timeout)
    golden, missing = _golden_bytes(root, target)
    cmp_ = Comparison(target=target, c=c, py=py, golden=golden, golden_missing=missing)
    _collect_problems(cmp_)
    return cmp_


def _collect_problems(cmp_: Comparison) -> None:
    if cmp_.c.failed:
        cmp_.problems.append(f"C en échec : {cmp_.c.cmd} -> {cmp_.c.error}")
    if cmp_.py.failed:
        cmp_.problems.append(f"Python en échec : {cmp_.py.cmd} -> {cmp_.py.error}")
    if cmp_.c.failed or cmp_.py.failed:
        return
    if cmp_.c.returncode != cmp_.py.returncode:
        cmp_.problems.append(
            f"codes de sortie différents : C={cmp_.c.returncode} "
            f"python={cmp_.py.returncode}")
    pos = first_diff(cmp_.c.stdout, cmp_.py.stdout)
    if pos is not None:
        cmp_.problems.append(
            f"stdout différents : premier octet différent en position {pos} "
            f"(C={len(cmp_.c.stdout)} o, python={len(cmp_.py.stdout)} o)")
    if cmp_.golden is not None:
        if first_diff(cmp_.c.stdout, cmp_.golden) is not None:
            cmp_.problems.append("C diverge du golden (référence) — golden à régénérer ?")
        if first_diff(cmp_.py.stdout, cmp_.golden) is not None:
            cmp_.problems.append("Python diverge du golden")


def write_golden(root: Path, target: Target, c_bin: Path,
                 env: dict, timeout: int) -> tuple:
    """Écrit fixtures/golden/<nom-sans-extension>.<fmt> depuis le binaire C (référence).

    Retourne (chemin écrit | None, problème | None). Le Python n'est jamais consulté.
    """
    out = run_one(build_argv_c(c_bin, target), root, env, timeout)
    if out.failed:
        return None, f"{target.name} : C en échec -> {out.error}"
    path = root / target.golden_rel
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(out.stdout)
    return path, None
