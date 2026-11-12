# Fixtures « fil » Toile — `fixtures/wire/*.hex`

Octets **réels** du protocole virtio-gpu 2D, tels qu'ils circulent sur le fil
entre l'invité QNX 8.0 et l'hôte. Sources de vérité : `CONTRACT.md` §3/§4 et
`core/include/toile/vgproto.h` (structures fil, petit-boutien explicite).

## Format du fichier `.hex`

- texte UTF-8, LF, **une commande par ligne** ;
- chaque ligne : les octets en **paires hexadécimales minuscules séparées par
  des espaces** (`01 01 00 00 …`) ;
- lignes vides ignorées ; `#` autorisé en tête de ligne (commentaire) ;
- une ligne = en-tête de contrôle (24 octets) + charge utile de la commande ;
- tout est **petit-boutien** (le mot `0x0101` s'écrit `01 01 00 00`).

Générées par script `struct` jetable (jamais d'octets écrits à la main), puis
revérifiées par redécodage indépendant.

## En-tête de contrôle — 24 octets (toutes commandes)

`toile_vg_ctrl_hdr` (vgproto.h) : `type` u32, `flags` u32, `fence_id` u64,
`ctx_id` u32, `ring_idx` u8, `padding[3]`. Dans ces fixtures : `flags=0`
(pas de `TOILE_VG_FLAG_FENCE`), `fence_id=0`, `ctx_id=0` (2D, pas de
contexte), `ring_idx=0`.

## Dérivation octet par octet — `resource_create_2d` (40 octets)

Commande : `resource_create_2d id=1 format=B8G8R8A8_UNORM width=64 height=48`
(ligne 3 de `boot-graphique.hex`). Charge utile = `resource_id` u32, `format`
u32, `width` u32, `height` u32 = 16 octets ; total 24 + 16 = 40.

| Offset | Octets | Champ | Valeur | Origine |
|---|---|---|---|---|
| 0–3 | `01 01 00 00` | type u32 LE | 0x0101 | CONTRACT §3 ; `TOILE_VG_CMD_RESOURCE_CREATE_2D` |
| 4–7 | `00 00 00 00` | flags u32 | 0 | pas de fence (§ en-tête, vgproto.h) |
| 8–15 | `00 00 00 00 00 00 00 00` | fence_id u64 | 0 | vgproto.h `toile_vg_ctrl_hdr` |
| 16–19 | `00 00 00 00` | ctx_id u32 | 0 | 2D : pas de contexte |
| 20 | `00` | ring_idx u8 | 0 | vgproto.h |
| 21–23 | `00 00 00` | padding[3] | 0 | vgproto.h |
| 24–27 | `01 00 00 00` | resource_id u32 | 1 | demandé (id=1) |
| 28–31 | `01 00 00 00` | format u32 | 1 | `B8G8R8A8_UNORM` = 1 (table §3, `TOILE_FMT_*`) |
| 32–35 | `40 00 00 00` | width u32 | 0x40 = 64 | demandé (64) |
| 36–39 | `30 00 00 00` | height u32 | 0x30 = 48 | demandé (48) |

Vérification : 40 octets × 3 caractères − 1 = **119 caractères** hors espaces
comptés, soit `40*3-1 = 119` avec les espaces séparateurs.

## Deuxième exemple — `resource_attach_backing` (64 octets)

Charge utile (disposition fil du brief, vérifiée dans les en-têtes standard) :
`resource_id` u32 + `nr_entries` u32 (8 octets), puis `nr_entries` ×
`mem_entry` = `addr` u64 + `length` u32 + `padding` u32 (16 octets chacun).
Ici 2 pages → 24 + 8 + 2×16 = **64 octets**.

| Offset | Octets | Champ | Valeur |
|---|---|---|---|
| 0–23 | `06 01 00 00 …` | en-tête | type 0x0106 (`RESOURCE_ATTACH_BACKING`), reste à 0 |
| 24–27 | `01 00 00 00` | resource_id | 1 |
| 28–31 | `02 00 00 00` | nr_entries | 2 |
| 32–39 | `00 10 00 00 00 00 00 00` | entry[0].addr u64 | 0x1000 |
| 40–43 | `00 10 00 00` | entry[0].length u32 | 0x1000 = 4096 |
| 44–47 | `00 00 00 00` | entry[0].padding | 0 |
| 48–55 | `00 20 00 00 00 00 00 00` | entry[1].addr u64 | 0x2000 |
| 56–59 | `00 10 00 00` | entry[1].length u32 | 4096 |
| 60–63 | `00 00 00 00` | entry[1].padding | 0 |

## Longueurs totales attendues (24 + charge utile)

### `boot-graphique.hex`

| # | Commande | Type | Charge utile | Total octets | Total caractères (avec espaces) |
|---|---|---|---|---|---|
| 1 | `resource_create_2d` | 0x0101 | 16 | **40** | 119 |
| 2 | `resource_attach_backing` (2 pages) | 0x0106 | 8 + 2×16 = 40 | **64** | 191 |
| 3 | `set_scanout` (rect 16 + scanout 4 + id 4) | 0x0103 | 24 | **48** | 143 |
| 4 | `transfer_to_host_2d` (rect 16 + offset 8 + id 4 + pad 4) | 0x0105 | 32 | **56** | 167 |
| 5 | `resource_flush` (rect 16 + id 4 + pad 4) | 0x0104 | 24 | **48** | 143 |

### `curseur.hex`

| # | Commande | Type | Charge utile | Total octets | Total caractères (avec espaces) |
|---|---|---|---|---|---|
| 1 | `update_cursor` (cursor_pos 16 + id/hot/pad 16) | 0x0300 | 32 | **56** | 167 |
| 2 | `move_cursor` (même disposition fil qu'update_cursor, standard) | 0x0301 | 32 | **56** | 167 |

`move_cursor` porte `resource_id=0`, `hot_x=0`, `hot_y=0` : seuls `scanout`,
`x`, `y` sont significatifs (CONTRACT §3 : champs `scanout x y`).

## Contenu sémantique des fixtures

- `boot-graphique.hex` — séquence de démarrage graphique : crée la ressource 1
  (B8G8R8A8_UNORM, 64×48), attache 2 pages invité (0x1000, 0x2000, 4096 o
  chacune), affecte le scanout 0, transfère tout le rectangle vers l'hôte
  (offset 0), puis affiche (flush du rectangle entier).
- `curseur.hex` — curseur matériel : `update_cursor` scanout 0 en (10,20) avec
  la ressource 2 (hot spot 0,0), puis `move_cursor` scanout 0 vers (40,50).

## Cohérence vérifiée

Chaque ligne a été contrôlée par deux scripts jetables indépendants :
(1) nombre d'octets == longueur attendue pour le type (octets 0–3, LE) ;
(2) redécodage `struct.unpack` redonnant exactement les champs demandés.
