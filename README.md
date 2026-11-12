# Toile

**Toile** est un pilote **virtio-gpu 2D** pour **QNX 8.0 en invité** (VM QEMU),
accompagné du **display driver Screen** qui s'appuie dessus. Projet candidat au
**QNX Global Hackathon 2026**, catégorie *Low-level Software*, bonus *Best use of
QNX-specific Technologies*. Licence [MIT](LICENSE).

**Site du projet : <https://tradoooo.github.io/toile/>**

[![ci](https://github.com/Tradoooo/toile/actions/workflows/ci.yml/badge.svg)](https://github.com/Tradoooo/toile/actions/workflows/ci.yml)
[![ci-budget](https://github.com/Tradoooo/toile/actions/workflows/ci-budget.yml/badge.svg)](https://github.com/Tradoooo/toile/actions/workflows/ci-budget.yml)

---

## Le problème

Une VM QNX démarre sous QEMU. Le noyau monte, les services partent… et **il n'y a
pas d'image**. Console série uniquement.

La raison est simple : Screen, le serveur graphique de QNX, a besoin d'un pilote
d'affichage, et QNX ne fournit **ni périphérique virtuel virtio-gpu côté
hyperviseur, ni display driver Screen pour un GPU virtuel en invité**. Résultat :
dans une VM, Screen est inutilisable **sans passthrough matériel** — et le
passthrough est lourd, lié à l'hyperviseur, non portable, et exige une carte
dédiée. C'est le trou le plus cité de tout notre inventaire (`G3`, voir
[`docs/GAP.md`](docs/GAP.md)).

Tous ceux qui développent, enseignent, testent ou valident une IHM QNX sont
concernés : pas de graphique en VM, donc pas d'itération rapide, pas de CI
graphique, pas de TP, pas de validation sur poste standard.

## La promesse

> Un développeur QNX démarre une VM sur son portable et voit un **bureau QNX
> complet** s'afficher dans une fenêtre — sans carte graphique dédiée, sans
> passthrough, sans seconde machine.

Le maillon manquant n'est pas énorme : QEMU fournit déjà le périphérique
`virtio-gpu-pci`, et le transport virtio pour QNX 8.0 est déjà écrit dans le
projet frère **Passerelle**. Toile n'écrit que ce qui n'existe nulle part : la
couche virtio-gpu invité et le display driver Screen au-dessus.

## Les trois niveaux d'ambition

- **S — socle** (livrable au 15/12/2026) : virtio-gpu 2D minimal, scanout unique,
  flush, Screen basique en invité QEMU, cœur portable testé sans cible.
- **C — complet** (3 mois) : multi-scanout, curseur matériel, suivi de dommages,
  double tampon + vsync, virtio-input clavier/souris, intégration Screen complète.
- **X — extrême** (documenté) : vdev virtio-gpu côté hôte QNX Hypervisor,
  accélération 2D côté hôte, piste 3D virgl/venus.

## Démarrage rapide

Tout se joue sur un PC : le cœur du projet est un **modèle exécutable** de la
machine à commandes virtio-gpu, testable **sans QNX, sans QEMU et sans GPU**.

```bash
# 1. Compiler le cœur C11 et la CLI de référence (gcc -std=c11 -O2 -Werror)
make build

# 2. Toute la preuve portable : tests C, tests Python, différentiel C↔Python, gardes
make test                                  # vert SANS QNX, SANS QEMU, SANS GPU (S9)

# 3. Rejouer une séquence de commandes virtio-gpu (boot graphique d'un scanout)
./build/toile-run run fixtures/boot-graphique.tcl
./build/toile-run run fixtures/boot-graphique.tcl --format json
./build/toile-run run fixtures/boot-graphique.tcl --format md

# 4. Le jumeau Python doit produire exactement les mêmes octets
cd host && python3 -m toile.cli run ../fixtures/boot-graphique.tcl && cd ..

# 5. Octets fil réels (encodeur/décodeur virtio), oracles, auto-test
./build/toile-run wire fixtures/wire/boot-graphique.hex
./build/toile-run golden fixtures/boot-graphique.tcl --format text
./build/toile-run selftest
```

Codes de sortie (`CONTRACT.md` §11) : `0` aucune erreur, `1` au moins un verdict
de sévérité `error`, `2` usage ou fixture invalide, `3` divergence avec
`--expect`. Cibles disponibles : `build`, `test-c`, `test-py`, `diff`, `golden`,
`guards`, `test`, `qemu-demo`, `clean` (`CONTRACT.md` §12).

> **Honnêteté sur la commande n°1.** Au 2026-11-12 le `Makefile` et les fixtures
> ne sont pas encore dans l'arbre : ils arrivent avec les lots L1–L3. Ce qui est
> écrit et vérifiable aujourd'hui est listé dans la section suivante.

## Arborescence du dépôt

```
toile/
├── README.md                    # ce fichier
├── CONTRACT.md                  # contrat GELÉ : commandes, états, verdicts, formats, CLI, Makefile, CI
├── TOILE-cahier-des-charges.md  # cahier des charges v0.1 (contexte, exigences S/C/X, essais E1–E5)
├── LICENSE  NOTICE              # MIT ; attributions Apache-2.0 (transport) et BSD (structures virtio-gpu)
├── Makefile                     # cibles gelées par CONTRACT.md §12 (lot L1)
├── core/                        # cœur portable C11 : ni fichier, ni horloge, ni socket, ni QNX, aucun malloc
│   ├── include/toile/           #   API gelée : vgproto.h resource.h scanout.h cmdproc.h verdict.h report.h
│   ├── src/                     #   machine à commandes, invariants, rapports, hachage d'état
│   ├── cli/                     #   toile-run : CLI de référence du différentiel (décision D10)
│   └── tests/                   #   tests unitaires C
├── host/                        # jumeau Python (stdlib seule) : seconde implémentation indépendante
│   ├── toile/                   #   vgproto.py resource.py cmdproc.py replay.py report.py cli.py
│   └── tests/                   #   tests unitaires Python
├── tests/                       # différentiel C↔Python octet pour octet (differential.py)
├── fixtures/                    # séquences de référence : *.tcl (DSL), wire/*.hex (octets), golden/*
├── scenarios/                   # cas d'usage : boot graphique, resize, multi-ressource, multi-scanout
├── guest/                       # code QNX invité (compilé avec qcc)
│   ├── virtio/                  #   adaptation du transport Passerelle : controlq/cursorq, type 16, DID 0x1050
│   ├── gpu/                     #   pilote virtio-gpu : resources, scanout, flush, resmgr /dev/gpu0
│   ├── display/                 #   display driver Screen au-dessus du pilote
│   └── input/                   #   virtio-input clavier/souris (niveau C)
├── host-qnx/                    # volet B (niveau X) : vdev virtio-gpu côté hôte QNX Hypervisor
├── qemu/                        # scripts de lancement QEMU du volet A (-device virtio-gpu-pci)
├── image/                       # buildfile mkifs d'une image invitée graphique
├── codelab/                     # « donner un affichage à une VM QNX » (4 chapitres)
├── docs/                        # DESIGN, GAP, DECISIONS, VALIDATION, VIRTIO-GPU (+ TRIALS-E1-E4, autre lot)
└── .github/                     # CI (budget ≤ 5 min/job), gardes checks/taille.py + checks/api-frozen.py
```

La documentation technique se lit dans cet ordre :
[`docs/DESIGN.md`](docs/DESIGN.md) (architecture 3 couches et règles de
portabilité), [`docs/VIRTIO-GPU.md`](docs/VIRTIO-GPU.md) (le protocole pour un
débutant), [`docs/GAP.md`](docs/GAP.md) (les trous visés et leur statut),
[`docs/DECISIONS.md`](docs/DECISIONS.md) (D1–D16),
[`docs/VALIDATION.md`](docs/VALIDATION.md) (la méthode de preuve).

## État d'avancement (honnête)

Statut au **2026-11-12**. Le socle S est **en cours** ; rien n'est annoncé comme
prouvé sur cible.

| Ce qui est en place | Preuve |
|---|---|
| `CONTRACT.md` gelé (§1–§13) | fichier présent ; toute modification exige une PR touchant le contrat **et** les deux implémentations |
| API publique C (`core/include/toile/*.h`) | écrite et conforme à `CONTRACT.md` §10 |
| Adaptation du transport Passerelle (`guest/virtio/`) | `toile_transport.{h,c}` + `toile-virtio-gpu.patch` écrits ; **non compilés sur cible QNX** (essai E3) |
| Corrections du protocole | en-tête standard `virtio_gpu.h` relu le 2026-11-12 : `TRANSFER_FROM_HOST_2D` et `SET_CURSOR` n'existent pas, `MAX_SCANOUTS` = 16 confirmé (`CONTRACT.md` §9) |

| Ce qui reste à faire | Qui |
|---|---|
| Cœur C11 complet, CLI, jumeau Python, fixtures, différentiel, `Makefile`, gardes CI | lots L1–L3 (en cours d'écriture) |
| Pilote invité, display driver Screen, image `mkifs`, démo QEMU | lots L5–L7, **bloqués par E1 et E2** |
| Validation contre QEMU réel (capture/rejeu) | lot L10, exigence C8 |

**Essais non tranchés sur cible** : **E1** (un invité QNX 8.0 énumère-t-il le
`virtio-gpu-pci` sur son bus PCI ?), **E2** (Screen accepte-t-il un display
driver tiers, et quel est son contrat exact ?), **E4** (le DDK hyperviseur est-il
accessible en licence non commerciale ? — ne bloque que le volet B). **E5**
(performances d'un flush 2D, ≥ 15–20 images/s en 1080p) n'est pas mesuré. Aucun
de ces cinq essais n'exige de matériel propriétaire, mais tous exigent un SDP QNX
installé et une VM : ils ne sont donc pas prouvés par ce dépôt. Détail et
registre complet des statuts : [`docs/GAP.md`](docs/GAP.md) §4–§5.

## Projet frère : Passerelle

Le transport virtio (PCI moderne MSI-X, virtio-MMIO v1/v2, files virtuelles,
négociation de fonctionnalités) **n'est pas réécrit** : il est repris du projet
frère [**Passerelle**](https://github.com/BaptisteFaisy/passerelle) (même
auteur, licence **Apache-2.0**), qui l'a écrit et validé pour un partage de
système de fichiers hôte/invité. Toile ajoute l'identité virtio-gpu (type de
périphérique 16, DID moderne `0x1050`), les files nommées `controlq`/`cursorq`,
un pool de requêtes à tampons DMA, et un patch minimal qui étend la découverte
PCI/MMIO de Passerelle au type GPU — patch appliqué à une **copie de build**,
jamais à l'arbre partagé, et candidat à contribution amont. Attribution complète
dans [`NOTICE`](NOTICE) ; décision D2 ; essai E3.

Passerelle partage des fichiers de système de fichiers ; Toile partage des
pixels. Périphériques, domaines et démonstrations distincts, transport commun
cité explicitement.

## Licence

**MIT** — voir [`LICENSE`](LICENSE). Les composants tiers et le prior art sont
listés dans [`NOTICE`](NOTICE) : transport virtio repris de Passerelle
(Apache-2.0, en-têtes SPDX conservés sur les fichiers dérivés) et structures
virtio-gpu dérivées des en-têtes standard `virtio_gpu.h` (licence BSD
3-clauses simplifiée). Le protocole virtio est un standard ouvert **OASIS** ;
aucune marque de tiers n'est utilisée dans les supports de soumission.
