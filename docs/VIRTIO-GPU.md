# VIRTIO-GPU — état de l'art pour commencer

Public visé : un lecteur qui connaît QNX ou QEMU mais n'a jamais ouvert une
spécification virtio. Ce document décrit le périphérique **virtio-gpu 2D** tel
que le standard le définit, puis ce que Toile en implémente et ce qu'il écarte.

**Sources.** Spécification OASIS virtio 1.x, section « GPU Device » ; en-tête
standard `virtio_gpu.h` (Linux/QEMU), relu le **2026-11-12** pour
`CONTRACT.md` §9 et recontrôlé sur cette machine dans
`/usr/include/linux/virtio_gpu.h` (`linux-libc-dev` 6.8.0-149) ; implémentation
hôte de référence QEMU, `hw/display/virtio-gpu.c`. Les codes cités ici sont ceux
de ces sources, pas des souvenirs : deux affirmations du cahier des charges ont
d'ailleurs été **corrigées** par cette relecture (`TRANSFER_FROM_HOST_2D` et
`SET_CURSOR` n'existent pas — voir `GAP.md` §3).

---

## 1. Le périphérique en trois idées

1. **virtio-gpu est un périphérique virtuel paravirtualisé** : l'invité ne pilote
   pas une carte, il envoie des **commandes de contrôle** à l'hôte, qui tient le
   framebuffer et l'affiche dans sa fenêtre (ou sur une tête réelle). L'invité
   reste propriétaire de ses pixels : ce sont des pages de sa mémoire.
2. **Identité** : type de périphérique virtio **16** ; en PCI moderne, le DID
   vaut `0x1050` (`0x1040 + 16`). Sous QEMU : `-device virtio-gpu-pci` (PCI) ou
   `-device virtio-gpu-device` (MMIO), et `-device virtio-vga` qui ajoute la
   compatibilité VGA.
3. **Le modèle est « ressources + scanouts »** : une *ressource* est une surface
   2D (format, largeur, hauteur, pages invité attachées) ; un *scanout* est une
   tête virtuelle à laquelle on affecte une ressource, avec une position. Afficher
   = transférer les pixels vers l'hôte, puis demander un flush.

L'espace de configuration du périphérique (`struct virtio_gpu_config`) expose
`events_read`, `events_clear`, `num_scanouts`, `num_capsets`. Le maximum de
scanouts du standard est **`VIRTIO_GPU_MAX_SCANOUTS` = 16** (confirmé le
2026-11-12) — d'où `TOILE_MAX_SCANOUTS` = 16 dans le modèle.

## 2. Les files virtuelles : `controlq` et `cursorq`

| File | Indice | Contenu |
|---|---|---|
| `controlq` | 0 | toutes les commandes de contrôle (création, backing, scanout, transfert, flush) et leurs réponses |
| `cursorq` | 1 | uniquement le curseur (`update_cursor`, `move_cursor`), pour que la souris reste fluide même si `controlq` est saturée |

Chaque requête occupe deux descripteurs : un **lisible** (la commande, en-tête
de contrôle + corps) et un **écrivable** (la réponse de l'hôte, au minimum un
en-tête de contrôle). L'en-tête `virtio_gpu_ctrl_hdr` fait **24 octets**
(`type`, `flags`, `fence_id`, `ctx_id`, `ring_idx`, `padding[3]`) — c'est
`TOILE_VG_HDR_LEN`. Le bit `VIRTIO_GPU_FLAG_FENCE` (1 << 0) demande un acquittement
ordonnancé (fence) ; `VIRTIO_GPU_FLAG_INFO_RING_IDX` (1 << 1) sélectionne l'anneau
de fences — ni l'un ni l'autre ne fait partie du socle S.

La réponse d'une requête `get_display_info` est la plus grosse du socle :
24 + 16 × 24 = **408 octets** (une entrée `pmode` par scanout) — d'où la
capacité de réponse de 512 octets du pool de requêtes de `guest/virtio/`.

## 3. Cycle de vie d'une ressource

Ordre canonique, et ce que chaque étape exige :

```
resource_create_2d        → la ressource existe (format, largeur, hauteur) ; pas encore de pixels
resource_attach_backing   → l'invité donne la liste de ses pages (addr:len) ; sans backing, pas de transfert
set_scanout               → la ressource est affectée à une tête (scanout) avec position et taille
transfer_to_host_2d       → copie invité → hôte des pixels (exige un backing)
resource_flush            → l'hôte affiche (n'a de sens que si la ressource est affichée par un scanout)
resource_detach_backing   → l'invité reprend ses pages
resource_unref            → la ressource est détruite
```

Deux contraintes d'ordre que Toile fait respecter comme des **invariants**
(`CONTRACT.md` §5) :

- un flush sur une ressource qu'aucun scanout n'affiche est un avertissement
  (`TOILE_FLUSH_NO_SCANOUT`) : les pixels partent vers l'hôte mais rien ne
  s'affiche ;
- un transfert exige un backing (`TOILE_BACKING_MISSING` sinon) : sans pages, il
  n'y a rien à copier.

Et une contrainte que Toile **durcit** par rapport au standard (décision D11) :
`resource_unref` d'une ressource encore référencée par un scanout actif est
**refusée** (`resource_referenced_by_scanout`) — il faut d'abord désactiver le
scanout (`set_scanout scanout=k id=0`). Le standard laisse faire ; Toile préfère
un refus net à un scanout orphelin (invariant I1).

## 4. Commandes et codes réels

Codes de `enum virtio_gpu_ctrl_type`. Colonne **Toile** : ce que le niveau S
implémente (DSL `CONTRACT.md` §3) et ce qu'il écarte.

| Code | Nom standard | Champs utiles | Toile |
|---|---|---|---|
| `0x0100` | `GET_DISPLAY_INFO` | — (réponse : 16 `pmodes`) | ✅ `get_display_info`, sans mutation |
| `0x0101` | `RESOURCE_CREATE_2D` | `resource_id`, `format`, `width`, `height` | ✅ `resource_create_2d` |
| `0x0102` | `RESOURCE_UNREF` | `resource_id` | ✅ `resource_unref` (refusée si affichée, D11) |
| `0x0103` | `SET_SCANOUT` | `rect`, `scanout_id`, `resource_id` | ✅ `set_scanout` (`id=0` désactive) |
| `0x0104` | `RESOURCE_FLUSH` | `rect`, `resource_id` | ✅ `resource_flush` |
| `0x0105` | `TRANSFER_TO_HOST_2D` | `rect`, `offset`, `resource_id` | ✅ `transfer_to_host_2d` |
| `0x0106` | `RESOURCE_ATTACH_BACKING` | `resource_id`, `nr_entries` + entrées `mem_entry` | ✅ `resource_attach_backing` (≤ 32 entrées) |
| `0x0107` | `RESOURCE_DETACH_BACKING` | `resource_id` | ✅ `resource_detach_backing` |
| `0x0206` | `TRANSFER_FROM_HOST_3D` | `box`, `offset`, `resource_id`, `level`, `stride`, `layer_stride` | ✅ **`transfer_from_host`** — seule lecture hôte → invité du standard, valable pour une ressource 2D (D12) |
| `0x0300` | `UPDATE_CURSOR` | `pos` (`scanout_id`, `x`, `y`), `resource_id`, `hot_x`, `hot_y` | ✅ `update_cursor` (curseur visible) |
| `0x0301` | `MOVE_CURSOR` | `pos` seulement | ✅ `move_cursor` (exige un curseur visible) |
| `0x0108`–`0x0109` | `GET_CAPSET_INFO` / `GET_CAPSET` | jeux de capacités 3D | ❌ écarté (3D) |
| `0x010a` | `GET_EDID` | EDID d'une tête | ❌ écarté |
| `0x010b` | `RESOURCE_ASSIGN_UUID` | UUID de ressource | ❌ écarté |
| `0x010c` / `0x010d` | `RESOURCE_CREATE_BLOB` / `SET_SCANOUT_BLOB` | blob resources | ❌ écarté (niveau X4) |
| `0x0200`–`0x0205`, `0x0207` | `CTX_*`, `RESOURCE_CREATE_3D`, `TRANSFER_TO_HOST_3D`, `SUBMIT_3D` | contextes 3D, commande de flux 3D | ❌ écarté (X3) |
| `0x0208` / `0x0209` | `RESOURCE_MAP_BLOB` / `RESOURCE_UNMAP_BLOB` | mémoire visible de l'hôte | ❌ écarté (X4) |
| `0x0302` | — | **n'existe pas** : l'enum s'arrête à `MOVE_CURSOR` | ❌ `SET_CURSOR` inventé par le cahier, corrigé (D13) |

Un type hors de cette table produit la cause `unknown_command_type`
(`TOILE_CMD_INVALID`), jamais un comportement implicite.

## 5. Formats de pixels et octets par pixel

Les huit formats « simples » du standard (`enum virtio_gpu_formats`) sont tous
des formats 32 bits, donc **4 octets par pixel** — c'est ce que renvoie
`toile_fmt_bpp` (`CONTRACT.md` §3) :

| Nom | Valeur | bpp | Nom | Valeur | bpp |
|---|---|---|---|---|---|
| `B8G8R8A8_UNORM` | 1 | 4 | `R8G8B8A8_UNORM` | 67 | 4 |
| `B8G8R8X8_UNORM` | 2 | 4 | `X8B8G8R8_UNORM` | 68 | 4 |
| `A8R8G8B8_UNORM` | 3 | 4 | `A8B8G8R8_UNORM` | 121 | 4 |
| `X8R8G8B8_UNORM` | 4 | 4 | `R8G8B8X8_UNORM` | 134 | 4 |

Conséquences pour Toile : `bytes_transferred` = aire du rectangle × 4 (invariant
I6) ; aucune gestion de plan YUV ni de bpp variable dans le socle (les formats
YUV sont en X2) ; un format absent de cette table → `bad_format`.

## 6. Réponses de l'hôte

| Code | Nom | Sens |
|---|---|---|
| `0x1100` | `RESP_OK_NODATA` | succès, pas de données — la réponse normale d'une commande 2D |
| `0x1101` | `RESP_OK_DISPLAY_INFO` | succès avec les 16 `pmodes` (réponse à `GET_DISPLAY_INFO`) |
| `0x1102`–`0x1106` | `RESP_OK_CAPSET_INFO`, `CAPSET`, `EDID`, `RESOURCE_UUID`, `MAP_INFO` | succès des familles écartées (3D, EDID, UUID, blob) |
| `0x1200` | `RESP_ERR_UNSPEC` | erreur non précisée |
| `0x1201` | `RESP_ERR_OUT_OF_MEMORY` | l'hôte manque de mémoire |
| `0x1202` | `RESP_ERR_INVALID_SCANOUT_ID` | scanout hors bornes |
| `0x1203` | `RESP_ERR_INVALID_RESOURCE_ID` | ressource inconnue |
| `0x1204` | `RESP_ERR_INVALID_CONTEXT_ID` | contexte 3D inconnu (écarté) |
| `0x1205` | `RESP_ERR_INVALID_PARAMETER` | paramètre incohérent |

Attention au vocabulaire : ces codes sont ceux **du périphérique vers l'invité**.
Le cœur de Toile, lui, ne produit pas de réponses fil — il produit des
**verdicts** (`TOILE_OK`, `TOILE_RESOURCE_MISSING`, `TOILE_SCANOUT_INVALID`…)
qui tracent la décision du modèle. `core/include/toile/vgproto.h` définit les
codes de réponse ci-dessus pour que le pilote invité puisse **vérifier ce que
QEMU a répondu** après `toile_tr_submit` ; `0x1204` n'y figure pas, les
contextes 3D étant hors socle.

## 7. Ce que Toile écarte, et pourquoi

| Écarté | Raison |
|---|---|
| 3D : contextes (`CTX_*`), capsets, `SUBMIT_3D`, virgl (OpenGL) et venus (Vulkan) | triple l'effort ; le besoin réel (Screen, IHM) est 2D — décision D5, piste X3 |
| blob resources, `SET_SCANOUT_BLOB`, mémoire visible de l'hôte (`MAP_BLOB`/`UNMAP_BLOB`, `SHM_ID_HOST_VISIBLE`) | optimisation de performance, pas du socle — piste X4 |
| `GET_EDID` | l'invité n'a pas besoin d'EDID synthétique pour un scanout unique fixe (S) |
| `RESOURCE_ASSIGN_UUID` | sert au partage inter-processus/inter-périphériques de l'hôte, inutile au modèle |
| fences (`FLAG_FENCE`, `RING_IDX`) | le socle soumet de façon synchrone (`toile_tr_submit`) ; l'ordonnancement fin viendra avec le double tampon/vsync (C4) |
| `SET_CURSOR` `0x0302` | **n'existe pas** dans le standard (D13) |

Ce qui reste est exactement le socle : un invité QNX qui crée une ressource, y
attache ses pages, l'affecte à un scanout, transfère ses pixels et demande un
flush — de quoi afficher un bureau.

## 8. Références exactes

- OASIS *virtio 1.x*, section « GPU Device » : files, commandes, formats,
  `VIRTIO_GPU_MAX_SCANOUTS`.
- En-tête standard `virtio_gpu.h` (uapi Linux / QEMU) : enums
  `virtio_gpu_ctrl_type`, `virtio_gpu_formats`, structures fil. Relu le
  **2026-11-12** (corrections `CONTRACT.md` §9) ; présent sur cette machine sous
  `/usr/include/linux/virtio_gpu.h` (`linux-libc-dev` 6.8.0-149).
- QEMU `hw/display/virtio-gpu.c` (+ `virtio-gpu-pci.c`) : implémentation hôte de
  référence du volet A.
- `guest/virtio/toile_transport.h` : adaptation du transport Passerelle, file
  `controlq` (0) / `cursorq` (1), type 16, DID `0x1050`.
- `NOTICE` : attribution BSD des structures virtio-gpu, Apache-2.0 du transport.
