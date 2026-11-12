#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Garde « API gelée » du dépôt Toile (stdlib uniquement).

Règle (CONTRACT.md §13) : chaque signature de l’API publique C gelée
(CONTRACT.md §10) est présente dans `core/include/toile/*.h`, et chaque
symbole `toile_*` a un équivalent Python dans `host/toile/*.py` (préfixe
`toile_` retiré). Sortie : une ligne par manque, avec le nom exact et le
fichier attendu ; exit 1 s’il y en a au moins un, 0 sinon. Options :
--verbose, --allow-missing-python (tant que le jumeau n’est pas écrit).
"""

import argparse
import ast
import os
import re
import sys

CONTRACT = "CONTRACT.md"
HEADER_DIR = os.path.join("core", "include", "toile")
PYTHON_DIR = os.path.join("host", "toile")
HEADER_RE = re.compile(r"/\*\s*([A-Za-z0-9_.\-]+\.h)\s*\*/")
NAME_RE = re.compile(r"\b(toile_[A-Za-z0-9_]+)\s*\(")


def repo_root():
    """Racine du dépôt : trois niveaux au-dessus de .github/checks/."""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def section(text, number):
    """Contenu de la section `## <number>` (jusqu'à la section suivante)."""
    kept, inside = [], False
    for line in text.splitlines():
        if line.startswith("## %d" % number):
            inside = True
            continue
        if inside and line.startswith("## "):
            break
        if inside:
            kept.append(line)
    return "\n".join(kept)


def code_blocks(text):
    """Blocs de code (entre triple accents graves) d'un texte."""
    blocks, current, inside = [], [], False
    for line in text.splitlines():
        if line.strip().startswith("```"):
            if inside:
                blocks.append("\n".join(current))
                current, inside = [], False
            else:
                inside = True
            continue
        if inside:
            current.append(line)
    return blocks


def normalize(text):
    """Forme canonique d'une signature : blancs réduits, `*` et `,` serrés."""
    text = " ".join(text.split())
    text = text.replace(" *", "*").replace("* ", "*")
    text = text.replace(" ,", ",").replace(", ", ",")
    text = text.replace(" (", "(").replace("( ", "(")
    text = text.replace(" ;", ";").replace("; ", ";")
    return text.strip().rstrip(";").strip()


def contract_signatures(root):
    """[(en-tête attendu, signature normalisée, signature brute)] de §10."""
    path = os.path.join(root, CONTRACT)
    if not os.path.isfile(path):
        raise SystemExit("%s introuvable : l'API gelée ne peut pas être vérifiée"
                         % CONTRACT)
    blocks = code_blocks(section(read_text(path), 10))
    signatures = []
    for block in blocks:
        header, buffer = None, []
        for line in block.splitlines():
            found = HEADER_RE.search(line)
            if found:
                header = found.group(1)
                continue
            if not line.strip() or line.strip().startswith(("/*", "*", "//")):
                continue
            buffer.append(line.strip())
            if not line.rstrip().endswith(";"):
                continue
            raw = " ".join(buffer)
            buffer = []
            signatures.append((header, normalize(raw), raw))
        if buffer:
            raw = " ".join(buffer)
            signatures.append((header, normalize(raw), raw))
    return signatures


def header_files(root):
    """{nom d'en-tête: chemin absolu} de core/include/toile/."""
    directory = os.path.join(root, HEADER_DIR)
    if not os.path.isdir(directory):
        return {}
    return {name: os.path.join(directory, name)
            for name in sorted(os.listdir(directory)) if name.endswith(".h")}


def normalized_headers(root):
    """{nom d'en-tête: texte normalisé} pour la recherche des signatures."""
    return {name: normalize(read_text(path))
            for name, path in header_files(root).items()}


def python_names(root):
    """Ensemble des noms de fonctions définies dans host/toile/*.py."""
    directory = os.path.join(root, PYTHON_DIR)
    names = set()
    if not os.path.isdir(directory):
        return names
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".py"):
            continue
        try:
            tree = ast.parse(read_text(os.path.join(directory, name)))
        except (OSError, SyntaxError):
            continue
        for node in ast.walk(tree):
            if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
                names.add(node.name)
            elif isinstance(node, ast.Assign):
                for target in node.targets:
                    if isinstance(target, ast.Name):
                        names.add(target.id)
    return names


def check_headers(signatures, headers, verbose):
    """Manques de signature dans les en-têtes gelés."""
    problems = []
    for header, signature, raw in signatures:
        expected = header or "?"
        if header and header in headers:
            found = signature in headers[header]
        else:
            found = any(signature in text for text in headers.values())
        if verbose:
            print("[api-frozen] %s → %s : %s"
                  % (expected, "OK" if found else "ABSENT", signature))
        if not found:
            problems.append("%s : signature absente de %s/%s — %s"
                            % (expected, HEADER_DIR, header or "*.h", raw))
    return problems


def check_python(signatures, names, verbose):
    """Manques d'équivalent Python (préfixe `toile_` retiré)."""
    problems = []
    for header, signature, raw in signatures:
        for symbol in set(NAME_RE.findall(signature)):
            twin = symbol[len("toile_"):]
            if verbose:
                print("[api-frozen] jumeau Python %s → %s" % (symbol, twin))
            if twin not in names:
                problems.append("%s : jumeau Python `%s` absent de %s/*.py "
                                "(attendu `%s`)" % (symbol, twin, PYTHON_DIR, twin))
    return problems


def main():
    parser = argparse.ArgumentParser(description="Garde API gelée du dépôt Toile")
    parser.add_argument("--verbose", action="store_true",
                        help="journalise chaque signature contrôlée")
    parser.add_argument("--allow-missing-python", action="store_true",
                        help="ne bloque pas si le jumeau Python manque encore")
    args = parser.parse_args()
    root = repo_root()
    signatures = contract_signatures(root)
    if not signatures:
        print("[api-frozen] aucune signature trouvée dans %s §10" % CONTRACT)
        return 1
    headers = normalized_headers(root)
    problems = check_headers(signatures, headers, args.verbose)
    if not args.allow_missing_python:
        problems += check_python(signatures, python_names(root), args.verbose)
    for line in problems:
        print(line)
    print("[api-frozen] %d signature(s) §10, %d manque(s)"
          % (len(signatures), len(problems)))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
