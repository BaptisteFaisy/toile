# TRIALS — E5 : débit d'un flush 2D virtio-gpu

**Date : 2026-11-12. Machine : PC Linux, gcc 14.2, SDP QNX 8.0 présent.**
Fichier de vérité associé : `tools/bench-model.c` (banc reproductible : `make bench`).

Règle de lecture, appliquée partout :

- **[MESURÉ]** = mesuré sur cette machine, commande et sortie brute ci-dessous ;
- **[MODÈLE]** = calcul sous hypothèses explicites ;
- **[NON MESURÉ]** = hors de portée ici, avec ce qu'il faudrait pour le mesurer.

---

## Question

Les performances d'un flush 2D via virtio-gpu (copie du framebuffer vers
l'hôte) sont-elles suffisantes pour un bureau utilisable (**≥ 15–20 images/s
en 1080p**) ?

## Verdict

**NON TRANCHÉ sur la cible.** Le cœur portable est **très loin** d'être le
goulot d'étranglement ([MESURÉ] ci-dessous, marge de ≈ 31 000× sur le besoin
calculé), mais le **débit réel invité → hôte n'a pas été mesuré** : le pilote
n'est pas encore câblé jusqu'à l'affichage dans l'invité. Ce qui reste à faire
est décrit en fin de document.

---

## 1. [MESURÉ] — le débit du cœur portable

Commande :

```bash
make bench      # compile tools/bench-model.c avec core/src/*.c et l'exécute
```

Sortie brute (extrait, 20 000 itérations par scénario, meilleure de 5 passes) :

```
banc de débit du coeur portable (essai E5)
surface                 : 1920x1080, 4 octets/pixel
octets par image        : 8294400 (8.29 Mo)

  [A] cycle de vie complet (create+attach+scanout+xfer+flush)
    commandes/s   : 2736558
    Mo/s pixels   : 4539621.95
    marge du modèle [A]         : 31763.1x

  [B] boucle d'affichage (xfer surface pleine + flush)
    commandes/s   : 1445280
    Mo/s pixels   : 5993414.77

  [D] référence hôte : memcpy de 8.29 Mo (hors virtio-gpu)
    Mo/s memcpy hôte           : 4994.89

empreinte d'état du modèle (P3) : 377f49d31d2eb12f
NOTE : le modèle ne copie aucun pixel ; il ne fait que
      COMPTER les octets (invariant I6). Les débits ci-dessus
      sont des débits de COMPTABILISATION, pas des débits DMA.
```

Lecture honnête : le modèle **compte** les octets, il ne les déplace pas. Sa
marge de 31 763× dit seulement que **le cœur portable ne sera jamais le
limiteur** de l'affichage — pas que l'affichage est rapide.

## 2. [MODÈLE] — le besoin en bande passante

Hypothèses explicites : 1920×1080 pixels, 4 octets par pixel (tous les formats
retenus au §3 du contrat), une image complète par rafraîchissement.

| Élément | Calcul | Valeur |
|---|---|---|
| Octets par image | 1920 × 1080 × 4 | **8,29 Mo** |
| Besoin à 20 img/s | 8,29 Mo × 20 | **≈ 166 Mo/s** |
| Besoin à 15 img/s | 8,29 Mo × 15 | **≈ 124 Mo/s** |
| Marge du modèle (mesurée) | 4 539 621 Mo/s ÷ 165,89 Mo/s | **≈ 27 000×** |

La référence hors virtio (`[D]`, `memcpy` de 8,29 Mo sur cette machine)
donne ≈ 5 000 Mo/s, soit **≈ 30× le besoin E5**. C'est un ordre de grandeur
**local**, pas une mesure du chemin virtio-gpu : il indique seulement que le
goulot d'étranglement potentiel est le **transport**, pas la mémoire de
l'invité.

Ordres de grandeur publics (à re-vérifier sur cible, non mesurés ici) : un
lien PCIe Gen3 x16 offre plusieurs Go/s ; les périphériques virtio de QEMU
atteignent couramment l'ordre du Go/s sur un hôte récent. **Ces chiffres sont
des repères, pas des mesures** : aucune publication n'a été citée ici, et la
seule source de vérité du projet reste la mesure sur la cible.

## 3. [NON MESURÉ] — ce qui manque, et comment le mesurer

Non mesuré sur cette machine, faute de pilote câblé jusqu'à l'affichage :

- le **débit réel invité → hôte** (transfert DMA via la file `controlq`) ;
- la **latence d'un flush** (aller-retour commande → acquittement) ;
- le comportement en **double tampon / vsync** (niveau C) ;
- l'effet du **partage de framebuffer** entre `/dev/gpu0` et l'invité.

Procédure de mesure sur cible (à exécuter dans l'invité) :

1. compteur de flush par seconde dans le pilote (`/dev/gpu0`), lu via un
   ioctl de statistiques — horodatage **côté invité** (`ClockCycles()`) ;
2. une séquence de N flush de la surface 1080p, sans synchronisation ;
3. relevé de `N / durée` puis comparaison au besoin de 166 Mo/s ;
4. commanded par l'appli de démonstration (le bureau réel), pour capter le
   coût de la composition et non seulement du transport.

## 4. Effet sur le cahier des charges

E5 ne bloque rien du socle S : le cahier des charges le classe « non
bloquant (ajuste les ambitions C) ». Il conditionne en revanche le **niveau C**
(suivi de dommages C3, double tampon C4) : si le débit réel mesuré sur cible
est Marginal, c'est le *damage tracking* qui devient la priorité, pas
l'implémentation du double tampon.

**Statut : NON TRANCHÉ — à mesurer sur la cible, procédure écrite ci-dessus.**
