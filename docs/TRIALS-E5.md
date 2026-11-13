<!-- SPDX-License-Identifier: MIT -->
# Essai E5 — débit de peinture sur cible (invité QNX 8.0 réel)

La vitrine anime l'écran à 30 im/s, mais **30 im/s était le plafond choisi**, pas
la mesure de la machine. Cet essai mesure ce que le client painting atteint
réellement, dans l'invité, sur le même framebuffer que la vitrine.

## Ce qui est mesuré

Le client `guest/common/toile_anim.c` peint le plasma en CPU pur (aucun GPU 3D,
aucun shader) dans le framebuffer `virtio-gpu` de l'invité. En mode `--bench N`,
il peint **N images sans cadence**, puis publie sur la console série :

```
BENCH: 1280x800 frames=300 total_ms=874 paint_im_s=343.2 MB_s=1405.9 present_us=730 threads=1
```

| champ | signification |
|---|---|
| `paint_im_s` | images peintes par seconde (hors cadence) |
| `MB_s` | débit de pixels écrits (largeur × hauteur × 4 octets par image) |
| `present_us` | coût moyen d'un `present` (transfert 2D + flush), en microsecondes |
| `threads` | fils de peinture (bandes verticales du même framebuffer) |

## Protocole

```bash
source ~/qnx800/qnxsdp-env.sh        # SDP QNX 8.0 (poste de travail)
qemu/bench-e5.sh --smp 4             # construit l'image, démarre l'invité, affiche le tableau
qemu/bench-e5.sh --smp 1 --vnc 16    # même mesure avec un seul cœur invité
```

L'image `image/toile-bench.build` lance, dans le même invité, trois passes de
300 images : 1, puis 2, puis 4 fils de peinture. L'hôte est un Debian 13 avec
KVM (`-enable-kvm -cpu host`), QEMU 8.2.2, 1536 Mo de RAM.

## Mesures (2026-11-13, 1280×800, 300 images par passe)

### Invité à 4 cœurs (`--smp 4`)

| fils de peinture | images/s | Mo/s | `present` (µs) |
|---|---|---|---|
| 1 | 343,2 | 1 405,9 | 730 |
| 2 | 1 744,2 | 7 144,2 | 573 |
| 4 | 1 840,5 | 7 538,7 | 533 |

### Invité à 1 cœur (`--smp 1`)

| fils de peinture | images/s | Mo/s | `present` (µs) |
|---|---|---|---|
| 1 | 401,6 | 1 645,0 | 570 |
| 2 | 2 459,0 | 10 072,1 | 400 |
| 4 | 1 948,1 | 7 979,2 | 510 |

## Lecture

- **Le gain vient du parallélisme de peinture, pas du nombre de cœurs invité.**
  Passer de 1 à 2 fils de peinture multiplie le débit par **5,1** (smp 4) ou
  **6,1** (smp 1) : la peinture d'une bande est purement CPU et se répartit.
- **4 fils saturent** : à 4 cœurs invité, 4 fils ne gagnent que 5 % sur 2 fils
  (le coût `present` et la bande passante mémoire deviennent le goulot).
- **Un cœur invité avec 4 fils régresse** (1 948 im/s contre 2 459 avec 2
  fils) : la contention d'ordonnancement coûte plus qu'elle ne rapporte. Le
  réglage par défaut de la vitrine (`--smp 4`, 1 fil par client) est donc
  volontaire.
- **Le `present` coûte 0,4 à 0,7 ms** : à 30 im/s par client, trois clients
  coûtent ~90 présentations par seconde, soit ~4 % du temps d'un cœur. C'est
  pourquoi le coût du `present` ne se voit pas à 30 im/s mais compte dès qu'on
  vise 1 000 im/s.

## Ce que cet essai ne prouve pas

- Rien sur le chemin **3D** : le virglrenderer n'est pas disponible dans
  l'émulateur (QEMU 8.2.2 Debian), la série 3D n'est donc pas mesurable ici.
- Rien sur la **relecture hôte→invité** : le virtio-gpu 2D n'a pas de
  `TRANSFER_FROM_HOST_2D` (cf. [Preuve A](TRIALS-E1-E6.md)).
- La mesure porte sur le **scanout 1280×800** du pilote ; d'autres résolutions
  n'ont pas été mesurées.

## Reproduire

Tout est dans le dépôt : `image/toile-bench.build` (l'image), `qemu/bench-e5.sh`
(le protocole), `guest/common/toile_anim_main.c` (le mode `--bench`). La sortie
brute est dans le journal série de l'invité (`/tmp/toile-bench/bench-serial.log`
par défaut).
