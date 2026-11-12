#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Garde de taille du dépôt Toile (stdlib uniquement).

Règle (CONTRACT.md §13) : tout fichier versionné fait moins de 500 lignes,
toute fonction C ou Python fait moins de 50 lignes. Exclusions : LICENSE,
NOTICE, TOILE-cahier-des-charges.md, fixtures/**, scenarios/** et tous les
*.md. Sortie : une ligne `fichier:lignes (limite)` par contrevenant ; exit 1
s'il y en a au moins un, 0 sinon. Options : --verbose, --limit-file,
--limit-func.
"""

import argparse
import ast
import bisect
import os
import re
import subprocess
import sys

DEFAULT_LIMIT_FILE = 500
DEFAULT_LIMIT_FUNC = 50
EXCLUDED_NAMES = ("LICENSE", "NOTICE", "TOILE-cahier-des-charges.md")
EXCLUDED_DIRS = ("fixtures", "scenarios")
SKIP_DIRS = (".git", "build", "__pycache__", ".qnx")
C_KEYWORDS = ("if", "for", "while", "switch", "do", "else", "return", "goto",
              "sizeof", "case", "default")
C_TOKEN_RE = re.compile(r"//[^\n]*|/\*.*?\*/|\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'",
                         re.S)
IDENT_RE = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*$")


def repo_root():
    """Racine du dépôt : trois niveaux au-dessus de .github/checks/."""
    here = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(here, "..", ".."))


def versioned_files(root):
    """Fichiers versionnés (suivis + non suivis non ignorés), sinon l'arbre."""
    try:
        proc = subprocess.run(
            ["git", "ls-files", "--cached", "--others", "--exclude-standard"],
            capture_output=True, text=True, timeout=60, cwd=root)
        if proc.returncode == 0 and proc.stdout.strip():
            return sorted({l.strip() for l in proc.stdout.splitlines() if l.strip()})
    except (OSError, subprocess.SubprocessError):
        pass
    found = []
    for base, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        found += [os.path.relpath(os.path.join(base, n), root).replace(os.sep, "/")
                  for n in files]
    return sorted(found)


def is_excluded(rel):
    """True si le chemin est exclu de la garde (docs, fixtures, scénarios)."""
    parts = rel.split("/")
    if parts[0] in EXCLUDED_DIRS or any(p in EXCLUDED_DIRS for p in parts[:-1]):
        return True
    return parts[-1] in EXCLUDED_NAMES or parts[-1].endswith(".md")


def read_text(path):
    """Contenu texte du fichier, ou None si binaire/illisible."""
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return handle.read()
    except (OSError, UnicodeDecodeError):
        return None


def strip_c_noise(text):
    """Neutralise commentaires et littéraux (longueur et lignes conservées)."""
    return C_TOKEN_RE.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)


def line_starts(clean):
    """Positions de début de chaque ligne du texte nettoyé."""
    starts, index = [0], clean.find("\n")
    while index >= 0:
        starts.append(index + 1)
        index = clean.find("\n", index + 1)
    return starts


def line_of(starts, position):
    """Numéro de ligne (1-based) d'une position dans le texte."""
    return bisect.bisect_right(starts, position)


def identifier_before(text, position):
    """(identifiant, position) juste avant `position` (None si absent)."""
    found = IDENT_RE.search(text, 0, position)
    if not found:
        return None, -1
    return found.group(1), found.start(1)


def matching_paren(text, position):
    """Parenthèse ouvrante correspondant à la fermante `position`."""
    depth = 0
    for index in range(position, -1, -1):
        if text[index] == ")":
            depth += 1
        elif text[index] == "(":
            depth -= 1
            if depth == 0:
                return index
    return -1


def matching_brace(clean, brace):
    """Accolade fermante correspondant à l'accolade ouvrante."""
    depth = 0
    for index in range(brace, len(clean)):
        if clean[index] == "{":
            depth += 1
        elif clean[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def statement_start(clean, position):
    """(début, segment) de la déclaration contenant `position`."""
    limit = max(0, position - 600)
    cut = -1
    for mark in (";", "}", "{"):
        found = clean.rfind(mark, limit, position)
        if found > cut:
            cut = found
    start = cut + 1
    while start < position and clean[start].isspace():
        start += 1
    return start, clean[start:position]


def function_at(clean, brace, starts):
    """(nom, ligne_début, ligne_fin) de la fonction ouverte à `brace`."""
    close = clean.rfind(")", 0, brace)
    if close < 0:
        return None
    open_paren = matching_paren(clean, close)
    if open_paren < 0:
        return None
    name, ident = identifier_before(clean, open_paren)
    if not name or name in C_KEYWORDS:
        return None
    start, segment = statement_start(clean, ident)
    if "=" in segment or "typedef" in segment:
        return None
    end = matching_brace(clean, brace)
    if end < 0:
        return None
    return (name, line_of(starts, start), line_of(starts, end))


def c_functions(text):
    """[(nom, ligne_début, ligne_fin)] des fonctions définies au niveau 0."""
    clean = strip_c_noise(text)
    starts = line_starts(clean)
    functions, depth, index, size = [], 0, 0, len(clean)
    while index < size:
        char = clean[index]
        if char == "{":
            if depth == 0:
                found = function_at(clean, index, starts)
                if found:
                    functions.append(found)
            depth += 1
        elif char == "}":
            depth = max(0, depth - 1)
        index += 1
    return functions


def python_functions(text):
    """[(nom, ligne_début, ligne_fin)] des fonctions Python du module."""
    try:
        tree = ast.parse(text)
    except SyntaxError:
        return []
    found = []
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            end = getattr(node, "end_lineno", node.lineno) or node.lineno
            found.append((node.name, node.lineno, end))
    return sorted(found, key=lambda item: item[1])


def check_file(rel, root, limit_file, limit_func, verbose):
    """Contrevenants d'un fichier : [lignes de sortie]."""
    text = read_text(os.path.join(root, rel))
    if text is None:
        if verbose:
            print("[taille] %s : binaire ou illisible, ignoré" % rel)
        return []
    violations = []
    count = len(text.splitlines())
    if count >= limit_file:
        violations.append("%s:%d (%d)" % (rel, count, limit_file))
    ext = os.path.splitext(rel)[1]
    if ext in (".c", ".h"):
        functions = c_functions(text)
    elif ext == ".py":
        functions = python_functions(text)
    else:
        functions = []
    for name, start, end in functions:
        size = end - start + 1
        if size >= limit_func:
            violations.append("%s:%d (%d) fonction=%s@%d"
                              % (rel, size, limit_func, name, start))
    if verbose and not violations:
        print("[taille] %s : %d lignes, %d fonction(s) OK"
              % (rel, count, len(functions)))
    return violations


def main():
    """Lance la garde ; retourne le code de sortie (1 si contrevenant)."""
    parser = argparse.ArgumentParser(description="Garde de taille du dépôt Toile")
    parser.add_argument("--verbose", action="store_true", help="journalise les fichiers")
    parser.add_argument("--limit-file", type=int, default=DEFAULT_LIMIT_FILE,
                        help="limite de lignes par fichier")
    parser.add_argument("--limit-func", type=int, default=DEFAULT_LIMIT_FUNC,
                        help="limite de lignes par fonction")
    args = parser.parse_args()
    root = repo_root()
    violations, checked = [], 0
    for rel in versioned_files(root):
        if is_excluded(rel):
            continue
        checked += 1
        violations += check_file(rel, root, args.limit_file, args.limit_func,
                                 args.verbose)
    for line in violations:
        print(line)
    print("[taille] %d contrevenant(s) sur %d fichier(s) contrôlé(s)"
          % (len(violations), checked))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
