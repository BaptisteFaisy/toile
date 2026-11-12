# Chapitre 1 — Un affichage pour une VM QNX : le problème et la promesse

**Durée estimée : ~30 min** (dont ~10 min de commandes, le reste en lecture)

## Objectif

Comprendre pourquoi un serveur graphique QNX (**Screen**) ne peut pas
s'afficher dans une VM sans passthrough matériel, découvrir le périphérique
virtuel standard qui lève ce blocage (**virtio-gpu**), constater ce que QEMU
fournit déjà côté hôte et ce qui manque côté invité QNX. À la fin du chapitre,
vous avez lancé une VM avec le périphérique virtio-gpu et vous savez exactement
ce que les chapitres 2 à 4 vont construire.

## Prérequis

- Un terminal Linux et QEMU installé (paquet `qemu-system-x86_64` de votre
  distribution). Vérifié ici avec QEMU 8.2.2 ; aucune version précise exigée.
- Pour l'étape 6 seulement : une ISO d'installation QNX 8.0 (SDP obtenu via le
  programme QNX Everywhere, gratuit en usage non commercial). Cette étape est
  **à valider sur cible** (essai E1 non tranché à la date d'écriture).
- Aucune carte graphique dédiée : le périphérique est **virtuel**, émulé par
  QEMU.

---

## Étape 1 — Le problème : Screen est muet dans une VM

Screen est le serveur graphique de QNX : c'est lui qui affiche les fenêtres des
applications (cockpits, clusters, IHM médicales…). Pour dessiner quelque chose,
Screen s'appuie sur un **display driver**, qui parle à son tour à un pilote de
périphérique graphique.

Dans une VM, ce dernier maillon casse :

1. Le seul « GPU » visible par l'invité est un périphérique **virtuel**
   (émulé par l'hyperviseur), pas une carte physique.
2. QNX 8.0 ne livre **aucun pilote invité** pour le périphérique graphique
   virtuel standard virtio-gpu, et **aucun display driver Screen** ne sait
   lui parler.
3. Conséquence — c'est le trou **G3** du cahier des charges (§2.2), le plus
   cité de l'inventaire : **Screen est inutilisable en invité sans
   passthrough matériel**. Sans passthrough, la VM QNX n'a qu'une **console
   série** : du texte, jamais un pixel.

Le passthrough (donner une carte physique à la VM) existe, mais il est lourd,
lié à un hyperviseur précis, non portable — et il exige… une carte graphique
dédiée. C'est précisément ce dont un développeur sur portable, un enseignant
en salle de TP ou une ferme de CI ne disposent pas. Les trous **G1** (virtio-gpu
absent des vdev de QNX Hypervisor) et **G2** (aucun display driver Screen pour
virtio-gpu) complètent le tableau.

> Honnêteté : G1–G6 sont des affirmations d'**absence** dans la documentation
> publique et l'inventaire du projet ; elles doivent être revérifiées page à
> page dans le SDP installé (cahier des charges §13). Voir `docs/GAP.md`.

## Étape 2 — La promesse : virtio-gpu, le périphérique virtuel standard

**virtio-gpu** est le périphérique graphique **virtuel** défini par le standard
ouvert virtio (OASIS, virtio 1.x, section « GPU Device »). C'est celui qu'utilisent
Linux et les BSD en VM : l'hôte l'émule, l'invité n'a besoin d'aucun matériel.

Anatomie minimale, telle qu'utilisée par Toile :

- **Deux files virtuelles** (virtqueues) :
  - `controlq` (file 0) : toutes les commandes 2D — création de ressources,
    scanout, transferts, flush ;
  - `cursorq` (file 1) : le curseur matériel (niveau C du projet).
- **Des ressources** : surfaces identifiées par un `resource_id`, auxquelles
  l'invité attache ses propres pages mémoire (*backing storage*).
- **Des scanouts** : affectation « ressource → tête virtuelle », avec position
  et taille.

Le cycle de vie d'une ressource, en cinq temps :

```
create ──► attach backing ──► set scanout ──► transfer ──► flush
 (l'invité   (l'invité donne    (la ressource   (copie des   (l'hôte
  déclare     ses pages au       est affectée à   pixels vers  affiche)
  la surface) périphérique)      une tête)        l'hôte)
```

Côté fil, ce sont les commandes `RESOURCE_CREATE_2D`,
`RESOURCE_ATTACH_BACKING`, `SET_SCANOUT`, `TRANSFER_TO_HOST_2D`,
`RESOURCE_FLUSH` — exactement le sous-ensemble gelé dans `CONTRACT.md` §3.

**Insight clé du projet** : ce cycle de vie est une **logique pure**. On peut
l'écrire en C11 portable et la tester sur PC, sans QNX ni QEMU ni GPU, en
rejouant des séquences de commandes. C'est le chapitre 2.

## Étape 3 — Ce que QEMU fournit déjà côté hôte

QEMU émule le périphérique virtio-gpu : `-device virtio-gpu-pci` (variante
PCI) ou `-device virtio-gpu-device` (variante MMIO). La moitié hôte du volet A
**existe déjà** — Toile ne l'écrit pas.

Vérifiez que votre QEMU expose bien le périphérique :

```bash
$ qemu-system-x86_64 -device help | grep virtio-gpu
```

Sortie *(vérifié sur la machine de l'auteur, QEMU 8.2.2)* :

```
name "virtio-gpu-device", bus virtio-bus
name "virtio-gpu-gl-device", bus virtio-bus
name "virtio-gpu-gl-pci", bus PCI, alias "virtio-gpu-gl"
name "virtio-gpu-pci", bus PCI, alias "virtio-gpu"
```

Observez maintenant le périphérique **tel que l'invité le verra sur son bus
PCI**, sans même démarrer de système invité :

```bash
$ printf 'info pci\nquit\n' | qemu-system-x86_64 -display none \
    -monitor stdio -nodefaults -device virtio-gpu-pci
```

Sortie, extraite *(vérifié sur la machine de l'auteur)* :

```
  Bus  0, device   2, function 0:
    Display controller: PCI device 1af4:1050
      PCI subsystem 1af4:1100
      IRQ 0, pin A
      BAR1: 32 bit memory at 0xffffffffffffffff [0x00000ffe].
      BAR4: 64 bit prefetchable memory at 0xffffffffffffffff [0x00003ffe].
```

Ce qu'on apprend : identifiant PCI **1af4:1050** (virtio moderne : 0x1040 +
type de périphérique 16 = GPU), deux BAR mémoire, une interruption. C'est
exactement ce que le pilote invité du chapitre 3 devra énumérer et configurer.

## Étape 4 — Ce qui manque côté invité QNX

| Côté | Fourni par | État |
|---|---|---|
| Hôte : émulation du périphérique virtio-gpu | QEMU (`-device virtio-gpu-pci`) | **existe déjà** |
| Invité : pilote virtio-gpu (énumération PCI, files, commandes) | — | **à écrire** : c'est Toile (chapitre 3) |
| Invité : display driver Screen au-dessus du pilote | — | **à écrire** : c'est Toile (chapitre 4) |

Le transport virtio lui-même (négociation PCI moderne, MSI-X, files virtuelles)
n'est pas réécrit : il est **adapté** du projet frère Passerelle (même auteur,
Apache-2.0), qui l'a déjà validé pour QNX 8.0. Voir `guest/virtio/` et l'essai
E3 (cahier des charges §10).

## Étape 5 — Les deux volets, et pourquoi on commence par A

| | Volet A — QEMU hôte | Volet B — QNX Hypervisor hôte |
|---|---|---|
| Périphérique côté hôte | Fourni par QEMU | **À écrire** (vdev virtio-gpu — absent des ~20 vdev livrés, trou G1) |
| Pilote côté invité QNX | À écrire (Toile) | À écrire (le même) |
| Outillage requis | QEMU, gratuit | DDK hyperviseur QNX |
| Risque bloquant | E1, E2 | E1, E2, **E4** (accès au DDK hyperviseur en licence non commerciale : **non vérifié**) |
| Ambition projet | **Socle S, jalon 15/12** | Niveau X, hors concours |

On commence par A parce que la moitié hôte existe déjà : le seul code à écrire
est côté invité, et aucun accès DDK hyperviseur n'est nécessaire. Tout ce qui
est appris en A (pilote invité, display driver, cœur portable) se réutilise
tel quel en B.

## Étape 6 — Lancer une VM QNX avec virtio-gpu, et constater le silence

> **⚠ À valider sur cible (essai E1).** La commande ci-dessous n'a pas encore
> été exécutée avec une vraie image QNX 8.0 : E1 — « un invité QNX sous QEMU
> énumère-t-il le périphérique virtio-gpu-pci ? » — est l'essai critique du
> volet A, non tranché à la date d'écriture. Le journal des essais vit dans
> `docs/TRIALS-E1-E4.md`. Le script encapsulé `qemu/run-toile.sh` (lot
> « scripts QEMU ») et la sonde `qemu/probe-e1.sh` couvriront cette étape.

Avec une ISO d'installation QNX 8.0 (répertoire `~/qnx800/` par convention) :

```bash
$ qemu-system-x86_64 \
    -m 4G -smp 4 \
    -display gtk \
    -serial mon:stdio \
    -device virtio-gpu-pci \
    -cdrom ~/qnx800/qnx800-install.iso \
    -boot d
```

**Ce qu'on attend à ce stade du projet** (l'observation exacte sera consignée
dans `docs/TRIALS-E1-E4.md`) :

- la fenêtre QEMU s'ouvre mais **reste noire** : aucun invité ne pilote le
  périphérique virtio-gpu ;
- la vie de la VM passe entièrement par la **console série** redirigée sur
  votre terminal (`-serial mon:stdio`) : c'est le trou G3 en image ;
- question ouverte E1 : le noyau QNX **voit-il** seulement le périphérique sur
  son bus PCI ? C'est vérifiable côté invité avec l'outil `pci` du SDP ou
  `libpci` — la procédure détaillée est au chapitre 3, étape 2.

C'est précisément ce contraste que la vidéo de démo montrera (cahier des
charges §9, séquence 0:00–0:25) : la VM démarre, rien ne s'affiche. Puis,
avec Toile : le bureau QNX apparaît dans la fenêtre.

## Étape 7 — Ce qu'on va construire, chapitre par chapitre

1. **Chapitre 2 — le cœur portable.** La machine à commandes virtio-gpu en
   C11 pur (`core/`) et son jumeau Python indépendant (`host/`), testés par
   fixtures DSL sur un poste ordinaire. Preuve que la logique du protocole est
   juste **avant** d'écrire la moindre ligne de pilote.
2. **Chapitre 3 — le pilote invité.** Énumération PCI du 1af4:1050, transport
   virtio Passerelle adapté, files `controlq`/`cursorq`, émission des commandes
   du contrat, resource manager `/dev/gpu0`, compilation `qcc`, essai E1.
3. **Chapitre 4 — Screen et la démo.** Le display driver (essai E2), la mire
   statique puis le bureau, l'image invitée `mkifs`, les scripts QEMU, les
   performances (essai E5) et la vidéo de soumission.

## Exercices

Tous les exercices ci-dessous tournent **sans QNX** — hôte Linux seul.

1. **Variantes du périphérique.** Listez les quatre variantes vues à l'étape 3
   et expliquez la différence entre `virtio-gpu-pci` (bus PCI) et
   `virtio-gpu-device` (bus virtio-bus, transport MMIO). Laquelle correspond à
   `-machine virt` ? À quel transport le patch `guest/virtio/toile-virtio-gpu.patch`
   étend-il la découverte Passerelle ? *(Réponse : aux deux — PCI moderne et MMIO.)*
2. **Arborescence QEMU.** Remplacez `info pci` par `info qtree` dans la sonde
   de l'étape 3 et retrouvez le périphérique virtio-gpu et ses propriétés
   (`hostmem`, `max_hostmem`…). Comparez avec `-device virtio-gpu-pci,max_hostmem=64M`.
3. **Sans accélération hôte.** Relancez la sonde avec `-display none` puis
   avec `-display gtk` : dans le second cas, QEMU ouvre une fenêtre noire —
   le périphérique est émulé, mais personne ne lui parle. C'est l'état « avant
   Toile » côté invité.
4. **Lecture du standard.** Dans la spécification OASIS virtio 1.x, section
   « GPU Device », retrouvez : l'identifiant de type de périphérique (16), les
   files `controlq`/`cursorq`, et la liste des commandes 2D. Comparez avec la
   table gelée `CONTRACT.md` §3 : quelles commandes du standard sont **hors**
   socle S (indice : blob, 3D) ?

## Points de contrôle

Vous pouvez passer au chapitre 2 quand vous savez répondre à :

- [ ] Pourquoi Screen ne peut-il pas s'afficher dans une VM QNX aujourd'hui ? (G3)
- [ ] Quel identifiant PCI porte le périphérique virtio-gpu moderne, et d'où vient le 1050 ? (0x1040 + type 16)
- [ ] Quelles sont les cinq étapes du cycle de vie d'une ressource virtio-gpu ?
- [ ] Quelle moitié du travail QEMU fournit-il déjà, et pour quel volet (A ou B) ?
- [ ] Quels essais bloquent le volet A, et lesquels ne bloquent que le volet B ? (E1/E2 bloquent A ; E4 ne bloque que B)

## Erreurs fréquentes

| Symptôme | Cause probable | Remède |
|---|---|---|
| `qemu-system-x86_64: -device virtio-gpu-pci: unknown device` | QEMU très ancien ou binaire amputé | Installez le paquet complet de votre distribution ; vérifiez avec `-device help` |
| Fenêtre QEMU noire immédiate… et c'est tout | Normal **à ce stade** : aucun invité ne pilote le périphérique | C'est l'objet des chapitres 3–4 |
| `-monitor stdio` avale vos touches / invite `(qemu)` confuse | Le moniteur partage stdio avec le shell | Tapez `quit` dans l'invite `(qemu)` ; ou utilisez `printf '…\nquit\n' \|` comme à l'étape 3 |
| La VM QNX ne démarre pas sur l'ISO | Étape 6 **non validée** : image, `-boot d` et tailles mémoire restent à trancher avec E1 | Suivez `docs/TRIALS-E1-E4.md` et `qemu/run-toile.sh` dès leur livraison |
| Chercher une option « virtio-gpu » dans le SDP QNX | G5 : aucune virtualisation GPU n'est documentée publiquement pour QNX 8.0 | C'est le vide que Toile comble ; ne pas chercher, écrire |
