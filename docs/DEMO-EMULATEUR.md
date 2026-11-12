# DEMO — la boucle invité → émulateur, preuve visuelle

**Date : 2026-11-12. Machine : PC Linux, SDP QNX 8.0 (`~/qnx800`), QEMU 8.2.2,
KVM.**

Cette page consigne la **preuve visuelle** que le pilote invité Toile peint
dans l'émulateur : l'invité QNX dessine une mire dans le framebuffer virtio-gpu,
et **QEMU l'affiche**.

## Méthode

1. Construire l'image IFS qui lance le pilote avec l'option `--mire`
   (peint la mire et la présente au démarrage, sans aucun client) :
   `source ~/qnx800/qnxsdp-env.sh` puis
   `make -f guest/common.mk driver` et `mkifs image/toile-driver.build /tmp/x.ifs`
   (le buildfile lance `toile-gpu --mire &`).
2. Démarrer QEMU avec le périphérique virtio-gpu **et** un affichage capturable
   (pas de `-display none`) :
   `qemu-system-x86_64 --enable-kvm --cpu host -m 1536 -kernel /tmp/x.ifs
   -vga none -device virtio-gpu-pci -vnc :11 -serial file:serial.log
   -monitor tcp:127.0.0.1:4566,server,nowait`
3. Attendre le marqueur `toile-gpu: mire painted and presented` sur la série.
4. Capturer l'affichage QEMU avec le moniteur : `screendump shot.ppm`, puis
   convertir en PNG (Pillow).

## Journal série (extrait)

```
toile-gpu: device up: 0 queues, msix=yes, features=0x100000000
toile-gpu: scanout 0 mode 1280x800 (enabled)
toile-gpu: framebuffer 1280x800 fmt=2 stride=5120 pa=0x5e003000
toile-gpu: scanout 0 <- resource 1 (1280x800)
toile-gpu: mire painted and presented
toile-gpu: 1280x800 fmt=2 scanouts=1 stride=5120 res=1 fb=4096000
toile-gpu: serving /dev/gpu0
TOILE: /dev/gpu0 pret
```

## Capture

![Mire Toile dans QEMU](../site/assets/toile-mire.png)

- Taille : **1280 × 800** (= le mode du scanout annoncé par `GET_DISPLAY_INFO`).
- Contenu : 6 blocs pleins (blanc, jaune, cyan, vert, magenta, bleu, rouge) +
  fond — la mire de test générée par `gpu_paint_mire()`.
- Analyse numérique : 263 couleurs distinctes (les barres + leurs bords
  crénelés), les 6 dominantes étant les couleurs primaires/secondaires pures.

Interprétation : ces pixels ont été écrits **dans l'invité**, au travers de la
ressource virtio-gpu (backing + `SET_SCANOUT`), et rendus **par l'hôte QEMU** —
la boucle « invité → émulateur » est fermée pour l'affichage.

## Limite (dite, pas cachée)

L'ouverture de `/dev/gpu0` par un *client* plante encore (SIGSEGV dans la
bibliothèque resmgr de QNX 8.0, avant tout handler Toile) — voir
`docs/TRIALS-E1-E6.md`. La preuve visuelle ci-dessus n'en dépend pas : elle
utilise le pilote seul (`--mire`). La relecture numérique (preuve A) exige, elle,
le chemin client.
