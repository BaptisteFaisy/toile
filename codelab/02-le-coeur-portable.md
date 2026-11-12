# Chapitre 2 — Le cœur portable : C11 + jumeau Python, sans QNX ni QEMU ni GPU

**Durée estimée : ~1 h**

## Objectif

Faire tourner **le cœur de Toile** — la machine à commandes virtio-gpu — sur
un poste ordinaire : construire le binaire, écrire sa première fixture DSL,
lire les verdicts, comparer les trois formats de sortie, vérifier le
déterminisme par le hash d'état, faire tourner le différentiel C↔Python et les
gardes CI. Ce chapitre est le plus important pédagogiquement : il prouve que
la logique du pilote est juste **avant** qu'aucun pilote n'existe sur cible.

## Prérequis

- Linux (ou macOS) avec `gcc` (C11), `python3` (≥ 3.8, stdlib seule), `make`, `git`.
- **Rien d'autre.** Pas de QNX, pas de QEMU, pas de GPU, pas de réseau après le clone.
- Avoir lu le chapitre 1 (le cycle de vie create → attach backing → set
  scanout → transfer → flush).

> **État à la date d'écriture.** Le contrat est **gelé** (`CONTRACT.md` v1) :
> commandes (§3), grammaire des fixtures (§4), verdicts (§6), formats de
> sortie octet pour octet (§7), hash d'état (§8), CLI (§11), cibles Makefile
> (§12). Les en-têtes publics `core/include/toile/` sont en place (6 fichiers).
> Les **implémentations** (`core/src/`, `core/cli/`, `host/toile/`, `Makefile`,
> `tests/`, `.github/checks/`) sont en cours de livraison en semaines S1–S2 du
> planning (cahier des charges §11). Chaque étape qui dépend d'un binaire pas
> encore livré porte la mention **⚠ en cours de livraison** ; la commande,
> elle, est exacte et gelée par le contrat.

---

## Étape 1 — Cloner et regarder l'arbre

```bash
$ git clone <url-du-dépôt-toile> toile && cd toile
$ ls
```

Les deux répertoires qui comptent pour ce chapitre :

```
core/            C11 PUR : ni horloge, ni fichier, ni socket, ni QNX
  include/toile/   vgproto.h resource.h scanout.h cmdproc.h verdict.h report.h
  src/             vgproto.c resource.c scanout.c cmdproc.c verdict.c report.c
  cli/             toile-run (le binaire C)
  tests/           tests unitaires C
host/            Python stdlib : jumeau indépendant + CLI
  toile/           vgproto.py resource.py cmdproc.py replay.py cli.py report.py
  tests/           tests unitaires Python
fixtures/        séquences de commandes de référence (+ golden/, wire/)
tests/           différentiel C↔Python
```

La **règle de portabilité** (cahier des charges §4) : `core/` ne lit aucun
fichier, ne consulte aucune horloge, n'appelle aucune API QNX. L'appelant
fournit les séquences de commandes ; le cœur rend des verdicts et un état.
C'est ce qui rend tout ce chapitre exécutable sur un poste ordinaire — et plus
tard embarquable tel quel dans le pilote invité (chapitre 3).

## Étape 2 — Construire ⚠ en cours de livraison

```bash
$ make build
```

Cible gelée (`CONTRACT.md` §12) : construit `build/toile-run` et les binaires
de tests, avec `gcc -std=c11 -O2 -Werror -Wall -Wextra`. Zéro dépendance
externe : si votre gcc est raisonnablement récent, ça compile.

```bash
$ ./build/toile-run selftest
```

`selftest` exécute les auto-tests internes du binaire (CLI gelée, §11). Code
de sortie attendu : `0`.

## Étape 3 — Écrire sa première fixture DSL

Les fixtures sont des fichiers texte `.tcl` — **Toile Command Language**, rien
à voir avec le langage Tcl homonyme. Grammaire complète : `CONTRACT.md` §4.
Une commande par ligne, `nom clé=valeur …` ; `#` introduit un commentaire.

Créez `ma-fixture.tcl` :

```tcl
# ma-fixture.tcl — premier cycle de vie complet (cf. chapitre 1, étape 2)

# 1. create : l'invité déclare une surface 1280×800, format 4 octets/pixel
resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800

# 2. attach backing : il donne ses pages au périphérique (addr:len, ≤ 32 entrées)
resource_attach_backing id=1 pages=0x1000:4096,0x2000:4096

# 3. set scanout : la ressource 1 est affectée à la tête 0, plein cadre
set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800

# 4. flush : « affiche » — effet hôte, sans rectangle = toute la ressource
resource_flush id=1
```

Points de grammaire à connaître (tous gelés, §4) :

- entiers décimaux ou `0x…` ; séparateurs espaces/tabulations ;
- alias de clés : `id`≡`resource`, `w`≡`width`, `h`≡`height` ;
- un rectangle **omis** (ou `width=0 height=0`) sur `resource_flush`,
  `transfer_to_host_2d`, `transfer_from_host` est développé en
  `x=0 y=0 width=<largeur ressource> height=<hauteur ressource>` —
  identiquement en C et en Python ;
- `pages` = entrées `addr:len` séparées par des virgules, dans l'ordre,
  **au plus 32** ;
- clé inconnue, clé manquante, valeur non numérique, nom de commande inconnu →
  **erreur de parse** : code de sortie `2`, message
  `ma-fixture.tcl:<ligne>: <raison>`. Ce n'est pas un verdict du modèle :
  la fixture n'entre même pas dans la machine.

Exécutez ⚠ en cours de livraison :

```bash
$ ./build/toile-run run ma-fixture.tcl
```

## Étape 4 — Lire les verdicts

Sortie attendue au format `text` (défaut), conforme à §7.1 — à l'exception du
hash, noté `<hash>` ici, dont la valeur exacte en 16 hexadécimaux est figée
par le golden (étape 5) :

```
toile v1 text
0000 TOILE_OK                 info    cmd=0x0101
0001 TOILE_OK                 info    cmd=0x0106
0002 TOILE_OK                 info    cmd=0x0103
0003 TOILE_OK                 info    cmd=0x0104
0004 TOILE_MODEL_CONSISTENT   info
# state resources=1 scanouts=1 hash=<hash> applied=4 rejected=0 flushes=1 bytes=0 cursor=0,0,0,0,0,0
```

Anatomie : une ligne par verdict — `%04u %-26s %-7s <payload>` — puis la ligne
d'état final précédée de `#`. Les dix codes de verdict (`CONTRACT.md` §6) :

| Code | Sévérité | Quand |
|---|---|---|
| `TOILE_OK` | info | commande appliquée |
| `TOILE_CMD_INVALID` | error | type inconnu, mauvaise cause (payload `cause=…`) |
| `TOILE_RESOURCE_EXISTS` | warning | création d'un `id` déjà actif |
| `TOILE_RESOURCE_MISSING` | error | commande sur un `id` inexistant |
| `TOILE_BACKING_MISSING` | error | transfert sans backing attaché |
| `TOILE_SCANOUT_INVALID` | error | scanout ≥ 16 ou ressource absente |
| `TOILE_FLUSH_NO_SCANOUT` | warning | flush d'une ressource non scannée |
| `TOILE_TRANSFER_OK` | info | transfert réussi (payload `rect=…`) |
| `TOILE_MODEL_CONSISTENT` | info | fin de séquence propre, invariants I1–I6 tenus |
| `TOILE_MODEL_COUNTEREXAMPLE` | error | invariant violé : cause + état, **arrêt immédiat** (fail fast) |

Codes de sortie (CLI §11) : `0` aucune erreur, `1` au moins un verdict
`error`, `2` usage/fixture invalide, `3` divergence avec `--expect`.

Ici : 4 commandes appliquées, 0 rejetée, 1 flush, 0 octet transféré (pas de
`transfer_to_host_2d`), curseur invisible → **exit 0**.

## Étape 5 — Les trois formats de sortie, et ce que chacun prouve

Même exécution, trois rendus (`CONTRACT.md` §7 — formats **octet pour octet**,
produits identiquement par `core/src/report.c` et `host/toile/report.py`) :

```bash
$ ./build/toile-run run ma-fixture.tcl --format text   # défaut, pour l'humain
$ ./build/toile-run run ma-fixture.tcl --format json   # pour la machine / CI
$ ./build/toile-run run ma-fixture.tcl --format md     # rapport d'audit
```

**`json`** — une seule ligne + `\n`, aucune espace hors des chaînes, ordre des
clés figé (hash remplacé par `<hash>` ici) :

```json
{"toile":"1","state":{"resources":1,"scanouts":1,"hash":"<hash>","applied":4,"rejected":0,"flushes":1,"bytes":0,"cursor":{"resource":0,"scanout":0,"x":0,"y":0,"hot_x":0,"hot_y":0,"visible":0}},"verdicts":[{"i":0,"code":"TOILE_OK","sev":"info","cmd":"0x0101"},{"i":1,"code":"TOILE_OK","sev":"info","cmd":"0x0106"},{"i":2,"code":"TOILE_OK","sev":"info","cmd":"0x0103"},{"i":3,"code":"TOILE_OK","sev":"info","cmd":"0x0104"},{"i":4,"code":"TOILE_MODEL_CONSISTENT","sev":"info"}]}
```

Clés d'un verdict, dans l'ordre : `i, code, sev, cmd, resource, scanout, rect,
cause` ; toute clé non applicable est **absente** (jamais `null`). Ce format
prouve que la sortie est **comparable par octets** entre implémentations et
dans le temps — la base du différentiel et des goldens.

**`md`** — l'audit lisible, collable dans une revue ou un ticket :

```markdown
# Rapport d'audit Toile

- fixture : `ma-fixture.tcl`
- verdicts : 5 (erreurs 0, avertissements 0, info 5)
- état : 1 ressources, 1 scanouts, hash `<hash>`

| # | code | sévérité | cmd | ressource | scanout | rect | cause |
|---|---|---|---|---|---|---|---|
| 0000 | TOILE_OK | info | 0x0101 | – | – | – | – |
| 0001 | TOILE_OK | info | 0x0106 | – | – | – | – |
| 0002 | TOILE_OK | info | 0x0103 | – | – | – | – |
| 0003 | TOILE_OK | info | 0x0104 | – | – | – | – |
| 0004 | TOILE_MODEL_CONSISTENT | info | – | – | – | – | – |
```

Cellule vide = `–` (tiret cadratin U+2013) ; aucune ligne vide après le
tableau ; fichier terminé par `\n`. Même un rapport « joli » est ici une
chaîne d'octets contractuelle.

**Goldens et `--expect`.** La référence octet pour octet s'engendre puis se
vérifie ⚠ en cours de livraison :

```bash
$ ./build/toile-run golden ma-fixture.tcl --format text
# écrit fixtures/golden/ma-fixture.text
$ ./build/toile-run run ma-fixture.tcl --expect fixtures/golden/ma-fixture.text
# exit 0 si identique, exit 3 à la première divergence
$ make golden        # régénère TOUT fixtures/golden/ avec le binaire C (référence)
```

## Étape 6 — Le hash d'état et le déterminisme (P3)

La ligne `# state …` et le champ `"hash"` du json portent une empreinte de
**tout** l'état du modèle : FNV-1a 64 bits (base `0xcbf29ce484222325`, premier
`0x100000001b3`) calculé sur une sérialisation canonique ASCII (`CONTRACT.md` §8) :

```
toile-state-v1
R <id> <format> <width> <height> <n_pages>     ← une ligne par ressource active, ids croissants
P <%016llx addr> <%len>                        ← pages, ordre d'attachement
S <scanout> <resource> <x> <y> <w> <h> <flush_count>  ← scanouts actifs, ids croissants
C <resource> <scanout> <x> <y> <hot_x> <hot_y> <visible>   ← curseur
K <applied> <rejected> <flushes> <bytes>       ← compteurs
```

Ce que le hash prouve : **même séquence ⇒ exactement même état**, quel que
soit l'ordre d'exécution, la machine, l'implémentation (C ou Python). Pas
d'horloge, pas d'aléa, pas de pointeur dans l'état — c'est la propriété P3 du
cahier des charges (§7.1). Elle a un usage concret au chapitre 3–4 : les
commandes émises par le pilote réel dans QEMU seront capturées puis rejouées
dans le modèle (exigence C8) — si les hash concordent, le pilote et le modèle
racontent la même histoire.

Vérifiez le déterminisme ⚠ en cours de livraison :

```bash
$ ./build/toile-run run ma-fixture.tcl --format json > /tmp/a.json
$ ./build/toile-run run ma-fixture.tcl --format json > /tmp/b.json
$ cmp /tmp/a.json /tmp/b.json && echo "DETERMINISTE"
DETERMINISTE
```

Deux exécutions, deux processus, mêmes octets. Changez une seule commande de
la fixture (par exemple `height=801`) : le hash change — c'est lui qui rend la
moindre mutation visible.

## Étape 7 — Le différentiel C↔Python (`make diff`)

Le jumeau Python (`host/toile/`) est une **implémentation indépendante** du
même contrat, avec la même CLI (gelée, §11) :

```bash
$ python3 -m toile.cli run ma-fixture.tcl --format json
```

*(Si le module n'est pas trouvé depuis la racine du dépôt : il vit sous
`host/` — `PYTHONPATH=host python3 -m toile.cli …` ; le mécanisme exact est
fourni par le Makefile livré.)*

Le différentiel ⚠ en cours de livraison (`tests/differential.py`, appelé par
la cible gelée) rejoue **toutes** les fixtures sur les deux implémentations,
dans les **trois** formats, et compare **octet pour octet** :

```bash
$ make diff
```

Ce que ça prouve : deux équipes de code — pardon, deux implémentations — qui
ne partagent que le texte du contrat ne peuvent converger octet pour octet que
si le contrat est **exécuté de façon identique et non ambiguë**. Une divergence
est soit un bug du C, soit un bug du Python, soit une ambiguïté de contrat :
dans les trois cas, on veut la voir ici, sur PC, en secondes — pas sur cible,
dans trois semaines. C'est la méthode « cœur portable + jumeau » déjà éprouvée
sur les projets frères du portefeuille.

## Étape 8 — Les gardes (`make guards`) et le vert complet (`make test`)

```bash
$ make guards
```

Cible gelée (§12) : enchaîne `.github/checks/taille.py` et
`.github/checks/api-frozen.py` ⚠ en cours de livraison. Les deux règles
(cahier des charges §4, `CONTRACT.md` §13) :

- **taille** : tout fichier versionné < 500 lignes ; toute fonction C/Python < 50 lignes ;
- **API gelée** : chaque signature publique de `CONTRACT.md` §10 est présente
  dans les en-têtes `core/include/toile/` — l'API ne bouge plus sans décision
  explicite.

Le tout-en-un :

```bash
$ make test    # = build + test-c + test-py + diff + guards
```

Propriété **S9** du cahier des charges : `make test` est **vert sans QNX, sans
QEMU et sans GPU**. C'est la garde-fou de la CI (budget 5 min, `CONTRACT.md`
§13) : la logique du projet est testée à chaque commit sur un runner
ordinaire ; la cible réelle (chapitres 3–4) ne bloque jamais la CI des PR.

## Exercice — la fixture fautive et le verdict exact

**Consigne : prédire avant d'exécuter.** Écrivez `casse.tcl` :

```tcl
# casse.tcl — six commandes, cinq fautes à prédire
resource_create_2d id=7 format=B8G8R8A8_UNORM width=64 height=64
resource_flush id=7
transfer_to_host_2d id=7
set_scanout scanout=16 id=7 x=0 y=0 width=64 height=64
move_cursor scanout=0 x=10 y=10
resource_unref id=99
```

Avant de lancer `./build/toile-run run casse.tcl`, complétez ce tableau
(aides : verdicts §6, causes figées §6, règles §3 du contrat) :

| # | Commande | Sévérité attendue | Code / cause (d'après le contrat) |
|---|---|---|---|
| 0 | `resource_create_2d id=7 …` | info | `TOILE_OK` |
| 1 | `resource_flush id=7` (aucun scanout) | **warning** | `TOILE_FLUSH_NO_SCANOUT` — la séquence continue |
| 2 | `transfer_to_host_2d id=7` (aucun backing) | **error** | `TOILE_BACKING_MISSING` |
| 3 | `set_scanout scanout=16 …` (≥ 16) | **error** | cause `scanout_id_out_of_range` — jamais un débordement |
| 4 | `move_cursor …` (curseur invisible) | **error** | cause `cursor_not_visible` |
| 5 | `resource_unref id=99` (inexistant) | **error** | `TOILE_RESOURCE_MISSING` |

Puis vérifiez : code de sortie attendu **1** (au moins un verdict `error`),
compteurs `applied=1 rejected=5`, et aucun état corrompu (les invariants I1–I6
tiennent après chaque commande — le processeur rejette, il ne casse pas).
L'appairage exact verdict↔cause et la ligne d'octets sont, comme toujours,
figés par le golden :

```bash
$ ./build/toile-run golden casse.tcl --format text
$ ./build/toile-run run casse.tcl --expect fixtures/golden/casse.text && echo CONFORME
```

**Variante — sabotage de golden (exit 3)** ⚠ en cours de livraison :

```bash
$ ./build/toile-run golden ma-fixture.tcl --format text
$ ./build/toile-run run ma-fixture.tcl --expect fixtures/golden/ma-fixture.text && echo OK
OK
$ sed -i 's/applied=4/applied=5/' fixtures/golden/ma-fixture.text
$ ./build/toile-run run ma-fixture.tcl --expect fixtures/golden/ma-fixture.text; echo "exit=$?"
exit=3
$ make golden    # restaure la référence (binaire C = référence)
```

**Bonus — le chemin « fil ».** Les fixtures `fixtures/wire/*.hex` contiennent
les octets réels du protocole (paires hexadécimales, une commande par ligne) :
`./build/toile-run wire mon.hex --format text`. À séquence équivalente, les
verdicts doivent être identiques à la voie DSL `run` — même cœur, deux portes
d'entrée.

**Bonus enseignant — sans machine.** Faites sérialiser « à la main » l'état de
`ma-fixture.tcl` selon §8, puis discutez : permuter deux `resource_create_2d`
d'ids distincts change-t-il le hash ? (Non : R est trié par ids croissants.)
Et permuter `attach_backing` et `create` ? (Erreur : le backing exige la
ressource.) Le hash canonique récompense l'ordre **logique**, pas l'ordre de
rédaction.

## Points de contrôle

- [ ] Distinguer les quatre codes de sortie : 0 succès, 1 erreur modèle, 2 fixture/usage invalide, 3 divergence golden.
- [ ] Expliquer ce que prouve chacun des trois formats (humain, machine/CI, audit) — et pourquoi les trois sont contractuels au **octet** près.
- [ ] Expliquer P3 : même séquence ⇒ même hash, en C comme en Python, sur toute machine.
- [ ] Prédire sévérité et cause des fautes usuelles (flush sans scanout, transfert sans backing, scanout ≥ 16).
- [ ] Citer les cinq cibles enchaînées par `make test` et la propriété S9.

## Erreurs fréquentes

| Symptôme | Cause probable | Remède |
|---|---|---|
| exit 2, message `ma-fixture.tcl:5: …` | Faute DSL ligne 5 (clé inconnue, nom de commande inconnu, valeur non numérique, clé manquante) | Le message donne ligne + raison ; grammaire §4 |
| `TOILE_FLUSH_NO_SCANOUT` inattendu | flush émis avant `set_scanout` | warning assumé : le modèle continue ; corrigez l'ordre si l'intention était d'afficher |
| `TOILE_BACKING_MISSING` | `transfer_to_host_2d` sans `resource_attach_backing` | attacher les pages avant tout transfert (invariant I3) |
| `make diff` rouge | golden modifié à la main, ou divergence C/Python réelle | `make golden` (le C est référence) ; rouge de nouveau → bug réel, ne pas masquer |
| `python3 -m toile.cli` → `No module named toile` | le paquet vit sous `host/` | `PYTHONPATH=host python3 -m toile.cli …` |
| `make build` échoue sur `-Werror` | un avertissement gcc est une erreur, contractuellement | corriger l'avertissement ; les drapeaux sont gelés (§12), jamais les retirer |
| hash différent « alors que rien n'a changé » | l'ordre des pages attachées ou un compteur a bougé | §8 : P suit l'ordre d'attachement, K suit les compteurs — toute mutation est légitimement visible |
