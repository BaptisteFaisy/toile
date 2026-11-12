#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Hermes conflict resolver — resolves a real merge conflict on a Toile pull
request (C11 + Python, frozen API in CONTRACT.md §10).

Path 1, deterministic: `git rebase origin/<base>`. If it applies alone, push
`--force-with-lease`, drop `hermes/conflit`, add `hermes/approuvee`, comment the
PR, exit 0. No AI involved.
Path 2, InferHub (OpenAI-compatible, model `combo/cheap`), only on a real
conflict: each conflicted file is resolved by the model, then verified (no
marker left, py_compile, gcc -fsyntax-only, repository guards) before any push.
Any failed verification → `git rebase --abort`, the label stays, the PR is
commented with the reason, exit 1: a broken push is never attempted. Every step
is logged on stdout — the runner log is the only proof.

Environment: PR_NUMBER, GITHUB_REPOSITORY (required), INFERHUB_API_KEY
(InferHub path only, never logged), GITHUB_TOKEN (git credentials).
Plumbing (process, GitHub API, prompt, InferHub call) lives in `conflict_lib.py`.
"""

import os
import shutil
import sys
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from conflict_lib import (  # noqa: E402  (path set just above)
    C_EXTS, C_FLAGS, GUARDS, LABEL_CONFLIT, LABEL_OK, MAX_ROUNDS, MODEL, PY_EXTS,
    ResolverError,
    build_prompt, clean_answer, comment, contracts_excerpt, env, extract_conflict_blocks,
    gh_api, git, git_names, has_markers, inferhub_resolve, log, read_text, redact, run,
    set_label, write_text,
)


def resolve_file(path, contracts):
    """Resolve one conflicted file with InferHub, write it and stage it."""
    original = read_text(path)
    blocks = extract_conflict_blocks(original)
    if not blocks:
        raise ResolverError("%s : aucune balise de conflit alors que git le signale "
                            "non fusionné — résolution manuelle requise" % path)
    log("%s : %d bloc(s) de conflit → InferHub (%s)" % (path, len(blocks), MODEL))
    resolved = clean_answer(inferhub_resolve(build_prompt(path, original, contracts)),
                            original)
    if has_markers(resolved):
        raise ResolverError("%s : la réponse InferHub contient encore des balises de "
                            "conflit" % path)
    write_text(path, resolved)
    git(["add", "--", path])
    log("%s : réécrit (%d → %d caractères) et ajouté à l'index"
        % (path, len(original), len(resolved)))


def continue_rebase():
    """`git rebase --continue`, falling back to `--skip` on an empty commit."""
    quiet = {"GIT_EDITOR": "true", "GIT_SEQUENCE_EDITOR": "true"}
    code, out, err = git(["rebase", "--continue"], check=False, timeout=180,
                         extra_env=quiet)
    if code != 0 and "no changes" in (err + out).lower():
        log("rebase : rien à committer sur ce commit → --skip")
        code = git(["rebase", "--skip"], check=False, timeout=180, extra_env=quiet)[0]
    return code


def ai_rebase(base, contracts):
    """Rebase onto origin/<base>, resolving each conflict round with InferHub."""
    if git(["rebase", "origin/" + base], check=False)[0] == 0:
        log("rebase : aucun conflit (la base a bougé entre-temps)")
        return
    for round_index in range(1, MAX_ROUNDS + 1):
        pending = git_names(["diff", "--name-only", "--diff-filter=U"])
        if pending:
            log("tour %d/%d : %d fichier(s) en conflit : %s"
                % (round_index, MAX_ROUNDS, len(pending), ", ".join(pending)))
            for path in pending:
                resolve_file(path, contracts)
        if continue_rebase() == 0:
            log("rebase terminé au tour %d" % round_index)
            return
        log("le rebase s'arrête encore après le tour %d" % round_index)
    raise ResolverError("le rebase n'a pas abouti après %d tours de résolution "
                        "InferHub" % MAX_ROUNDS)


def verify(base):
    """Raise ResolverError when any post-resolution verification fails."""
    touched = git_names(["diff", "--name-only", "origin/%s...HEAD" % base])
    for path in touched:
        if os.path.isfile(path) and has_markers(read_text(path)):
            raise ResolverError("%s : balise de conflit résiduelle" % path)
    log("vérification 1/3 : aucune balise de conflit (%d fichier(s))" % len(touched))
    check_python([p for p in touched if os.path.splitext(p)[1] in PY_EXTS])
    check_c([p for p in touched if os.path.splitext(p)[1] in C_EXTS])
    check_guards()


def check_python(paths):
    """`python3 -m py_compile` sur chaque .py touché."""
    if not paths:
        log("vérification 2/3 : aucun .py touché — py_compile sans objet")
        return
    code, out, err = run([sys.executable or "python3", "-m", "py_compile"] + paths,
                         timeout=120)
    if code != 0:
        raise ResolverError("syntaxe Python invalide — %s"
                            % redact((err or out).strip()[:300]))
    log("vérification 2/3 : py_compile OK (%d fichier(s) : %s)"
        % (len(paths), ", ".join(paths)))


def check_c(paths):
    """`gcc -fsyntax-only -std=c11 -Icore/include` sur chaque .c/.h touché.

    Sans gcc, on le dit BRUYAMMENT et on saute : un vérification sautée n'est
    jamais annoncée comme un succès (le workflow `ci` la rejouera).
    """
    if not paths:
        log("vérification 2b/3 : aucun .c/.h touché — gcc sans objet")
        return
    gcc = shutil.which("gcc")
    if not gcc:
        log("::warning::ATTENTION — `gcc` introuvable sur ce runner : la syntaxe C "
            "de %d fichier(s) N'EST PAS VÉRIFIÉE (%s). Ce n'est PAS un succès : "
            "le workflow `ci` doit les rejouer." % (len(paths), ", ".join(paths)))
        return
    for path in paths:
        code, out, err = run([gcc] + list(C_FLAGS) + [path], timeout=90)
        if code != 0:
            raise ResolverError("%s : syntaxe C invalide — %s"
                                % (path, redact((err or out).strip()[:300])))
    log("vérification 2b/3 : gcc -fsyntax-only OK (%d fichier(s) : %s)"
        % (len(paths), ", ".join(paths)))


def check_guards():
    """Les gardes du dépôt (taille.py, api-frozen.py), avec python3."""
    for guard in GUARDS:
        if not os.path.isfile(guard):
            log("::warning::ATTENTION — garde %s absente du dépôt : IGNORÉE, ce "
                "n'est pas un succès" % guard)
            continue
        code, out, err = run([sys.executable or "python3", guard], timeout=180)
        if code != 0:
            raise ResolverError("garde du dépôt en échec : %s — %s"
                                % (guard, redact((err or out).strip()[:400])))
        log("vérification 3/3 : %s OK" % guard)




def publish(repo, pr, head_ref, mode, detail=""):
    """Push the rebased branch, flip the labels and comment the PR."""
    number = pr["number"]
    log("push : HEAD → origin/%s (--force-with-lease)" % head_ref)
    git(["push", "--force-with-lease", "origin", "HEAD:refs/heads/" + head_ref])
    log("étiquettes : -%s +%s" % (LABEL_CONFLIT, LABEL_OK))
    set_label(repo, number, LABEL_CONFLIT, False)
    set_label(repo, number, LABEL_OK, True)
    comment(repo, number,
            "### ✅ Conflit résolu — voie %s\n\n%s\nLa branche a été rebasée et "
            "poussée en `--force-with-lease` (l'historique est réécrit, pas le "
            "contenu validé). Aucune fusion n'est faite ici : **Mergify reste le seul "
            "pilote de merge**.\n\nRésolution automatique par "
            "`.github/hermes/resolve_conflict.py`." % (mode, detail))
    log("PR #%d annotée — terminé" % number)


def resolve(repo, pr):
    """Deterministic path first, InferHub path second. Raises ResolverError."""
    head = pr.get("head") or {}
    number, head_ref = pr["number"], head.get("ref")
    head_repo = (head.get("repo") or {}).get("full_name")
    base = (pr.get("base") or {}).get("ref") or "main"
    if not head_ref:
        raise ResolverError("PR #%s : branche source introuvable" % number)
    if head_repo and head_repo != repo:
        raise ResolverError("la PR #%d vient d'un fork (%s) : ce runner ne peut pas "
                            "pousser dessus" % (number, head_repo))
    log("PR #%d : %s → %s (dépôt %s)" % (number, head_ref, base, repo))
    log("étape 1/4 : fetch + rebase déterministe sur origin/%s" % base)
    git(["fetch", "origin", "--prune"], timeout=180)
    code, out, err = git(["rebase", "origin/" + base], check=False)
    if code == 0:
        log("étape 2/4 : rebase déterministe réussi — aucune IA nécessaire")
        verify(base)
        publish(repo, pr, head_ref, "déterministe (rebase seul)")
        return
    log("étape 2/4 : conflit réel → voie InferHub (%s)"
        % redact((err or out).strip()[:200]))
    git(["rebase", "--abort"], check=False)
    ai_rebase(base, contracts_excerpt())
    log("étape 3/4 : vérifications")
    verify(base)
    log("étape 4/4 : publication")
    publish(repo, pr, head_ref, "InferHub (%s)" % MODEL,
            "Chaque fichier en conflit a été résolu par le modèle, puis vérifié "
            "(balises, syntaxe, gardes du dépôt).\n")


def fail(repo, pr, reason):
    """Clean failure: abort the rebase, keep the label, comment, exit 1."""
    number = pr.get("number")
    git(["rebase", "--abort"], check=False)
    log("ÉCHEC PROPRE (rien n'a été poussé) : %s" % reason)
    try:
        set_label(repo, number, LABEL_CONFLIT, True)
        set_label(repo, number, LABEL_OK, False)
        comment(repo, number,
                "### ❌ Résolution automatique impossible\n\n**Raison** : %s\n\nAucune "
                "poussée n'a été faite : la branche est intacte et le rebase a été "
                "annulé. L'étiquette `%s` reste posée.\n\nRepli humain : `git fetch "
                "origin && git checkout <branche> && git rebase origin/<base>`, "
                "résoudre, `git push --force-with-lease`." % (reason, LABEL_CONFLIT))
    except ResolverError as exc:
        log("impossible d'annoter la PR : %s" % exc)
    return 1


def resolve_pr():
    """Read the environment, fetch the PR, resolve it; return the exit code.

    Not called `run()`: that name belongs to the process runner imported from
    `conflict_lib`, and shadowing it broke `verify()` (caught by the blank test).
    """
    repo, number = env("GITHUB_REPOSITORY"), env("PR_NUMBER")
    if not repo or not number:
        raise ResolverError("PR_NUMBER et GITHUB_REPOSITORY sont obligatoires "
                            "(reçus : PR_NUMBER=%r, GITHUB_REPOSITORY=%r)"
                            % (number, repo))
    log("lecture de la PR %s#%s via `gh api`" % (repo, number))
    pr = gh_api("GET", "/repos/%s/pulls/%s" % (repo, number))
    try:
        resolve(repo, pr)
        return 0
    except ResolverError as exc:
        return fail(repo, pr, str(exc))


def main():
    try:
        return resolve_pr()
    except ResolverError as exc:
        log("ERREUR : %s" % exc)
        return 1
    except Exception as exc:  # noqa: BLE001 — log it, never crash with a traceback
        log("ERREUR inattendue : %s : %s" % (type(exc).__name__, redact(str(exc))))
        traceback.print_exc()
        return 1


if __name__ == "__main__":
    sys.exit(main())
