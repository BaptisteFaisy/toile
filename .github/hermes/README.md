# Hermes — fraîcheur et conflits (dépôt Toile)

Deux scripts, une règle : **Mergify est le seul pilote de merge. Rien ici ne
fusionne.** Le gate pose des étiquettes et des checks ; le resolver rebasé et
pousse la branche. Jamais aucun `PUT /pulls/N/merge`.

## `gate.py` — la gate de fraîcheur

Lancée par `.github/workflows/hermes-lite.yml` sur `pull_request`,
`check_run: completed` et `workflow_dispatch`.

- **Branche en retard, sans conflit** → rebase serveur
  (`PUT /pulls/N/update-branch`). Le push est fait par le bot : aucun workflow ne
  se déclenche, donc la boucle `settle_after_refresh` ré-évalue la PR dans ce run.
- **Conflit réel** → check rouge `hermes-lite/verdict` + étiquette
  `hermes/conflit`. La gate s'arrête là : elle ne résout rien, c'est le resolver
  qui prend le relais.
- **Fraîche + checks requis verts** → étiquette `hermes/approuvee` + check vert.
  Les checks requis sont lus dans `HERMES_REQUIRED_CHECKS` (le workflow pose
  `ci, ci-budget`). `HERMES_LITE_DRY_RUN=1` coupe en plus le rebase serveur ;
  même à `0`, aucune fusion n'est émise.

## `resolve_conflict.py` (+ `conflict_lib.py`) — le resolver de conflits

`resolve_conflict.py` porte la politique, `conflict_lib.py` la plomberie
(processus, API GitHub via `gh`, git, prompt, appel InferHub) ; les deux sont
100 % stdlib. Déclenché par `hermes-conflict-resolver.yml` quand
`hermes/conflit` est posée, ou à la main avec l'entrée `pr_number`. Il lit
`PR_NUMBER` et `GITHUB_REPOSITORY`, puis :

1. **Voie déterministe d'abord.** `git fetch` + `git rebase origin/<base>`. S'il
   passe seul : push `--force-with-lease`, retrait de `hermes/conflit`, pose de
   `hermes/approuvee`, commentaire sur la PR. Aucune IA, aucun coût.
2. **Voie InferHub ensuite**, fichier par fichier. Chaque bloc
   `<<<<<<<` / `=======` / `>>>>>>>` est extrait et envoyé à InferHub
   (`POST https://api.inferhub.dev/v1/chat/completions`, modèle `combo/cheap`,
   surchargeable par `INFERHUB_MODEL`) avec un prompt strict : respecter l'API C
   gelée de `CONTRACT.md` §10, ne jamais renommer un symbole `toile_*`, et
   renvoyer **uniquement** le fichier complet, sans code fence ni commentaire.
3. **Vérifications avant tout push** : aucune balise de conflit résiduelle,
   `python3 -m py_compile` sur chaque `.py` touché, `gcc -fsyntax-only -std=c11
   -Icore/include` sur chaque `.c`/`.h` touché, puis les deux gardes du dépôt
   (`.github/checks/taille.py`, `.github/checks/api-frozen.py`). Si `gcc` ou une
   garde manque, c'est annoncé **bruyamment** et sauté — jamais présenté comme un
   succès ; le workflow `ci` rejoue tout.
4. **Échec propre.** Une vérification en échec → `git rebase --abort`,
   l'étiquette `hermes/conflit` reste, la PR est commentée avec le fichier et la
   raison, exit 1. Aucune poussée cassée n'est tentée. Chaque étape est journalisée
   sur stdout : le log du runner est la seule preuve.

## Secret `INFERHUB_API_KEY`

Le workflow échoue vite avec un message explicite si le secret manque (rien
n'est poussé). À poser une fois :

```bash
gh secret set INFERHUB_API_KEY --repo BaptisteFaisy/toile
# ou : Settings → Secrets and variables → Actions → New repository secret
```

La clé n'est lue **que** dans l'environnement (`INFERHUB_API_KEY`), jamais écrite
dans un fichier, et elle est masquée (`sk-***`) dans chaque ligne de log et
chaque message d'erreur. Le jeton GitHub passe par un `credential.helper` en
argv : jamais dans la ligne de commande git ni dans la config.

## Runner self-hosted r630 — piège de PATH

Le runner `r630-toile-01` n'a **pas** `/home/baptiste/bin` dans son PATH par
défaut, et `gh` (utilisé par `conflict_lib.gh_api`) y vit. Le workflow doit donc
l'ajouter avant tout appel :

```bash
echo "/home/baptiste/bin" >> "$GITHUB_PATH"
command -v gh >/dev/null || { echo "::error::gh introuvable"; exit 1; }
```

Sans cela le resolver meurt sur « `gh` est introuvable sur ce runner ».

## Repli manuel

Rien ici n'est un garde-fou bloquant. Si le resolver échoue, rebaser à la main :
`git fetch origin`, `git rebase origin/main`, résoudre, puis
`git push --force-with-lease`. La gate retirera `hermes/conflit` au prochain
passage et Mergify fusionnera.
