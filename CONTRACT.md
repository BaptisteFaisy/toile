# CONTRACT.md — contrat gelé de Toile (v1)

Statut : **gelé**. Toute modification passe par une PR qui met à jour ce fichier
**et** les deux implémentations (C `core/`, Python `host/`) dans la même PR.
Un symbole public listé ici ne peut être ni renommé, ni supprimé, ni re-signaturé.
C'est ce document que lit l'agent de résolution de conflits `.github/hermes/`
(rubrique « FROZEN PUBLIC API »).

Références : `TOILE-cahier-des-charges.md` §5 (contrat), OASIS virtio 1.x
« GPU Device », `virtio_gpu.h` des en-têtes standard Linux/QEMU (relu le
2026-11-12, voir §9 « corrections »).

---

## 1. Chaîne de traitement (figée)

```
commande virtio-gpu → parser → valider → appliquer à l'état → effet hôte → acquittement
```

Aucune étape ne remonte. Une commande invalide produit un verdict et **n'est
jamais appliquée partiellement** : soit l'état change complètement, soit il ne
change pas du tout. Les compteurs (`applied`, `rejected`) changent toujours.

## 2. Portabilité du cœur (règle dure)

`core/` est en **C11 pur** : aucun `fopen`, aucune horloge (`time`, `clock`),
aucune socket, aucune API QNX, aucun `printf` hors `core/cli/` et des rapports.
L'appelant fournit les commandes, les tampons et la mémoire. Le même noyau
s'exécute depuis un test unitaire sur PC, depuis le jumeau Python (par
comparaison) et depuis le pilote invité QNX.

## 3. Commandes acceptées (niveau S)

Codes = `enum virtio_gpu_ctrl_type` des en-têtes standard (vérifié le 2026-11-12).

| Nom DSL | Type | Champs | Effet |
|---|---|---|---|
| `get_display_info` | `0x0100` | — | aucune mutation ; verdict `TOILE_OK` |
| `resource_create_2d` | `0x0101` | `id format width height` | crée la ressource |
| `resource_unref` | `0x0102` | `id` | détruit la ressource (refusée si un scanout la référence) |
| `set_scanout` | `0x0103` | `scanout id x y width height` | affecte ; `id=0` désactive le scanout |
| `resource_flush` | `0x0104` | `id [x y width height]` | effet hôte « afficher » |
| `transfer_to_host_2d` | `0x0105` | `id [x y width height] [offset]` | copie invité → hôte |
| `resource_attach_backing` | `0x0106` | `id pages` | attache les pages invité |
| `resource_detach_backing` | `0x0107` | `id` | détache les pages |
| `update_cursor` | `0x0300` | `scanout x y [id hot_x hot_y]` | curseur matériel (visible) |
| `move_cursor` | `0x0301` | `scanout x y` | déplace un curseur déjà visible |
| `transfer_from_host` | `0x0206` | `id [x y width height] [offset]` | lecture hôte → invité |

`transfer_from_host` porte le code `VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D`
(0x0206) : c'est la seule commande de lecture hôte→invité du standard (§9).

Alias de clés acceptés au parser : `id`≡`resource`, `w`≡`width`, `h`≡`height`.
Un rectangle omis sur `resource_flush`, `transfer_to_host_2d` et
`transfer_from_host` prend **tout le rectangle de la ressource** ; `offset`
vaut 0 par défaut. Règle exacte de développement : un rectangle dont `width`
ET `height` valent 0 (omis ou écrit explicitement) est développé par le
processeur en `x=0, y=0, width=resource.width, height=resource.height`
avant application — identique en C et en Python.

Formats de pixels (`toile_fmt`, valeurs du standard, tous à 4 octets/pixel) :

| Nom | Valeur | Nom | Valeur |
|---|---|---|---|
| `B8G8R8A8_UNORM` | 1 | `R8G8B8A8_UNORM` | 67 |
| `B8G8R8X8_UNORM` | 2 | `X8B8G8R8_UNORM` | 68 |
| `A8R8G8B8_UNORM` | 3 | `A8B8G8R8_UNORM` | 121 |
| `X8R8G8B8_UNORM` | 4 | `R8G8B8X8_UNORM` | 134 |

Limites : `TOILE_MAX_RESOURCES` 64, `TOILE_MAX_SCANOUTS` 16,
`TOILE_MAX_PAGES` 32 pages par ressource, `TOILE_CAUSE_MAX` 96 octets.
Un `scanout` ≥ 16 ou une table pleine est une erreur, jamais un débordement.

## 4. Grammaire des fixtures (DSL)

Fichier texte UTF-8, LF, une commande par ligne :

```
# commentaire
resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800
resource_attach_backing id=1 pages=0x1000:4096,0x2000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800
resource_flush id=1
```

- ligne vide ou commençant par `#` : ignorée ;
- `nom clé=valeur …`, séparateurs = espaces/tabulations ;
- entiers décimaux ou `0x…` ; `pages` = `addr:len` séparés par des virgules,
  dans l'ordre, au plus 32 entrées ;
- clé inconnue, clé manquante, valeur non numérique, nom inconnu → **erreur de
  parse** (code de sortie 2), message `fixtures/x.tcl:<ligne>: <raison>` ;
  ce n'est pas un verdict du modèle.

Extension des fixtures de parse : `.tcl` (Toile Command Language). Les
fixtures « fil » (octets réels) vivent dans `fixtures/wire/*.hex` : une ligne
de paires hexadécimales par commande, séparées par des espaces ; `#` autorisé.

## 5. État du modèle

```
ressources : id → {format, width, height, pages[], flush_count}
scanouts   : scanout_id → {resource_id, x, y, width, height}
curseur    : {resource_id, scanout_id, x, y, hot_x, hot_y, visible}
compteurs  : cmds_applied, cmds_rejected, flushes, bytes_transferred
```

Invariants (P1), vérifiés **après chaque commande** :

| # | Invariant |
|---|---|
| I1 | tout scanout actif référence une ressource existante |
| I2 | `resource_flush` ne réussit que sur une ressource attachée à un scanout |
| I3 | `transfer_to_host_2d` exige un backing attaché |
| I4 | aucune ressource active avec `width==0`, `height==0` ou format inconnu |
| I5 | les tables ne débordent jamais leurs limites (§3) |
| I6 | `bytes_transferred` = somme des aires × bpp des transferts réussis |

Une violation produit `TOILE_MODEL_COUNTEREXAMPLE` (erreur) avec la cause et
**arrête** la séquence (fail fast). En fin de séquence propre, le processeur
émet `TOILE_MODEL_CONSISTENT`.

## 6. Verdicts

Enum `toile_verdict_code` (valeurs figées) :

| # | Code | Sévérité | Champs renseignés |
|---|---|---|---|
| 0 | `TOILE_OK` | info | cmd_id |
| 1 | `TOILE_CMD_INVALID` | error | cmd_id, cause |
| 2 | `TOILE_RESOURCE_EXISTS` | warning | cmd_id, resource_id |
| 3 | `TOILE_RESOURCE_MISSING` | error | cmd_id, resource_id |
| 4 | `TOILE_BACKING_MISSING` | error | cmd_id, resource_id |
| 5 | `TOILE_SCANOUT_INVALID` | error | cmd_id, scanout_id, resource_id |
| 6 | `TOILE_FLUSH_NO_SCANOUT` | warning | cmd_id, resource_id |
| 7 | `TOILE_TRANSFER_OK` | info | cmd_id, resource_id, rect |
| 8 | `TOILE_MODEL_CONSISTENT` | info | état |
| 9 | `TOILE_MODEL_COUNTEREXAMPLE` | error | cmd_id, cause, état |

Causes figées (chaîne exacte, ASCII, espaces interdits) :

| Cause | Déclencheur |
|---|---|
| `unknown_command_type` | type hors table §3 |
| `bad_format` | format absent de §3 |
| `bad_geometry` | `width==0` ou `height==0` à la création |
| `resource_table_full` | 64 ressources actives |
| `scanout_table_full` | 16 scanouts actifs |
| `backing_entry_count` | 0 ou > 32 entrées |
| `resource_referenced_by_scanout` | `resource_unref` d'une ressource affichée |
| `cursor_not_visible` | `move_cursor` sans curseur visible |
| `scanout_id_out_of_range` | `scanout` ≥ 16 |
| `invariant_i1` … `invariant_i6` | violation de l'invariant §5 |

## 7. Formats de sortie (octet pour octet)

Les trois formats sont produits de façon identique par `core/src/report.c` et
`host/toile/report.py`. Le test différentiel les compare **octet pour octet**.

### 7.1 `text` (défaut)

```
toile v1 text
%04u %-26s %-7s <payload>\n     ← une ligne par verdict, dans l'ordre
# state resources=%u scanouts=%u hash=%016llx applied=%llu rejected=%llu flushes=%llu bytes=%llu cursor=%u,%u,%u,%u,%u,%u\n
```

`<payload>` = champs `clé=valeur` séparés par **une** espace, dans cet ordre,
omission si non applicable selon §6 : `cmd=0x%04x`, `resource=%u`,
`scanout=%u`, `rect=%u,%u,%u,%u`, `cause=<chaîne>`, `state=%u,%u`
(ressources,scanouts). `TOILE_MODEL_CONSISTENT` n'a pas de payload : dans ce
cas les espaces de remplissage de fin de ligne sont supprimés (la ligne ne
se termine pas par des espaces).

### 7.2 `json`

Une seule ligne + `\n`, aucune espace hors des chaînes, ordre des clés figé :

```
{"toile":"1","state":{"resources":R,"scanouts":S,"hash":"%016llx","applied":A,"rejected":J,"flushes":F,"bytes":B,"cursor":{"resource":U,"scanout":K,"x":X,"y":Y,"hot_x":HX,"hot_y":HY,"visible":V}},"verdicts":[{"i":0,"code":"TOILE_OK","sev":"info","cmd":"0x0101","resource":1}]}
```

Objet verdict : clés dans l'ordre `i, code, sev, cmd, resource, scanout, rect,
cause` ; `rect` est la chaîne `"%u,%u,%u,%u"` ; `visible` est `0`/`1` ;
toute clé non applicable est **absente** (jamais `null`).

### 7.3 `md` (audit)

```
# Rapport d'audit Toile

- fixture : `<nom>`
- verdicts : N (erreurs E, avertissements W, info I)
- état : R ressources, S scanouts, hash `<%016llx>`

| # | code | sévérité | cmd | ressource | scanout | rect | cause |
|---|---|---|---|---|---|---|---|
| 0000 | TOILE_OK | info | 0x0101 | 1 | – | – | – |
```

Cellule vide = `–` (tiret cadratin U+2013). Aucune ligne vide après la dernière
ligne du tableau ; fichier terminé par `\n`.

## 8. Hachage de l'état (P3, déterminisme)

FNV-1a 64 bits (base `0xcbf29ce484222325`, premier `0x100000001b3`) sur la
sérialisation canonique ASCII suivante, affichée en **16 hexadécimaux
minuscules** :

```
toile-state-v1\n
R %u %u %u %u %u\n                      ← id, format, width, height, n_pages (ids croissants)
P %016llx %u\n                          ← addr, length (ordre d'attachement)
S %u %u %u %u %u %u %llu\n              ← scanout_id, resource_id, x, y, w, h, flush_count (ids croissants)
C %u %u %u %u %u %u %u\n                ← curseur : resource, scanout, x, y, hot_x, hot_y, visible
K %llu %llu %llu %llu\n                 ← applied, rejected, flushes, bytes
```

Seules les entrées **actives** sont sérialisées. Même séquence ⇒ même hash.

## 9. Corrections relevées dans les sources (§13 du cahier des charges)

| Affirmation du cahier | Vérification (2026-11-12, `virtio_gpu.h` QEMU/Linux) |
|---|---|
| `TRANSFER_FROM_HOST_2D` existe | **Faux** en virtio 1.x : la lecture hôte→invité est `TRANSFER_FROM_HOST_3D` (0x0206), valable aussi pour une ressource 2D. Toile l'expose sous le nom `transfer_from_host` |
| `SET_CURSOR` (0x0302) existe | **Faux** : l'enum s'arrête à `MOVE_CURSOR` (0x0301). Toile n'implémente pas 0x0302 |
| `RESOURCE_CREATE_BLOB`/`RESOURCE_MAP_BLOB` | Présents (0x010c/0x0208) mais **hors socle** (niveau X4) |
| `VIRTIO_GPU_MAX_SCANOUTS` = 16 | Confirmé |

Ces corrections sont à reporter dans `TOILE-cahier-des-charges.md` §13 et
`docs/GAP.md`.

## 10. API C publique gelée (`core/include/toile/`)

```c
/* cmdproc.h */
int  toile_cmdproc_run(const toile_cmd *cmds, size_t n, toile_model *model,
                       toile_verdict *out, size_t cap, size_t *n_out);
int  toile_cmd_parse_line(const char *line, unsigned long line_no,
                          toile_cmd *out, char *err, size_t err_cap);
const char *toile_cmd_name(uint32_t type);

/* resource.h */
void toile_model_init(toile_model *m);
int  toile_resource_create(toile_model *m, uint32_t id, toile_fmt fmt,
                           uint32_t w, uint32_t h);
int  toile_resource_attach_backing(toile_model *m, uint32_t id,
                                   const toile_page *pages, size_t n);
int  toile_resource_detach_backing(toile_model *m, uint32_t id);
int  toile_resource_unref(toile_model *m, uint32_t id);
int  toile_resource_flush(toile_model *m, uint32_t id, toile_rect rect);
int  toile_resource_transfer_to_host(toile_model *m, uint32_t id, toile_rect rect,
                                     uint64_t offset);
int  toile_resource_transfer_from_host(toile_model *m, uint32_t id, toile_rect rect,
                                       uint64_t offset);
const toile_resource *toile_resource_find(const toile_model *m, uint32_t id);
uint64_t toile_model_state_hash(const toile_model *m);

/* scanout.h */
int  toile_scanout_set(toile_model *m, uint32_t scanout, uint32_t resource_id,
                       uint32_t x, uint32_t y, uint32_t w, uint32_t h);
const toile_scanout *toile_scanout_find(const toile_model *m, uint32_t scanout);
int  toile_scanout_count(const toile_model *m);
int  toile_resource_has_scanout(const toile_model *m, uint32_t resource_id);
int  toile_model_check_invariants(const toile_model *m, char *cause, size_t cap);

/* verdict.h */
const char *toile_verdict_name(toile_verdict_code code);
const char *toile_verdict_severity(toile_verdict_code code);
int  toile_verdict_is_error(toile_verdict_code code);

/* vgproto.h */
int  toile_vg_encode(const toile_cmd *cmd, uint8_t *buf, size_t cap, size_t *written);
int  toile_vg_decode(const uint8_t *buf, size_t len, toile_cmd *out);
uint32_t toile_fmt_bpp(toile_fmt fmt);
int  toile_fmt_parse(const char *name, toile_fmt *out);

/* report.h */
int  toile_report_text(const toile_model *m, const toile_verdict *v, size_t n,
                       const char *source, char *buf, size_t cap, size_t *written);
int  toile_report_json(const toile_model *m, const toile_verdict *v, size_t n,
                       const char *source, char *buf, size_t cap, size_t *written);
int  toile_report_md(const toile_model *m, const toile_verdict *v, size_t n,
                     const char *source, char *buf, size_t cap, size_t *written);
```

Les fonctions d'état renvoient **0 (`TOILE_OK`)** ou un code positif de
`toile_verdict_code`. Elles n'écrivent jamais dans un fichier, n'allouent
jamais (mémoire fournie par l'appelant) et ne modifient pas l'état en cas
d'échec.

## 11. CLI (identique en C et en Python)

```
toile-run run    <fixture> [--format text|json|md] [--expect <golden>]
toile-run wire   <hexfile> [--format text|json|md] [--expect <golden>]
toile-run golden <fixture> [--format text|json|md]   # écrit fixtures/golden/<nom>.<fmt>
toile-run selftest
python3 -m toile.cli <mêmes sous-commandes>          # jumeau Python
```

Codes de sortie : `0` aucune erreur, `1` au moins un verdict de sévérité
`error`, `2` usage/fixture invalide, `3` divergence avec `--expect`.

## 12. Makefile — cibles gelées

| Cible | Effet |
|---|---|
| `make build` | `build/toile-run` + binaires de tests (gcc, `-std=c11 -O2 -Werror -Wall -Wextra`) |
| `make test-c` | tests unitaires C (`core/tests/`) |
| `make test-py` | tests unitaires Python (`host/tests/`, stdlib `unittest`) |
| `make diff` | différentiel C↔Python sur toutes les fixtures, octet pour octet |
| `make golden` | régénère `fixtures/golden/*` (avec le binaire C, référence) |
| `make guards` | `.github/checks/taille.py` + `.github/checks/api-frozen.py` |
| `make test` | `build test-c test-py diff guards` — **vert sans QNX, QEMU ni GPU** (S9) |
| `make qemu-demo` | volet A : documenté, exige SDP QNX + QEMU, jamais appelé en CI PR |
| `make bench` | débit du cœur portable (essai E5) : `tools/bench-model.c`, hors CI PR |
| `make clean` | supprime `build/` et `__pycache__/` |

## 13. Gardes CI (budget 5 min)

| Garde | Fichier | Règle |
|---|---|---|
| Taille | `.github/checks/taille.py` | tout fichier versionné < 500 lignes ; toute fonction C/Python < 50 lignes |
| API gelée | `.github/checks/api-frozen.py` | chaque signature de §10 est présente dans les en-têtes |
| Budget CI | `.github/ci-budget/check_timeouts.py` | chaque job déclare `timeout-minutes` ≤ 5 |
| Build | `ci.yml` | `make test` sur runner self-hosted `toile-worker` (r630) |

Règles CI communes : `concurrency` + `cancel-in-progress`, `fetch-depth: 1`
(seule entorse : `hermes-conflict-resolver` qui rebasé a besoin de
`fetch-depth: 0`), build natif long et E2E QEMU **hors PR**.
