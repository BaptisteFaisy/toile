#!/usr/bin/env python3
"""Harnais différentiel Toile : C (`build/toile-run`) vs jumeau Python (`toile.cli`).

Contrat gelé : CONTRACT.md §11 (CLI, sous-commandes run/wire, formats text/json/md)
et §4 (fixtures .tcl, fixtures/wire/*.hex). Comparaison **octet pour octet** de
stdout + codes de sortie ; le binaire C est la référence des goldens.

Ce harnais ne corrige JAMAIS les implémentations : il constate et rapporte.

Codes de sortie : 0 identique partout, 1 divergence, 2 harnais inutilisable.
"""

from __future__ import annotations

import argparse
import difflib
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import diff_lib as dl  # noqa: E402

ROOT_DEFAULT = Path(__file__).resolve().parent.parent


def unified(a: bytes, b: bytes, label_a: str, label_b: str) -> str:
    """Diff unifié lisible (décodage UTF-8 tolérant, les octets restent la vérité)."""
    ta = a.decode("utf-8", errors="replace").splitlines(keepends=True)
    tb = b.decode("utf-8", errors="replace").splitlines(keepends=True)
    body = difflib.unified_diff(ta, tb, fromfile=label_a, tofile=label_b, n=3)
    return "".join(body).rstrip("\n")


def _indent(text: str, prefix: str = "    ") -> list:
    return [prefix + line for line in text.splitlines()]


def describe_failure(cmp_: dl.Comparison) -> str:
    """Bloc lisible d'un échec : fixture, format, commandes, diff unifié, octet."""
    lines = [f"ÉCHEC {cmp_.target.name}",
             f"  C      : {cmp_.c.cmd}",
             f"  Python : {cmp_.py.cmd}"]
    lines += [f"  - {p}" for p in cmp_.problems]
    if not cmp_.c.failed and not cmp_.py.failed:
        body = unified(cmp_.c.stdout, cmp_.py.stdout,
                       f"C {cmp_.target.name}", f"python {cmp_.target.name}")
        if body:
            lines.append("  diff unifié C -> python :")
            lines += _indent(body)
    if cmp_.golden is not None:
        lines += describe_golden_diff(cmp_)
    return "\n".join(lines)


def describe_golden_diff(cmp_: dl.Comparison) -> list:
    lines = []
    if dl.first_diff(cmp_.c.stdout, cmp_.golden) is not None:
        body = unified(cmp_.golden, cmp_.c.stdout, "golden", f"C {cmp_.target.name}")
        lines.append("  diff unifié golden -> C :")
        lines += _indent(body)
    if dl.first_diff(cmp_.py.stdout, cmp_.golden) is not None:
        body = unified(cmp_.golden, cmp_.py.stdout, "golden", f"python {cmp_.target.name}")
        lines.append("  diff unifié golden -> python :")
        lines += _indent(body)
    return lines


def render_report(comparisons: list, golden_checked: int, warnings: list) -> str:
    """Rapport final : détails des échecs, avertissements, puis la ligne de synthèse."""
    fixtures = len({c.target.rel for c in comparisons})
    failures = [c for c in comparisons if not c.ok]
    lines = []
    for cmp_ in failures:
        lines.append(describe_failure(cmp_))
        lines.append("")
    lines += [f"avertissement : {w}" for w in warnings]
    extra = f", {golden_checked} comparées au golden" if golden_checked else ""
    lines.append(f"rapport : {fixtures} fixtures, {len(comparisons)} comparaisons, "
                 f"{len(failures)} échecs{extra}")
    return "\n".join(lines)


def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="tests/differential.py",
        description="Différentiel C <-> Python sur toutes les fixtures (CONTRACT.md §11).")
    p.add_argument("--write-golden", action="store_true",
                   help="écrit fixtures/golden/<nom>.<fmt> depuis le binaire C (référence)")
    p.add_argument("--verbose", action="store_true", help="une ligne par comparaison")
    p.add_argument("--jobs", type=int, default=4, help="exécutions parallèles (défaut 4)")
    p.add_argument("--filter", default="", metavar="MOTIF",
                   help="ne garde que les fixtures dont le chemin contient MOTIF")
    p.add_argument("--c-bin", default=dl.DEFAULT_C_BIN,
                   help=f"binaire C (défaut {dl.DEFAULT_C_BIN})")
    p.add_argument("--py-cmd", default=dl.DEFAULT_PY_CMD,
                   help=f"jumeau Python (défaut {dl.DEFAULT_PY_CMD!r})")
    p.add_argument("--root", default="",
                   help="racine du dépôt (défaut : parent de tests/, calculé depuis __file__)")
    p.add_argument("--timeout", type=int, default=120,
                   help="timeout par invocation, secondes (défaut 120)")
    return p


def run_parallel(args, root: Path, c_bin: Path, env: dict, targets: list) -> list:
    """Comparaisons concurrentes, résultat dans l'ordre des fixtures (rapport stable)."""
    jobs = max(1, args.jobs)
    results = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(dl.compare_target, root, t, c_bin,
                               args.py_cmd, env, args.timeout) for t in targets]
        for future in futures:
            cmp_ = future.result()
            results.append(cmp_)
            if args.verbose:
                etat = "OK   " if cmp_.ok else "ÉCHEC"
                print(f"{etat} {cmp_.target.name} "
                      f"(exit C={cmp_.c.returncode} python={cmp_.py.returncode})")
    return results


def compare_all(args, root: Path, c_bin: Path, env: dict, targets: list) -> int:
    comparisons = run_parallel(args, root, c_bin, env, targets)
    golden_checked = sum(1 for c in comparisons if c.golden is not None)
    warnings = sorted({f"{c.target.name} : golden absent ({c.target.golden_rel})"
                       for c in comparisons if c.golden_missing})
    print(render_report(comparisons, golden_checked, warnings))
    if all(c.ok for c in comparisons):
        return dl.EXIT_OK
    return dl.EXIT_DIVERGENCE


def write_goldens(args, root: Path, c_bin: Path, env: dict, targets: list) -> int:
    """Mode --write-golden : le binaire C seul décide du contenu des goldens."""
    written, problems, warnings, seen = [], [], [], {}
    for target in targets:
        path, problem = dl.write_golden(root, target, c_bin, env, args.timeout)
        if problem:
            problems.append(problem)
            continue
        written.append(path)
        if args.verbose:
            print(f"golden écrit : {target.golden_rel} ({len(path.read_bytes())} o)")
        first = seen.setdefault(target.golden_rel, target.rel)
        if first != target.rel:
            warnings.append(f"collision de nom de golden : {target.golden_rel} "
                            f"écrit par {first} puis {target.rel}")
    for line in problems:
        print(f"échec : {line}", file=sys.stderr)
    for line in warnings:
        print(f"avertissement : {line}")
    fixtures = len({t.rel for t in targets})
    print(f"rapport : {fixtures} fixtures, {len(written)} goldens écrits, "
          f"{len(problems)} échecs")
    return dl.EXIT_OK if not problems else dl.EXIT_DIVERGENCE


def no_fixture_message(root: Path, motif: str) -> str:
    if motif:
        return (f"harnais inutilisable : aucune fixture ne correspond au filtre {motif!r} "
                f"sous {root}.")
    return ("harnais inutilisable : aucune fixture trouvée sous "
            f"{root} (fixtures/*.tcl, fixtures/fautes/, fixtures/invalides/, "
            "fixtures/wire/*.hex).\n  lot fixtures/ pas encore livré.")


def main(argv=None) -> int:
    args = build_parser().parse_args(argv)
    root = Path(args.root).resolve() if args.root else ROOT_DEFAULT
    try:
        c_bin = dl.check_harness(root, args.c_bin, args.py_cmd)
    except dl.HarnessError as exc:
        print(str(exc), file=sys.stderr)
        return dl.EXIT_UNUSABLE
    targets = dl.discover(root, args.filter)
    if not targets:
        print(no_fixture_message(root, args.filter), file=sys.stderr)
        return dl.EXIT_UNUSABLE
    env = dl.env_for(root)
    if args.write_golden:
        return write_goldens(args, root, c_bin, env, targets)
    return compare_all(args, root, c_bin, env, targets)


if __name__ == "__main__":
    sys.exit(main())
