#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Gate Hermes Lite du dépôt Toile — fraîcheur exigée, sans jamais fusionner.

Règles :
- retard sans conflit  → rebase serveur (PUT /pulls/N/update-branch) ;
  le push du bot ne déclenche aucun workflow → boucle `settle` dans ce run.
- vrai conflit         → check rouge + label `hermes/conflit` : le resolver
  `.github/hermes/resolve_conflict.py` prend le relais (déterministe, puis InferHub).
- frais + checks requis verts (HERMES_REQUIRED_CHECKS, posé par le workflow :
  `ci, ci-budget`) → label `hermes/approuvee` + check `hermes-lite/verdict` vert.

MERGIFY EST LE SEUL PILOTE DE MERGE : ce gate ne fusionne JAMAIS, même quand
HERMES_LITE_DRY_RUN vaut 0 (le dry-run ne coupe que le rebase serveur).
"""

API = 'https://api.github.com'
CHECK_NAME = 'hermes-lite/verdict'
LABEL_OK = 'hermes/approuvee'
# Poser `hermes/conflit` déclenche le resolver (`.github/hermes/resolve_conflict.py`
# via `.github/workflows/hermes-conflict-resolver.yml`) : voie déterministe d'abord
# (rebase seul), voie InferHub ensuite, échec propre sinon. Le gate, lui, ne résout
# rien et ne merge jamais — Mergify reste le seul pilote de merge.
LABEL_CONFLIT = 'hermes/conflit'

import json
import time


def api(method, url, body=None, token=None):
    import json as _json
    import urllib.request as _ur
    import urllib.error as _ue
    data = _json.dumps(body).encode() if body is not None else None
    req = _ur.Request(API + url, data=data, method=method)
    req.add_header('Authorization', 'Bearer ' + token)
    req.add_header('Accept', 'application/vnd.github+json')
    try:
        with _ur.urlopen(req, timeout=30) as r:
            raw = r.read()
            return r.status, (_json.loads(raw) if raw else None)
    except _ue.HTTPError as e:
        payload = e.read()
        try:
            return e.code, _json.loads(payload)
        except Exception:
            return e.code, payload.decode('utf-8', errors='replace')


def ensure_label(repo, token, name, color, desc):
    api('POST', '/repos/%s/labels' % repo, token=token,
        body={'name': name, 'color': color, 'description': desc})


def set_label(pr, repo, token, name, add):
    num = pr['number']
    has = name in [l['name'] for l in pr.get('labels') or []]
    if add and not has:
        api('POST', '/repos/%s/issues/%d/labels' % (repo, num),
            token=token, body={'labels': [name]})
    elif not add and has:
        api('DELETE', '/repos/%s/issues/%d/labels/%s' % (repo, num, name),
            token=token)


def refresh_branch(repo, token, pr):
    """Rebase serveur d'une PR en retard. Renvoie 'refreshed'/'conflict'/'error'."""
    num = pr['number']
    expected = pr['head']['sha']
    sc, resp = api('PUT', '/repos/%s/pulls/%d/update-branch' % (repo, num),
                   token=token,
                   body={'update_method': 'rebase', 'expected_head_sha': expected})
    if sc == 202:
        return 'refreshed'
    if sc in (409, 422):
        return 'conflict'
    print('[gate] #%d update-branch %s : %s' % (num, sc, resp), flush=True)
    return 'error'


def create_check(repo, token, sha, ok, title, summary):
    api('POST', '/repos/%s/check-runs' % repo, token=token, body={
        'name': CHECK_NAME,
        'head_sha': sha,
        'status': 'completed',
        'conclusion': 'success' if ok else 'failure',
        'output': {'title': title, 'summary': summary},
    })


def required_checks_green(repo, token, pr, required):
    """Statuts de commit requis verts. Liste vide => considéré vert."""
    if not required:
        return True
    sha = pr['head']['sha']
    sc, data = api('GET', '/repos/%s/commits/%s/status' % (repo, sha), token=token)
    if sc != 200 or not data:
        return False
    states = {s['context']: s['state'] for s in data.get('statuses') or []}
    for ctx in required:
        if states.get(ctx) != 'success':
            return False
    return True


def settle_after_refresh(repo, token, num, required, attempts=10, pause=6, dry=False):
    """[HERMES-REFRESH] Ré-évalue la PR après le rebase serveur, DANS CE RUN.

    Le push effectué par `update-branch` est un push de bot : il ne déclenche
    AUCUN workflow (anti-récursion GitHub), et hermes-lite n'a ni cron ni
    dispatch possible avec GITHUB_TOKEN. Sans cette boucle, une PR rebasée
    resterait ouverte pour toujours : personne ne la re-évaluerait.
    Pollue borné (~60 s), puis rend la main (la veille événementielle
    rattrapera au prochain check_run de ce head).
    """
    for i in range(attempts):
        time.sleep(pause if i else 2)
        sc, pr = api('GET', '/repos/%s/pulls/%d' % (repo, num), token=token)
        if sc != 200 or not isinstance(pr, dict):
            continue
        if pr.get('merged'):
            return 'merged'
        sha = pr['head']['sha']
        base_ref = pr['base']['ref']
        sc, cmp_ = api('GET', '/repos/%s/compare/%s...%s' % (repo, base_ref, sha),
                       token=token)
        behind = cmp_.get('behind_by') if sc == 200 else -1
        behind = behind if isinstance(behind, int) else -1
        if behind > 0:
            continue  # rebase pas encore appliqué (ou main a bougé entre-temps)
        conflicts = (pr.get('mergeable') is False
                     and (pr.get('mergeable_state') or '') == 'dirty')
        if conflicts:
            create_check(repo, token, sha, False,
                         'Conflits avec %s — intervention humaine' % base_ref,
                         'Le rebase automatique a révélé un conflit. Le label '
                         '`hermes/conflit` déclenche '
                         '`.github/hermes/resolve_conflict.py` (voie déterministe, '
                         'puis InferHub) ; repli manuel sinon.')
            set_label(pr, repo, token, LABEL_CONFLIT, True)
            set_label(pr, repo, token, LABEL_OK, False)
            return 'conflict'
        set_label(pr, repo, token, LABEL_OK, True)
        create_check(repo, token, sha, True, 'PR à jour — merge délégué à Mergify',
                     'Fraîche au moment du verdict (settle après rebase '
                     'serveur) ; Mergify est le seul pilote de merge.')
        if dry:
            return 'dry'
        m = try_merge(repo, token, pr, required)
        print('[gate] #%d : settle verdict → %s' % (num, m), flush=True)
        if m != 'waiting':
            return m
    return 'timeout'


def try_merge(repo, token, pr, required):
    """Verdict de merge. NE FUSIONNE JAMAIS : Mergify est le seul pilote de merge.

    Le nom est conservé (appelé par `handle_pr` et `settle_after_refresh`), mais
    aucun `PUT /pulls/N/merge` n'est émis : le gate se borne à dire si la PR est
    fraîche et si les checks requis sont verts, puis rend la main à Mergify.
    """
    num = pr['number']
    sha = pr['head']['sha']
    # Re-vérification de fraîcheur au dernier moment (défense anti-course)
    sc, cmp_ = api('GET', '/repos/%s/compare/%s...%s'
                   % (repo, pr['base']['ref'], sha), token=token)
    if sc == 200 and cmp_.get('behind_by') not in (0, None):
        return 'behind'
    if not required_checks_green(repo, token, pr, required):
        return 'waiting'
    print('[gate] #%d : fraîche + checks requis verts (%s) — merge délégué à '
          'Mergify, aucune fusion ici' % (num, ', '.join(required) or 'aucun'),
          flush=True)
    return 'delegated'




def behind_count(repo, token, base_ref, sha):
    """Nombre de commits de retard (API compare) ; -1 si inconnu."""
    sc, cmp_ = api('GET', '/repos/%s/compare/%s...%s' % (repo, base_ref, sha),
                   token=token)
    behind = cmp_.get('behind_by') if sc == 200 else -1
    return behind if isinstance(behind, int) else -1


def mark_conflict(repo, token, pr, sha, base_ref, reason):
    """Check rouge + étiquette `hermes/conflit` : le resolver prend le relais."""
    create_check(repo, token, sha, False,
                 'Conflits avec %s — intervention humaine' % base_ref, reason)
    set_label(pr, repo, token, LABEL_CONFLIT, True)
    set_label(pr, repo, token, LABEL_OK, False)


def mark_fresh(repo, token, pr, sha, required):
    """Check vert + étiquette `hermes/approuvee` : Mergify fusionnera."""
    set_label(pr, repo, token, LABEL_OK, True)
    create_check(repo, token, sha, True,
                 'PR à jour — merge délégué à Mergify',
                 'Fraîche au moment du verdict ; %s. Aucune fusion ici : '
                 'Mergify est le seul pilote de merge.' %
                 ('checks requis verts : ' + ', '.join(required) if required
                  else 'aucun check requis configuré'))


def handle_behind(pr, repo, token, required, dry, sha, base_ref, behind):
    """PR en retard : rebase serveur (dry-run : rien), puis settle."""
    num = pr['number']
    if dry:
        print('[gate] #%d : en retard de %s commit(s) — rebase serveur (dry-run)'
              % (num, behind))
        return 'dry'
    result = refresh_branch(repo, token, pr)
    print('[gate] #%d : en retard de %s → refresh %s' % (num, behind, result),
          flush=True)
    if result == 'refreshed':
        # Le push du rebase est un push de bot : aucun workflow ne se déclenche.
        # Ré-évaluation DANS CE RUN (toujours sans fusion).
        return settle_after_refresh(repo, token, num, required, dry=dry)
    if result == 'conflict':
        mark_conflict(repo, token, pr, sha, base_ref,
                      'Le rebase automatique a été refusé : conflit réel. '
                      'Le label `hermes/conflit` déclenche le resolver ; '
                      'repli manuel sinon.')
    return result


def handle_pr(pr, repo, token, required, dry=False):
    """Verdict de fraîcheur d'une PR. Ne fusionne jamais."""
    num = pr['number']
    sha = pr['head']['sha']
    base_ref = pr['base']['ref']
    conflicts = (pr.get('mergeable') is False
                 and (pr.get('mergeable_state') or '') == 'dirty')
    if conflicts:
        mark_conflict(repo, token, pr, sha, base_ref,
                      'Le gate ne résout pas les conflits (pas d IA ici). Le label '
                      '`hermes/conflit` déclenche '
                      '`.github/hermes/resolve_conflict.py` ; repli manuel sinon.')
        return 'conflict'
    set_label(pr, repo, token, LABEL_CONFLIT, False)
    behind = behind_count(repo, token, base_ref, sha)
    if behind != 0:
        return handle_behind(pr, repo, token, required, dry, sha, base_ref, behind)
    mark_fresh(repo, token, pr, sha, required)
    if dry:
        print('[gate] #%d : fraîche — prête pour Mergify (dry-run, '
              'aucune fusion ici)' % num)
        return 'dry'
    verdict = try_merge(repo, token, pr, required)
    print('[gate] #%d : verdict → %s (aucune fusion ici)' % (num, verdict),
          flush=True)
    return verdict




def list_open_prs(repo, token):
    prs, page = [], 1
    while True:
        sc, plist = api('GET', '/repos/%s/pulls?state=open&per_page=100&page=%d'
                        % (repo, page), token=token)
        if sc != 200 or not isinstance(plist, list):
            break
        prs += plist
        if len(plist) < 100:
            break
        page += 1
    return prs


def collect_prs(event_name, event, repo, token):
    """PRs à évaluer selon l'événement déclenchant le workflow."""
    if event_name == 'workflow_dispatch':
        only = (event.get('inputs') or {}).get('pr_number') or None
        prs = list_open_prs(repo, token)
        if only:
            prs = [p for p in prs if p['number'] == int(only)]
        return prs
    if event_name == 'pull_request':
        return [event['pull_request']]
    if event_name == 'check_run':
        # Un check vient de finir : ré-évalue les PR dont le head porte ce check.
        sha = event['check_run']['head_sha']
        return [p for p in list_open_prs(repo, token)
                if p['head']['sha'] == sha]
    return []


def main():
    import os
    token = os.environ['GH_TOKEN']
    repo = os.environ['GITHUB_REPOSITORY']
    event_name = os.environ['GITHUB_EVENT_NAME']
    required = [s.strip() for s in
                os.environ.get('HERMES_REQUIRED_CHECKS', '').split(',') if s.strip()]
    dry = os.environ.get('HERMES_LITE_DRY_RUN') == '1'

    for lbl in (LABEL_OK, LABEL_CONFLIT):
        ensure_label(repo, token, lbl, '0e8a16' if lbl == LABEL_OK else 'd73a4a',
                     'Posé par hermes-lite')

    with open(os.environ['GITHUB_EVENT_PATH']) as fh:
        event = json.load(fh)

    prs = collect_prs(event_name, event, repo, token)

    ready = 0
    for pr in prs:
        if pr.get('draft') or pr.get('state') != 'open':
            continue
        head_ref = ((pr.get('head') or {}).get('ref') or '')
        if head_ref.startswith('mergify/'):
            # PR-brouillon d'un batch Mergify : le corps est écrit par Mergify,
            # il n'y a donc pas de « ## Scope » à contrôler, et un contrôle
            # complet y serait toujours rouge. On sort en SUCCÈS sans
            # contrôler (règle 27 du maître) ; le contrôle complet reste sur
            # la vraie PR.
            print('[gate] #%s branche Mergify (%s) : contrôle ignoré'
                  % (pr.get('number'), head_ref), flush=True)
            continue
        try:
            r = handle_pr(pr, repo, token, required, dry)
            if r == 'delegated':
                ready += 1
        except Exception as e:
            print('[gate] #%s exception : %r' % (pr.get('number'), e), flush=True)
    print('[gate] terminé : %d PR traitée(s), %d prête(s) pour Mergify, '
          '0 merge (ce gate ne fusionne jamais)' % (len(prs), ready), flush=True)


if __name__ == '__main__':
    main()
