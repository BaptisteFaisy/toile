#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Plumbing for the Hermes conflict resolver of the Toile repository (stdlib
only).

Split out of `resolve_conflict.py` to keep both files short (project rule:
split instead of growing). This module holds everything that is not policy:
process running, the GitHub API, git with an argv-safe credential helper,
conflict-block parsing, the InferHub call and the strict prompt.

Nothing here decides whether to push: that lives in `resolve_conflict.py`.
The API key is read from INFERHUB_API_KEY and never logged (`redact`).
"""

import json
import os
import re
import subprocess
import urllib.error
import urllib.request
from urllib.parse import quote

API_BASE = "https://api.inferhub.dev/v1"
MODEL = "combo/cheap"
LABEL_OK, LABEL_CONFLIT = "hermes/approuvee", "hermes/conflit"
MARKER_RE = re.compile(r"^(?:<{7}|>{7}|\|{7}|={7}$)", re.MULTILINE)
SECRET_RE = re.compile(r"sk-[A-Za-z0-9_\-]{4,}")
CONTRACTS = "CONTRACT.md"
GUARDS = (".github/checks/taille.py", ".github/checks/api-frozen.py")
PY_EXTS = (".py",)
C_EXTS = (".c", ".h")
C_FLAGS = ("-fsyntax-only", "-std=c11", "-Icore/include")
MAX_ROUNDS = 3
PROMPT = (
    "You resolve a Git merge conflict in the Toile repository: a 2D virtio-gpu\n"
    "pilot for QNX 8.0 written in C11, with a byte-identical Python twin.\n\n"
    "ROLE\n  You are a merge-conflict resolver. The public C API of this repository is\n"
    "  FROZEN and documented in CONTRACT.md section 10. Produce the resolved content\n"
    "  of one file without changing that frozen API.\n\n"
    "INSTRUCTIONS — read all of them, then answer\n"
    "  1. Return ONLY the complete content of the resolved file.\n"
    "  2. No code fence, no preamble, no commentary, no trailing note.\n"
    "  3. Never emit the markers <<<<<<<, ======= or >>>>>>>.\n"
    "  4. Never rename, drop or re-signature a `toile_*` symbol: the frozen public C\n"
    "     API below (CONTRACT.md section 10) must stay byte-identical.\n"
    "  5. Keep the intent of both sides when they do not conflict. When they do, keep\n"
    "     the side that preserves the frozen API and the conventions of Toile\n"
    "     (C11 + Python, files < 500 lines, functions < 50 lines).\n"
    "  6. Preserve the existing style, indentation and line endings.\n\n"
    "FROZEN PUBLIC API — excerpt of CONTRACT.md section 10:\n%s\n\n"
    "FILE: %s\n\nFULL CONTENT WITH CONFLICT MARKERS:\n%s\n\n"
    "ISOLATED CONFLICT BLOCKS:\n%s\n\n"
    "FINAL ANSWER = the complete resolved file, and nothing else.")


class ResolverError(Exception):
    """Any failure that must end the run cleanly, with exit code 1."""


def log(message):
    print("[resolver] %s" % message, flush=True)


def env(name, default=None):
    value = os.environ.get(name)
    return default if value is None or value == "" else value


def redact(text):
    """Strip anything that looks like an API key before it reaches the log."""
    return SECRET_RE.sub("sk-***", text or "")


def run(cmd, timeout=120, check=False, stdin=None, extra_env=None):
    """Run a command; return (returncode, stdout, stderr)."""
    environ = dict(os.environ, **(extra_env or {}))
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout,
                              input=stdin, env=environ)
    except FileNotFoundError:
        return 127, "", "%s: introuvable" % cmd[0]
    except subprocess.TimeoutExpired:
        return 124, "", "%s: délai dépassé" % cmd[0]
    if check and proc.returncode != 0:
        raise ResolverError("`%s` a échoué (%d) : %s"
                            % (" ".join(cmd[:3]), proc.returncode,
                               redact((proc.stderr or proc.stdout).strip()[:400])))
    return proc.returncode, proc.stdout, proc.stderr


def git(args, check=True, timeout=120, extra_env=None):
    """git with an argv-safe credential helper (token never in argv or config)."""
    auth = []
    if env("GITHUB_TOKEN"):
        helper = "!f() { echo username=x-access-token; echo password=$GITHUB_TOKEN; }; f"
        auth = ["-c", "credential.helper=", "-c", "credential.helper=" + helper]
    return run(["git"] + auth + args, timeout=timeout, check=check,
               extra_env=extra_env)


def gh_api(method, path, body=None, allowed=()):
    """Call the GitHub REST API through `gh api`; `allowed` tolerates given errors."""
    cmd, stdin = ["gh", "api", "-X", method, path], None
    if body is not None:
        cmd, stdin = cmd + ["--input", "-"], json.dumps(body)
    code, out, err = run(cmd, timeout=60, stdin=stdin)
    if code == 127:
        raise ResolverError("`gh` est introuvable sur ce runner : impossible de lire "
                            "ou d'annoter la PR.")
    if code != 0:
        blob = redact((err or out).strip())
        if any(str(tolerated) in blob for tolerated in allowed):
            log("gh api %s %s → toléré" % (method, path))
            return {}
        raise ResolverError("gh api %s %s a échoué (%d) : %s"
                            % (method, path, code, blob[:300]))
    return json.loads(out) if out.strip() else {}


def set_label(repo, number, label, add):
    """Add or remove a label; removing an absent one (404) is tolerated."""
    if add:
        gh_api("POST", "/repos/%s/issues/%d/labels" % (repo, number),
               {"labels": [label]})
    else:
        gh_api("DELETE", "/repos/%s/issues/%d/labels/%s"
               % (repo, number, quote(label, safe="")), allowed=(404,))


def comment(repo, number, body):
    gh_api("POST", "/repos/%s/issues/%d/comments" % (repo, number), {"body": body})


def read_text(path):
    with open(path, "r", encoding="utf-8") as handle:
        return handle.read()


def write_text(path, content):
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(content)


def has_markers(text):
    """True when a conflict marker survives (MULTILINE is baked into MARKER_RE)."""
    return bool(MARKER_RE.search(text or ""))


def git_names(args):
    return [line.strip() for line in git(args, check=False)[1].splitlines()
            if line.strip()]


def extract_conflict_blocks(text):
    """Return [(ours, theirs)] for every conflict block found in `text`."""
    blocks, ours, theirs, state = [], [], [], None
    for line in text.splitlines():
        if line.startswith("<<<<<<<"):
            state, ours, theirs = "ours", [], []
        elif state and line.rstrip() == "=======":
            state = "theirs"
        elif state and line.startswith(">>>>>>>"):
            blocks.append(("\n".join(ours), "\n".join(theirs)))
            state = None
        elif state:
            (ours if state == "ours" else theirs).append(line)
    return blocks


def contracts_excerpt(limit=6000):
    """The frozen public C API: CONTRACT.md section 10 plus the section titles."""
    if not os.path.isfile(CONTRACTS):
        log("%s introuvable : contexte d'API gelée réduit au minimum" % CONTRACTS)
        return "(CONTRACT.md unavailable — do not change any toile_* symbol)"
    lines = read_text(CONTRACTS).splitlines()
    kept, inside = [], False
    for line in lines:
        if line.startswith("## 10"):
            inside = True
        elif inside and line.startswith("## "):
            inside = False
        if inside or line.startswith(("## ", "### ")):
            kept.append(line)
    text = "\n".join(kept) or "\n".join(lines[:40])
    return text if len(text) <= limit else text[:limit].rsplit("\n", 1)[0] + "\n[…]"



def build_prompt(path, content, contracts):
    """Strict prompt: conflict blocks, frozen public API, file content only."""
    blocks = "\n\n".join(
        "### Conflict %d\n--- side A (`<<<<<<< HEAD`, base branch after rebase) ---\n"
        "%s\n--- side B (`>>>>>>>`, pull request branch) ---\n%s"
        % (index, ours.strip("\n"), theirs.strip("\n"))
        for index, (ours, theirs) in enumerate(extract_conflict_blocks(content), 1))
    return PROMPT % (contracts, path, content, blocks)


def inferhub_resolve(prompt, timeout=150):
    """POST the prompt to InferHub and return the answer text."""
    key = env("INFERHUB_API_KEY")
    if not key:
        raise ResolverError(
            "INFERHUB_API_KEY est absent de l'environnement : la voie InferHub est "
            "impossible. Poser le secret GitHub `INFERHUB_API_KEY` (Settings → "
            "Secrets and variables → Actions), puis relancer.")
    body = {"model": env("INFERHUB_MODEL", MODEL), "temperature": 0.2,
            "stream": False, "messages": [{"role": "user", "content": prompt}]}
    request = urllib.request.Request(
        API_BASE + "/chat/completions", data=json.dumps(body).encode("utf-8"),
        headers={"Authorization": "Bearer " + key, "Content-Type": "application/json"},
        method="POST")
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            raw = response.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", errors="replace")[:300]
        raise ResolverError("InferHub a renvoyé HTTP %s : %s" % (exc.code, redact(detail)))
    except (urllib.error.URLError, OSError) as exc:
        raise ResolverError("InferHub est injoignable : %s" % redact(str(exc)))
    try:
        answer = json.loads(raw)["choices"][0]["message"]["content"]
    except (ValueError, KeyError, IndexError, TypeError):
        raise ResolverError("réponse InferHub illisible : %s" % redact(raw[:300]))
    if not answer or not answer.strip():
        raise ResolverError("réponse InferHub vide")
    return answer


def clean_answer(raw, original):
    """Drop a surrounding code fence and restore the original trailing newline."""
    lines = raw.split("\n")
    while lines and not lines[0].strip():
        lines.pop(0)
    if lines and lines[0].strip().startswith("```"):
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    if lines and lines[-1].strip().startswith("```"):
        lines.pop()
    text = "\n".join(lines)
    if original.endswith("\n"):
        return text if text.endswith("\n") else text + "\n"
    return text.rstrip("\n")
