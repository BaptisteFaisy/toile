# Codelab — Donner un affichage à une VM QNX

Quatre chapitres pratiques pour comprendre et reproduire **Toile** : un pilote
virtio-gpu 2D pour QNX 8.0 en invité (VM QEMU) et le display driver Screen qui
s'appuie dessus. Ce codelab matérialise l'exigence **C9** du cahier des charges
(§6.2) : à la fin, une VM QNX démarre sur un poste ordinaire et affiche un
bureau dans une fenêtre QEMU — sans passthrough matériel, sans carte graphique
dédiée, sans seconde machine.

## Pour qui ?

| Profil | Ce qu'il y gagne |
|---|---|
| **Développeur QNX sans matériel cible** | Comprendre pourquoi Screen ne s'affiche pas en VM aujourd'hui (trou G3), et disposer d'un chemin concret pour développer et déboguer une IHM sur son portable |
| **Enseignant / formateur embarqué** | Un TP complet : du protocole virtio-gpu (standard OASIS) au serveur graphique QNX, avec un cœur portable testable en salle de cours sur n'importe quel poste |
| **Ingénieur test / CI** | La méthode « cœur portable + jumeau Python » : valider la logique d'un pilote graphique **sans** QNX, **sans** QEMU, **sans** GPU, donc dans n'importe quelle ferme de CI |

Aucune expérience préalable de virtio n'est demandée. Savoir lancer une
commande dans un terminal Linux suffit pour les chapitres 1 et 2 ; les
chapitres 3 et 4 supposent en plus les bases du développement QNX (SDP, `qcc`).

## Ce qu'on construit

1. **Un modèle portable du protocole virtio-gpu 2D** (C11 pur + jumeau Python),
   testé par fixtures sur PC — c'est le cœur de Toile, et le chapitre 2 du
   codelab.
2. **Un pilote invité QNX** qui énumère le périphérique `virtio-gpu-pci`,
   réutilise le transport virtio du projet frère Passerelle (adapté, non
   réécrit — Apache-2.0), et expose `/dev/gpu0` — chapitre 3.
3. **Un display driver Screen** au-dessus du pilote, une image invitée `mkifs`
   et les scripts QEMU qui affichent le bureau — chapitre 4.

## Les 4 chapitres

| # | Fichier | Contenu | Durée estimée | Prérequis |
|---|---|---|---|---|
| 1 | [`01-un-affichage-pour-une-vm-qnx.md`](01-un-affichage-pour-une-vm-qnx.md) | Le problème (Screen inutilisable en VM), virtio-gpu, ce que QEMU fournit déjà, ce qui manque côté QNX, les volets A/B | ~30 min | Terminal Linux, QEMU installé |
| 2 | [`02-le-coeur-portable.md`](02-le-coeur-portable.md) | Le cœur C11 et son jumeau Python : fixtures DSL, verdicts, formats de sortie, hash d'état, différentiel, gardes | ~1 h | `gcc`, `python3`, `make` — **c'est tout** |
| 3 | [`03-le-pilote-invite.md`](03-le-pilote-invite.md) | Le pilote virtio-gpu côté invité QNX : PCI, files `controlq`/`cursorq`, resource manager, `qcc`, essai E1 | ~1 h 30 | SDP QNX 8.0, QEMU, chapitres 1–2 |
| 4 | [`04-screen-et-la-demo.md`](04-screen-et-la-demo.md) | Le display driver Screen, la mire puis le bureau, l'image `mkifs`, la démo et la vidéo de soumission | ~1 h 30 | Chapitre 3, SDP QNX 8.0, QEMU |

Chaque chapitre suit la même structure : **objectif → prérequis → étapes
numérotées avec commandes exactes → ce qu'on observe → exercices → points de
contrôle → erreurs fréquentes**.

## Matériel et logiciel nécessaires

- **Un poste Linux avec QEMU** (testé ici avec QEMU 8.2.2 ; toute version
  récente exposing `virtio-gpu-pci` convient — vérification au chapitre 1).
- **Le SDP QNX 8.0**, gratuit en usage non commercial (programme QNX
  Everywhere, licence obtenue en ~1 h) — uniquement pour les chapitres 3–4.
- **`gcc` (C11), `python3` (stdlib), `make`** — uniquement pour le chapitre 2.
- **Aucune carte graphique dédiée n'est nécessaire.** C'est tout l'intérêt du
  projet : l'affichage passe par le périphérique **virtuel** standard
  virtio-gpu, émulé par QEMU sur l'hôte. Le poste peut même n'avoir aucun
  accélérateur graphique (CI, serveur sans écran).

## État d'avancement — avertissement honnête

Ce codelab accompagne un projet **en cours** (planning §11 du cahier des
charges, 13 semaines). L'état réel, sans enrobage :

- **Chapitres 1–2 : exécutables partout.** Le cœur portable se teste sans QNX,
  sans QEMU et sans GPU : un poste Linux ordinaire suffit. Les commandes du
  chapitre 2 sont celles du contrat **gelé** (`CONTRACT.md` §11–12) ; le code
  correspondant (`core/`, `host/`, `Makefile`, `tests/`) est livré en semaines
  S1–S2 du planning — tant qu'un binaire manque, l'étape est marquée comme
  telle dans le texte. Les observations côté QEMU du chapitre 1 ont été
  vérifiées sur la machine de l'auteur (QEMU 8.2.2).
- **Chapitres 3–4 : dépendent des essais E1/E2, non encore tranchés sur
  cible.** E1 (un invité QNX sous QEMU énumère-t-il le périphérique
  `virtio-gpu-pci` ?) et E2 (Screen accepte-t-il un display driver tiers via
  son interface documentée ?) sont les deux essais critiques du volet A
  (cahier des charges §10). Chaque étape qui n'a pas encore été exécutée sur
  cible réelle porte la mention explicite **« à valider sur cible »**, avec
  renvoi vers `docs/TRIALS-E1-E4.md` (le journal des essais, tenu à jour par
  le lot « code invité »).

Rien dans ce codelab ne présente une commande comme testée si elle ne l'a pas
été. Le doute est écrit, pas caché.

## Conventions de lecture

| Marqueur | Sens |
|---|---|
| *(vérifié sur la machine de l'auteur)* | Commande exécutée réellement, sortie reproduite fidèlement |
| *(à valider sur cible)* | Commande écrite d'après le contrat ou la documentation, **jamais exécutée** sur QNX/QEMU avec l'invité QNX |
| `$` | Invite d'un terminal hôte Linux (utilisateur ordinaire) |
| `#` | Invite d'un terminal **invité QNX** (shell de la VM) |

## Où aller ensuite

- `CONTRACT.md` — le contrat gelé : commandes virtio-gpu acceptées, DSL des
  fixtures, verdicts, formats de sortie, CLI, cibles Makefile.
- `TOILE-cahier-des-charges.md` — le cahier des charges complet (périmètre,
  exigences S/C/X, essais E1–E5, planning).
- `docs/` — DESIGN, GAP, DECISIONS, VALIDATION, VIRTIO-GPU et le journal des
  essais `TRIALS-E1-E4.md`.
