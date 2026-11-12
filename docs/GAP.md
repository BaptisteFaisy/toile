# GAP — l'écart entre l'existant et l'objectif

Ce document dit **ce qui manque** (les trous G1–G6 du cahier des charges §2.2),
**ce que Toile livre** à chaque niveau d'ambition, et **ce qui a été corrigé**
depuis la rédaction du cahier. Il tient aussi le registre d'honnêteté : trois
valeurs de statut, jamais plus.

- **vérifié** — relu dans une source primaire (en-tête standard, spécification,
  exécution d'une commande de ce dépôt) à la date indiquée ;
- **partiellement vérifié** — une moitié de la preuve existe (documentaire ou
  code écrit), l'autre exige une cible, un SDP ou une exécution non faite ;
- **non vérifié** — affirmation d'absence ou d'accès, jamais relue page à page.

Une absence dans la documentation publique est **un résultat de recherche, pas
une preuve**. Tant qu'un trou n'est pas « vérifié », il ne peut pas être présenté
comme tel dans la soumission au concours.

---

## 1. Les trous visés (G1–G6, cahier des charges §2.2)

| Code | Trou, tel que formulé dans l'inventaire | Source | Statut |
|---|---|---|---|
| **G1** | **virtio-gpu absent** des ~20 vdev livrés avec QNX Hypervisor | Inventaire lot D (liste des vdev vérifiée) | **non vérifié** — à revérifier page à page dans le SDP installé |
| **G2** | **Aucun display driver Screen pour virtio-gpu** en invité : Screen ne sait pas parler à un GPU virtuel | Inventaire lot D | **non vérifié** — à revérifier (Screen Developer's Guide, essai E2) |
| **G3** | **Screen inutilisable en invité sans passthrough** matériel — le trou le plus cité de l'inventaire | Inventaire lot D | **partiellement vérifié** — vérifié dans l'inventaire lot D ; à confirmer sur QEMU (E1/E2) |
| **G4** | **virtio-input absent** (clavier/souris en VM) : même avec l'affichage, les entrées manquent | Inventaire lot D (idée « Main ») | **non vérifié** — même nature d'absence que G1 |
| **G5** | **Aucune virtualisation GPU documentée** dans la doc publique QNX 8.0 | Inventaire | **partiellement vérifié** — absence constatée dans la doc publique consultée, non dans le SDP installé |
| **G6** | Pas de multi-écran virtuel : un invité ne peut pas piloter plusieurs têtes sans matériel | Conséquence de G1–G3 | **non vérifié** — dépend de G1–G3 |

Rien dans Toile ne ferme ces trous *par affirmation* : le projet les ferme en
écrivant le maillon manquant (pilote virtio-gpu + display driver Screen) et en
montrant le bureau QNX s'afficher dans une fenêtre QEMU (démo, exigence S8).

## 2. Ce que Toile livre, par niveau

### 2.1 Socle **S** (sous-ensemble livrable au 15/12/2026)

| ID | Livrable | Ce qui le prouve | Statut au 2026-11-12 |
|---|---|---|---|
| S1 | `CONTRACT.md` gelé (commandes, états, verdicts, formats) | fichier présent, §1–§13 | **vérifié** |
| S2 | Cœur C11 pur : machine à commandes + invariants | `core/include/toile/*.h` écrits ; `core/src/` en cours | **partiellement vérifié** (API gelée, implémentation en cours) |
| S3 | Jumeau Python + différentiel C↔Python vert | `host/toile/`, `tests/differential.py`, `make diff` | **non vérifié** (lot L2 à écrire) |
| S4 | Fixtures de référence (boot graphique, resize, multi-ressource) | `fixtures/*.tcl`, `fixtures/wire/*.hex`, `fixtures/golden/*` | **non vérifié** (lot L3 à écrire) |
| S5 | Transport virtio Passerelle **adapté, non réécrit** | `guest/virtio/toile_transport.{h,c}`, `toile-virtio-gpu.patch` | **partiellement vérifié** (adaptation écrite, non compilée sur cible QNX) |
| S6 | Driver invité minimal : énumération PCI, controlq, scanout unique, flush | `guest/gpu/` | **non vérifié** (lot L5, bloqué par E1) |
| S7 | Display driver Screen minimal : un framebuffer, une tête, une mire puis le bureau | `guest/display/` | **non vérifié** (lot L6, bloqué par E2) |
| S8 | Démo QEMU : image invitée qui démarre Screen dans la fenêtre QEMU | `image/`, `qemu/`, `make qemu-demo` | **non vérifié** (lot L7, exige SDP + QEMU) |
| S9 | `make test` vert **sans QNX, sans QEMU, sans GPU** | `make test` = `build test-c test-py diff guards` | **non vérifié** (le `Makefile` n'est pas encore écrit) |
| S10 | CI : build `-Werror`, tests, différentiel, garde taille, budget ≤ 5 min | `.github/workflows/ci.yml`, `.github/checks/*.py` | **non vérifié** (gardes à écrire, cibles gelées par `CONTRACT.md` §13) |

### 2.2 Complet **C** (version visée à 3 mois)

Multi-scanout et positionnement (C1), curseur matériel via `cursorq` (C2), suivi
de dommages (C3), double tampon + vsync (C4), virtio-input clavier/souris (C5),
intégration Screen complète avec applications réelles (C6), resource manager
`/dev/gpu0` à API typée (C7), **validation du modèle contre QEMU réel** par
capture/rejeu (C8), codelab 4 chapitres (C9), image `mkifs` reproductible et
scripts QEMU documentés (C10). Tout C est aujourd'hui **non vérifié** : c'est la
cible, pas l'état. Le contrat anticipe déjà C1/C2 — 16 scanouts et les commandes
`update_cursor` (0x0300) / `move_cursor` (0x0301) sont dans le niveau S du DSL.

### 2.3 Extrême **X** (documenté, hors concours)

Volet B : vdev virtio-gpu côté hôte QNX Hypervisor (X1, **bloqué par E4**),
accélération 2D côté hôte (X2), piste 3D virgl/venus documentée (X3), blob
resources + mémoire visible de l'hôte (X4), contribution amont au transport
Passerelle (X5 — le patch `toile-virtio-gpu.patch` en est la première forme).

## 3. Corrections relevées dans les sources (CONTRACT.md §9)

Le cahier des charges §2.3 listait des commandes virtio-gpu **de mémoire**. Les
structures ont été relues le **2026-11-12** dans l'en-tête standard
`virtio_gpu.h` (Linux/QEMU) : deux affirmations étaient fausses, une est
confirmée. Ces corrections sont reportées dans le cahier des charges §13 et
gravées dans `CONTRACT.md` §3 et §9.

| Affirmation du cahier | Preuve (relecture du 2026-11-12) | Conséquence pour Toile |
|---|---|---|
| `TRANSFER_FROM_HOST_2D` existe | **Faux.** En virtio 1.x il n'y a pas de transfert de lecture 2D : la lecture hôte → invité est `VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D`, code **0x0206**, valable aussi pour une ressource créée en 2D | la commande est exposée sous le nom DSL `transfer_from_host` avec le code 0x0206 (décision D12) |
| `SET_CURSOR` (0x0302) existe | **Faux.** L'enum `virtio_gpu_ctrl_type` s'arrête à `VIRTIO_GPU_CMD_MOVE_CURSOR` (0x0301) ; 0x0302 n'est pas défini | Toile n'implémente pas 0x0302 (décision D13) ; le curseur passe par `update_cursor` / `move_cursor` |
| `VIRTIO_GPU_MAX_SCANOUTS` = 16 | **Confirmé** | `TOILE_MAX_SCANOUTS` = 16 ; un `scanout` ≥ 16 est une erreur (`scanout_id_out_of_range`), jamais un débordement |
| `RESOURCE_CREATE_BLOB` / `RESOURCE_MAP_BLOB` | **Présents** (0x010c / 0x0208) mais hors socle | niveau X4, non implémentés, `unknown_command_type` au parse du modèle S |

Les codes conservés et vérifiés le même jour : `0x0100` `get_display_info`,
`0x0101` `resource_create_2d`, `0x0102` `resource_unref`, `0x0103` `set_scanout`,
`0x0104` `resource_flush`, `0x0105` `transfer_to_host_2d`, `0x0106`
`resource_attach_backing`, `0x0107` `resource_detach_backing`, `0x0300`
`update_cursor`, `0x0301` `move_cursor` ; réponses `0x1100` / `0x1101` et erreurs
`0x1200`…`0x1205`. Détail et contexte : [`VIRTIO-GPU.md`](VIRTIO-GPU.md).

## 4. Ce qui reste non vérifié (et qui le tranche)

| Point ouvert | Pourquoi ça bloque | Qui tranche |
|---|---|---|
| **Interface display driver Screen exacte** (contrat `io-display`, bibliothèque, points d'entrée) | S7 ne peut pas être écrit sérieusement sans le contrat réel ; un display driver inventé ne charge pas | **E2** — relecture du Screen Developer's Guide dans le SDP installé, puis essai de chargement |
| **DDK hyperviseur en licence non commerciale** (QNX Everywhere) | le volet B (X1) exige d'écrire un vdev côté hôte ; sans accès, pas de volet B | **E4** — demande de licence et lecture des termes ; ne bloque **que** X1 |
| **Performances d'un flush 2D** (copie du framebuffer vers l'hôte, ≥ 15–20 images/s en 1080p) | décide si le bureau est utilisable ou s'il faut dégainer dommages/double tampon/blob | **E5** — mesure sur cible ; non bloquant, ajuste les ambitions C |
| **Énumération PCI du `virtio-gpu-pci` par un invité QNX 8.0** | sans elle, aucun pilote n'a de périphérique à ouvrir | **E1** — boot QEMU + `pci`/`libpci` en invité |
| **Réutilisation effective du transport Passerelle** (files multiples, MSI-X) | l'adaptation est écrite, pas compilée ni exécutée sur cible | **E3** — compilation `qcc` de `guest/virtio/` + patch appliqué à une copie de build |

## 5. Registre de statut consolidé

| Affirmation | Statut |
|---|---|
| Structures virtio-gpu (codes de commandes, formats, limites) relues dans `virtio_gpu.h` | **vérifié** (2026-11-12) |
| `VIRTIO_GPU_MAX_SCANOUTS` = 16 | **vérifié** |
| `TRANSFER_FROM_HOST_2D` inexistant en virtio 1.x (lecture = 0x0206) | **vérifié** |
| `SET_CURSOR` 0x0302 inexistant (l'enum s'arrête à 0x0301) | **vérifié** |
| API publique C du cœur (`core/include/toile/*.h`) conforme à `CONTRACT.md` §10 | **vérifié** (en-têtes écrits ; garde `api-frozen.py` à exécuter) |
| Attribution du transport (Apache-2.0) et des structures (BSD) | **vérifié** (`NOTICE`) |
| QEMU fournit un périphérique `virtio-gpu-pci` mature | **partiellement vérifié** (standard utilisé par Linux/BSD ; côté invité QNX = E1) |
| Transport Passerelle réutilisable pour virtio-gpu | **partiellement vérifié** (adaptation écrite, non compilée/exécutée = E3) |
| Screen inutilisable en invité sans passthrough (G3) | **partiellement vérifié** (inventaire lot D ; confirmation QEMU = E1/E2) |
| Cœur C11 implémenté et tests verts sans cible | **non vérifié** (implémentation en cours, `Makefile` absent) |
| Interface display driver Screen | **non vérifié** (E2) |
| DDK hyperviseur accessible en licence non commerciale | **non vérifié** (E4) |
| Performances de flush suffisantes pour un bureau utilisable | **non vérifié** (E5) |
| G1, G2, G4, G6 | **non vérifié** (à revérifier dans le SDP installé) |
