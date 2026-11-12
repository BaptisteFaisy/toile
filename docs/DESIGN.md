# DESIGN — architecture de Toile

Toile est un **pilote virtio-gpu 2D pour QNX 8.0 en invité** plus un **display
driver Screen** au-dessus. Le projet est découpé en trois couches dont une seule
tourne sur QNX : tout le reste se prouve sur un PC, sans QNX, sans QEMU, sans GPU.

Source normative : [`CONTRACT.md`](../CONTRACT.md) (gelé). Ce document explique
**pourquoi** le découpage est celui-là ; le contrat dit **quoi**.

---

## 1. Les trois couches

```
┌───────────────────────────────────────────────────────────────────────┐
│ Couche 3 — display driver Screen (`guest/display/`)                    │
│   Interface display driver de Screen (essai E2, non tranché) :         │
│   framebuffer, flip/blit, vsync (niveau C). Processus séparé du pilote.│
├───────────────────────────────────────────────────────────────────────┤
│ Couche 2 — pilote invité QNX                                           │
│   `guest/gpu/`     resources, scanouts, flush, resmgr `/dev/gpu0`      │
│   `guest/virtio/`  adaptation du transport Passerelle (controlq/cursorq)│
│   `guest/input/`   virtio-input clavier/souris (niveau C)               │
├───────────────────────────────────────────────────────────────────────┤
│ Couche 1 — cœur portable C11 (`core/`)                                 │
│   Machine à commandes virtio-gpu 2D : parser, validation, état         │
│   (ressources / backings / scanouts / curseur), invariants, verdicts,  │
│   rapports text|json|md, hachage d'état. Jumeau : `host/toile/` (Python)│
└───────────────────────────────────────────────────────────────────────┘
        │  octets fil (controlq / cursorq)
        ▼
   QEMU `virtio-gpu-pci` (volet A)   |   vdev QNX Hypervisor (volet B, niveau X)
```

La dépendance va **vers le bas uniquement**. La couche 1 ne connaît ni la 2 ni la 3 ;
la couche 2 consomme la couche 1 et le transport ; la couche 3 consomme la couche 2.
Conséquence pratique : le cœur se compile et se teste avec `gcc -std=c11` sur un
portable, et le même code est lié plus tard dans le pilote invité compilé avec `qcc`.

## 2. Règle de portabilité du cœur (dure)

`core/` respecte cinq interdictions, vérifiables par lecture et par CI :

| Interdit | Pourquoi |
|---|---|
| aucun fichier (`fopen`, E/S) | l'appelant fournit les octets ; le cœur ne dépend d'aucun système de fichiers |
| aucune horloge (`time`, `clock`) | toute trace non déterministe casserait P3 (déterminisme) |
| aucune socket | rien à négocier : le cœur est une fonction pure de la séquence |
| aucune API QNX | le cœur doit compiler avec gcc sur PC **et** avec `qcc` sur cible |
| **aucune allocation dynamique** | `toile_model` est un tableau fixe fourni par l'appelant (64 ressources, 16 scanouts, 32 pages/ressource) ; zéro `malloc`, zéro échec d'allocation, zéro fuite |

Le corollaire : **toute fonction du cœur renvoie `TOILE_OK` (0) ou un code de
verdict positif, et ne modifie jamais l'état en cas d'échec** (`CONTRACT.md` §10).
La mémoire des verdicts et des rapports est aussi fournie par l'appelant.

## 3. Pourquoi un jumeau Python indépendant

`host/toile/` réécrit la même machine à commandes en Python (stdlib seule),
**sans partager de code** avec le C. Ce n'est pas une traduction automatique ni
une couche de confort : c'est une **seconde opinion exécutable**.

- Le C est rapide, proche du métal, et c'est lui qui tournera dans le pilote.
  Le Python est lisible en revue, et c'est lui qu'on modifie en premier quand le
  contrat bouge.
- Les deux produisent les rapports `text`, `json`, `md` **octet pour octet**
  identiques et le même hachage d'état FNV-1a 64 bits (`CONTRACT.md` §7, §8).
- `tests/differential.py` (cible `make diff`) compare les deux sur toutes les
  fixtures. Une divergence est un bug de l'une des deux implémentations, jamais
  un « écart acceptable » : le différentiel est la garde-fou du contrat.

La valeur ajoutée est **différentielle** : une faute de logique dans le C
(débordement, ordre de parcours, arrondi d'un compteur) a très peu de chances
d'être reproduite à l'identique dans une implémentation écrite séparément.

## 4. Flux : commande → verdict → état → effet hôte → acquittement

Chaîne figée (`CONTRACT.md` §1), aucune étape ne remonte :

```
commande virtio-gpu → parser → valider → appliquer à l'état → effet hôte → acquittement
```

1. **commande** : une ligne de DSL (`.tcl`) ou des octets fil (`.hex`), décodés
   en `toile_cmd` par `toile_cmd_parse_line` / `toile_vg_decode`.
2. **verdict** : la validation produit toujours une trace — `TOILE_OK`
   (info), un avertissement (`TOILE_RESOURCE_EXISTS`, `TOILE_FLUSH_NO_SCANOUT`)
   ou une erreur (`TOILE_RESOURCE_MISSING`, `TOILE_BACKING_MISSING`,
   `TOILE_SCANOUT_INVALID`, `TOILE_CMD_INVALID`).
3. **état** : application **tout ou rien**. Une commande refusée ne laisse aucune
   trace dans les tables ; seuls les compteurs `cmds_applied` / `cmds_rejected`
   changent.
4. **effet hôte** : `resource_flush` (afficher) et `transfer_to_host_2d`
   (copier invité → hôte) / `transfer_from_host` (lire hôte → invité) modifient
   les compteurs `flushes` et `bytes_transferred`.
5. **acquittement** : le verdict est écrit dans le tableau de sortie, puis la
   séquence continue — sauf **fail fast** : la première violation d'invariant
   (I1..I6) arrête la séquence et émet `TOILE_MODEL_COUNTEREXAMPLE` (décision D16).

Après chaque commande, `toile_model_check_invariants` vérifie I1..I6 ; en fin de
séquence propre, le processeur émet `TOILE_MODEL_CONSISTENT`.

## 5. Pourquoi un DSL texte pour les fixtures

Les séquences de commandes de référence sont des fichiers texte `.tcl`
(*Toile Command Language*, `CONTRACT.md` §4) :

```
# boot graphique minimal
resource_create_2d id=1 format=B8G8R8A8_UNORM width=1280 height=800
resource_attach_backing id=1 pages=0x1000:4096,0x2000:4096
set_scanout scanout=0 id=1 x=0 y=0 width=1280 height=800
resource_flush id=1
```

- **Déterminisme** : un fichier texte est reproductible, sans horodatage ni
  ordre de sérialisation variable ; le même fichier rejoué donne le même hachage.
- **Lisible en revue** : une PR qui ajoute un cas de test se lit comme une phrase.
  Un binaire de trace ne se relit pas, et un JSON de 400 lignes non plus.
- **Diffable** : `git diff` montre exactement quelle commande a changé.
- **Double entrée** : le même cœur accepte aussi les octets réels
  (`fixtures/wire/*.hex`), ce qui permet de tester l'encodeur/décodeur fil sans
  changer de fixture.
- Les erreurs de **parse** (clé inconnue, valeur non numérique) ne sont pas des
  verdicts du modèle : code de sortie 2, message `fixtures/x.tcl:<ligne>: <raison>`.

## 6. Place du transport Passerelle : adapté, non réécrit

Le transport virtio (PCI moderne MSI-X, MMIO v1/v2, files virtuelles, négociation
de fonctionnalités) existe déjà dans le projet frère **Passerelle** (Apache-2.0,
même auteur). Toile **ne réécrit pas les virtqueues** : `guest/virtio/`
(`toile_transport.h`) est une couche fine qui délègue chaque opération d'anneau,
de négociation et d'interruption à l'API `pgw_vio_*` / `pgw_vq_*` de Passerelle,
et ajoute ce qui manque pour un GPU :

- identité virtio-gpu (type de périphérique 16, DID PCI moderne `0x1050`) ;
- files nommées `controlq` (0) et `cursorq` (1) ;
- un pool de requêtes avec tampons DMA (commande lisible / réponse écrivable),
  alloués par le hook `vq_alloc` de Passerelle — mémoire physiquement contiguë
  non cachée sur QNX, tas identity-mapped sur PC ;
- une soumission synchrone (`toile_tr_submit`) et un trio asynchrone
  (`push` / `kick` / `poll`) ;
- une ouverture **mock** (`toile_tr_open_mock`) pour exercer la couche sur PC.

Les points d'entrée de découverte PCI/MMIO de Passerelle n'autorisent que 9P et
virtiofs : le patch `guest/virtio/toile-virtio-gpu.patch` les étend au type GPU.
Il est appliqué à une **copie de build** des sources Passerelle, jamais à
l'arbre partagé. Attribution : `NOTICE` (Apache-2.0), décision D2, essai E3.

## 7. Stratégie de preuve P1 / P2 / P3

| Preuve | Énoncé | Moyen |
|---|---|---|
| **P1 — cohérence** | après toute séquence valide, chaque scanout actif référence une ressource existante, chaque flush porte sur une ressource affichée, les compteurs sont justes | invariants I1..I6 vérifiés après chaque commande ; verdict `TOILE_MODEL_CONSISTENT` |
| **P2 — rejet propre** | une commande invalide est refusée avec une cause figée, **sans application partielle** | fixtures fautives ; comparaison de l'état avant/après ; causes `resource_referenced_by_scanout`, `bad_geometry`, `scanout_id_out_of_range`… |
| **P3 — déterminisme** | la même séquence produit exactement le même état et les mêmes verdicts | hachage FNV-1a 64 bits d'une sérialisation canonique ASCII (`CONTRACT.md` §8), comparé entre C et Python et entre deux exécutions |

P1–P3 sont prouvées **sans cible**. Elles ne remplacent pas la validation contre
QEMU réel (exigence C8 : capture des commandes émises par le pilote, rejeu dans
le modèle) — elles la rendent interprétable : quand l'état rejoué diffère de
celui de QEMU, on sait que le bug est dans le modèle ou dans le pilote, pas dans
le harnais. Détail des moyens : [`VALIDATION.md`](VALIDATION.md).
