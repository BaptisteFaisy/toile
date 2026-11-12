#!/usr/bin/env python3
"""Garde-fou « budget CI » : tout job GitHub Actions doit être borné à 5 min.

Règle user 2026-10-24 (~/Bureau/CI-BUDGET-5MIN.md, CONTRACT.md §13).
Bibliothèque standard uniquement : le runner self-hosted r630-toile-01 n'a
pas PyYAML garanti, on utilise un mini-parseur YAML ligne à ligne (jamais
de dépendance tierce).

Analyse : on repère la section `jobs:` et, pour chaque job (clé à 2
espaces), son `timeout-minutes:` à 4 espaces.

Erreurs (exit 1) : job sans `timeout-minutes`, ou timeout > MAX_MINUTES.
Avertissements (exit 0) : `concurrency` absent ; `fetch-depth` != 1 non
justifié par un commentaire proche (la seule entorse admise du dépôt est
hermes-conflict-resolver : fetch-depth 0, rebase oblige, commentée).

Paramétrage : argv[1] = limite en minutes, argv[2] = racine du dépôt ;
variables CI_BUDGET_MAX_MINUTES et CI_BUDGET_ROOT sinon. SKIP_FILES liste
les jobs longs HORS PR justifiés (aucun sur ce dépôt pour l'instant).
"""

import glob
import os
import re
import sys

MAX_MINUTES = 5
WORKFLOWS = ".github/workflows"
SKIP_FILES = set()  # jobs longs hors PR : les lister ici + justification YAML

TOP_KEY = re.compile(r"^[A-Za-z_][A-Za-z0-9_-]*:")
JOB_KEY = re.compile(r"^  ([A-Za-z0-9_.-]+):\s*(#.*)?$")
TIMEOUT = re.compile(r"^    timeout-minutes:\s*(\d+)\s*(#.*)?$")
FETCH_DEPTH = re.compile(r"^\s*fetch-depth:\s*(\d+)\s*(#.*)?$")


def read_lines(path):
    with open(path, encoding="utf-8") as handle:
        return handle.read().splitlines()


def scan_jobs(lines):
    """Retourne [(nom, timeout|None)] pour la section jobs: du fichier."""
    jobs = []
    in_jobs = False
    for line in lines:
        if line.startswith("jobs:"):
            in_jobs = True
            continue
        if in_jobs and TOP_KEY.match(line):
            in_jobs = False
        if not in_jobs:
            continue
        match = JOB_KEY.match(line)
        if match:
            jobs.append([match.group(1), None])
            continue
        match = TIMEOUT.match(line)
        if match and jobs:
            jobs[-1][1] = int(match.group(1))
    return jobs


def fetch_depth_justified(lines, index):
    """Un fetch-depth != 1 est admis s'il est expliqué par un commentaire
    proche (même ligne, ou 4 lignes avant) — ex. hermes-conflict-resolver."""
    start = max(0, index - 4)
    window = lines[start:index + 1]
    return any("#" in line for line in window)


def check_fetch_depth(lines, rel, warnings):
    for index, line in enumerate(lines):
        match = FETCH_DEPTH.match(line)
        if not match:
            continue
        depth = int(match.group(1))
        if depth == 1 or fetch_depth_justified(lines, index):
            continue
        warnings.append(
            "%s: fetch-depth: %d (ligne %d) non justifié par un commentaire"
            % (rel, depth, index + 1))


def check_file(path, max_minutes, errors, warnings):
    lines = read_lines(path)
    jobs = scan_jobs(lines)
    if not jobs:
        warnings.append("%s: aucun job détecté (workflow vide ?)" % path)
        return
    for name, timeout in jobs:
        if timeout is None:
            errors.append(
                "%s: job '%s' sans timeout-minutes (défaut 360 min, interdit)"
                % (path, name))
        elif timeout > max_minutes:
            errors.append(
                "%s: job '%s' timeout-minutes: %d > %d (budget CI)"
                % (path, name, timeout, max_minutes))
    if not any(line.startswith("concurrency:") for line in lines):
        warnings.append("%s: pas de concurrency (annulation des runs obsolètes)"
                        % path)
    if "actions/checkout" in "\n".join(lines):
        check_fetch_depth(lines, path, warnings)


def parse_config(argv):
    """Limite et racine : argv prioritaire, sinon variables d'environnement."""
    max_minutes = int(argv[1]) if len(argv) > 1 else int(
        os.environ.get("CI_BUDGET_MAX_MINUTES", MAX_MINUTES))
    root = argv[2] if len(argv) > 2 else os.environ.get("CI_BUDGET_ROOT", ".")
    return max_minutes, root


def collect_workflows(root):
    paths = sorted(glob.glob(os.path.join(root, WORKFLOWS, "*.yml")))
    paths += sorted(glob.glob(os.path.join(root, WORKFLOWS, "*.yaml")))
    return [p for p in paths if os.path.basename(p) not in SKIP_FILES]


def main(argv):
    max_minutes, root = parse_config(argv)
    paths = collect_workflows(root)

    errors, warnings = [], []
    if not paths:
        print("::warning::Aucun workflow dans %s" % WORKFLOWS)
    for path in paths:
        check_file(os.path.relpath(path, root), max_minutes, errors, warnings)

    for message in warnings:
        print("::warning::%s" % message)
    for message in errors:
        print("::error::%s" % message)

    print("%d workflow(s) contrôlé(s) : %d erreur(s), %d avertissement(s)"
          % (len(paths), len(errors), len(warnings)))
    if errors:
        print("Règle : CI ≤ %d min par job — voir ~/Bureau/CI-BUDGET-5MIN.md"
              % max_minutes)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
