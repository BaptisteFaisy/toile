# TRIALS — E1 à E6 — le pilote invité démarre-t-il dans un invité QNX 8.0 ?

**Date des vérifications : 2026-11-12. Machine : PC Linux, SDP QNX 8.0
installé dans `~/qnx800`, QEMU 8.2.2 (`/usr/bin/qemu-system-x86_64`),
KVM disponible.**

Règle de lecture de ce document, appliquée partout :

- **[MACHINE]** = vérifié **sur cette machine**, sortie brute ci-dessous ;
- **[VM]** = exige une VM QNX démarrée ; la procédure est écrite et
  reproductible (`qemu/probe-driver.sh`) ;
- **jamais** d'affirmation « ça marche » sans la sortie correspondante.

| Essai | Question | Verdict | Dossier |
|---|---|---|---|
| **E1** | L'invité QNX 8.0 énumère-t-il `virtio-gpu-pci` sur son bus PCI ? | **TRANCHÉ — POSITIF** | `TRIALS-E1-E4.md` §E1 |
| **E2** | Screen accepte-t-il un display driver tiers ? | **PARTIELLEMENT TRANCHÉ** | `TRIALS-E1-E4.md` §E2 |
| **E3** | Le transport virtio de Passerelle est-il réutilisable tel quel ? | **PARTIELLEMENT TRANCHÉ** | `TRIALS-E1-E4.md` §E3 |
| **E4** | Le DDK hyperviseur est-il accessible ? | **TRANCHÉ — NÉGATIF** | `TRIALS-E1-E4.md` §E4 |
| **E5** | 15–20 images/s en 1080p via un flush 2D ? | **NON MESURÉ** | `TRIALS-E5.md` |
| **E6** | **Le pilote invité Toile démarre-t-il dans un invité QNX 8.0, trouve-t-il le `virtio-gpu`, crée-t-il `/dev/gpu0` ?** | **TRANCHÉ — POSITIF sur les trois points**, avec **un défaut ouvert** sur le chemin « client » du resmgr | **ci-dessous** |

---

## E6 — Le pilote invité démarre dans l'invité

**Question.** Le code invité de Toile (`guest/gpu/` + `guest/virtio/` +
`guest/common/`) est-il du décor, ou tourne-t-il réellement dans un invité
QNX 8.0 sous QEMU ?

**Verdict : TRANCHÉ, POSITIF.** Le 2026-11-12, dans un invité QNX 8.0
démarré sous QEMU 8.2.2 avec `-device virtio-gpu-pci` :

1. le pilote **énumère le périphérique virtio-gpu via le serveur PCI** de
   QNX et l'annonce sur la console série ;
2. il **négocie le périphérique virtio**, ouvre la file de contrôle,
   **reçoit la réponse à `GET_DISPLAY_INFO`** (mode 1280×800) ;
3. il **envoie `RESOURCE_CREATE_2D` + `RESOURCE_ATTACH_BACKING` +
   `SET_SCANOUT`**, que le périphérique accepte (ressource 1 créée) ;
4. il **publie `/dev/gpu0`** (resource manager, `resmgr_attach` renvoie 0)
   et l'annonce sur la console.

**Un défaut reste ouvert** (§ « Défaut ouvert » plus bas) : le resource
manager QNX 8.0 plante quand un *client* ouvre `/dev/gpu0`. La publication
du nœud est prouvée ; l'usage du nœud par un client ne l'est pas encore.

### Méthode employée

1. **Compilation du pilote** (SDP QNX 8.0, `qcc`) :

   ```sh
   source /home/baptiste/qnx800/qnxsdp-env.sh
   cd /home/baptiste/Bureau/toile/guest
   make -f common.mk driver probe
   ```

   Commande `qcc` exacte d'un fichier (recette complète dans
   `guest/README.md`) :

   ```sh
   qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra -Werror \
       -I../core/include -Icommon -Igpu -Ivirtio \
       -Ibuild/pgw/common/include -Ibuild/pgw/virtio/include \
       -Ibuild/pgw/virtio/src \
       -c gpu/gpu_pci.c -o build/gpu/gpu_pci.o
   ```

   Liaison du pilote :

   ```sh
   qcc -V gcc_ntox86_64 build/gpu/*.o build/virtio/*.o build/common/*.o \
       build/core/*.o build/pgw/*.o -lpci -o build/toile-gpu
   ```

2. **Construction de l'image** (`image/toile-driver.build` : noyau +
   serveur PCI + `ksh` sur la ligne série + le pilote `toile-gpu` + le
   client de preuve `toile-gpu-probe`) :

   ```sh
   cd /home/baptiste/Bureau/toile
   source /home/baptiste/qnx800/qnxsdp-env.sh
   mkifs image/toile-driver.build /tmp/toile-driver/toile-driver.ifs
   ```

3. **Lancement** :

   ```sh
   qemu-system-x86_64 -m 1536 -kernel /tmp/toile-driver/toile-driver.ifs \
       -vga none -device virtio-gpu-pci -display none \
       -serial file:/tmp/toile-driver/serial-virtio-gpu-pci.log \
       -monitor none -enable-kvm -cpu host
   ```

4. **Capture** de la console série jusqu'au marqueur
   `=== TOILE_DRIVER_PROBE_END ===`, puis **verdict automatisé** par
   `qemu/probe-driver.sh` (code de sortie 0 = le pilote a trouvé le
   périphérique **et** créé `/dev/gpu0` ; 1 = non ; 2 = outillage).

Tout est scripté et rejouable :

```sh
cd /home/baptiste/Bureau/toile && ./qemu/probe-driver.sh ; echo "rc=$?"
```

### Sortie brute [VM — exécuté le 2026-11-12]

Journal série complet (`/tmp/toile-driver/serial-virtio-gpu-pci.log`,
retours chariot supprimés) :

```
non UEFI or UEFI+CSM boot
ACPI table not found (0x4746434d)
overriding mask for controller 2, vector_base 0
=== TOILE_DRIVER_PROBE_BEGIN ===
--- client /dev/gpu0 ---
CLIENT: ouverture de /dev/gpu0
TOILE: virtio-gpu trouve (1af4/1050) sur B000:D03:F00
toile-gpu: device up: 0 queues, msix=yes, features=0x100000000
toile-gpu: no cursorq: hardware cursor disabled (level C)
toile-gpu: scanout 0 mode 1280x800 (enabled)
toile-gpu: framebuffer 1280x800 fmt=2 stride=5120 pa=0x5e003000
toile-gpu: scanout 0 <- resource 1 (1280x800)
toile-gpu: 1280x800 fmt=2 scanouts=1 stride=5120 res=1 fb=4096000
toile-gpu: serving /dev/gpu0
TOILE: /dev/gpu0 pret
TOILE: signal fatal 11 addr=10 (SIGSEGV=11)
CLIENT: ECHEC ouverture de /dev/gpu0
client rc=1
--- pidin ---
     pid tid name                         prio STATE          Blocked
       1   1 /proc/boot/procnto-smp-instr   0f READY
       2   1 proc/boot/devc-ser8250        24r RECEIVE        1
       3   1 proc/boot/pci-server          10r RECEIVE        1
       4   1 proc/boot/ksh                 10r SIGSUSPEND
   16389   1 proc/boot/pidin               10r REPLY          1
=== TOILE_DRIVER_PROBE_END ===
```

Verdict du script [MACHINE] :

```
$ ./qemu/probe-driver.sh ; echo "rc=$?"
[probe-driver] VERDICT DRIVER = POSITIF : périphérique virtio-gpu trouvé + /dev/gpu0 créé
[probe-driver]   TOILE: virtio-gpu trouve (1af4/1050) sur B000:D03:F00
[probe-driver]   TOILE: /dev/gpu0 pret
[probe-driver]   (réserve) le client /dev/gpu0 n'a pas pu ouvrir le noeud :
[probe-driver]   le resmgr QNX 8.0 plante sur le message _IO_CONNECT du client
[probe-driver]   (défaut documenté dans docs/TRIALS-E1-E6.md, §Défaut ouvert)
rc=0
```

### Ce que cette sortie prouve, ligne par ligne

| Ligne | Ce qu'elle prouve |
|---|---|
| `TOILE: virtio-gpu trouve (1af4/1050) sur B000:D03:F00` | l'énumération **par le serveur PCI de QNX** (`pci_device_find`, pas `pci-tool`) trouve le `virtio-gpu` moderne et connaît son BDF |
| `device up: 0 queues, msix=yes, features=0x100000000` | la séquence de statut virtio 1.x (§3.1) est allée jusqu'à `FEATURES_OK` puis `DRIVER_OK` (le bit 32 = `VIRTIO_F_VERSION_1`) ; MSI-X a été programmé |
| `scanout 0 mode 1280x800 (enabled)` | **aller-retour effectif sur la file de contrôle** : le périphérique a répondu `RESP_OK_DISPLAY_INFO` |
| `framebuffer 1280x800 fmt=2 stride=5120 pa=0x5e003000` | allocation DMA **physiquement contiguë** (4 096 000 octets) |
| `scanout 0 <- resource 1 (1280x800)` | **le périphérique a accepté `RESOURCE_CREATE_2D`, `RESOURCE_ATTACH_BACKING` et `SET_SCANOUT`** — c'est la phase 2 (bonus) |
| `serving /dev/gpu0` + `TOILE: /dev/gpu0 pret` | `resmgr_attach()` a renvoyé 0 : le nœud `/dev/gpu0` est publié dans l'espace de noms du noyau |

### Phase atteinte

- **Phase 1 (obligatoire) : ATTEINTE.** Énumération + annonce
  `TOILE: virtio-gpu trouve ...`, création de `/dev/gpu0` + annonce
  `TOILE: /dev/gpu0 pret`. Un échec aurait été annoncé explicitement
  (`TOILE: ECHEC <raison>`) : ce mécanisme a d'ailleurs été exercé pendant
  la mise au point (voir plus bas).
- **Phase 2 (bonus) : ATTEINTE.** La première commande virtio-gpu
  (`RESOURCE_CREATE_2D`, avec son backing et son scanout) part sur la file
  de contrôle et **est acceptée par le périphérique** (ressource 1 créée,
  mode 1280×800 négocié). Le pilote journalise le verdict.

### Défaut ouvert (à ne pas masquer)

`/dev/gpu0` est **publié** mais un client ne peut pas encore l'ouvrir : à
la première requête d'un client, le pilote meurt sur `SIGSEGV` (`addr=10`,
c'est-à-dire un déréférencement de `NULL + 16`).

Analyse faite, documentée, **non résolue** :

- le message reçu est un `_IO_CONNECT` (type `0x100`, sous-type `1` =
  `_IO_CONNECT_COMBINE_CLOSE`) envoyé par le client (`stat()`/
  `realpath()` sont packagés ainsi par la libc client QNX 8.0) ;
- le gestionnaire **interne de la bibliothèque resmgr de QNX 8.0** plante
  **avant** d'appeler le moindre gestionnaire du pilote : ni
  `connect_funcs.open`, ni `connect_funcs.combo`, ni `io_funcs.stat` ne
  sont appelés (tracé par journalisation dans les trois gestionnaires) ;
- le crash se produit dans la bibliothèque, pas dans le code Toile : le
  pointeur d'OCB (`ctp->iov[0].iov_base`) vaut `NULL` au moment où la
  bibliothèque s'en sert.

Éléments déjà corrigés au passage (et **documentés dans le patch
`guest/virtio/toile-virtio-gpu.patch`** ou dans `guest/gpu/gpu_resmgr.c`) :

- l'ordre des inclusions (`<sys/iofunc.h>` **avant** `<sys/resmgr.h>`) :
  sans lui `RESMGR_HANDLE_T` vaut `void` et le prototype du gestionnaire
  `open` ne correspond plus ;
- `connect_funcs.open` et `connect_funcs.combo` sont renseignés
  (`iofunc_func_init()` ne remplit pas la table des connect) ;
- `io_funcs.stat` est protégé contre un OCB `NULL` (au lieu de déréférencer
  `NULL`).

**Ce qui reste à faire** pour ce défaut : comprendre la séquence
`_IO_CONNECT_COMBINE_CLOSE` de la bibliothèque resmgr QNX 8.0 (le
sous-type 1 est documenté comme « open + opération + close »), ou
implémenter le gestionnaire `combo` de façon complète (création de l'OCB,
opération, fermeture). Non fait à ce jour : le budget du lot était
consommé par la mise au point du transport.

### Corrections apportées au code invité par cet essai

Le pilote ne compilait pas et ne pouvait pas démarrer tel quel. Les
corrections (toutes dans `guest/`, zone réservée à ce lot) :

1. **`guest/virtio/toile_transport.{h,c}`** — la séquence de statut
   virtio 1.x (§3.1) est désormais pilotée **par Toile** et non plus par
   `pgw_vio_init_device()` : ce dernier commence par `pgw_vio_reset()`,
   dont le crochet QNX (`qnx_pci_teardown`) **détache le périphérique PCI
   et démonte ses BAR** — le périphérique était mort avant la lecture du
   premier mot de features. Nouvelle API `toile_tr_ready()` : `DRIVER_OK`
   n'est positionné qu'après la création des files (virtio 1.2 §3.1.4.2).
2. **`guest/virtio/toile-virtio-gpu.patch`** — la marche des capacités PCI
   de Passerelle lisait un octet de « longueur » dans **toutes** les
   capacités ; sur un vrai `virtio-gpu-pci` la capacité **MSI-X (0x11)**
   a un octet +2 qui est la taille de table, pas une longueur : le
   transport rejetait le périphérique (« capability 0x11 has length 2 »).
   Le hunk ne lit la longueur que pour les capacités *vendor*.
3. **`guest/gpu/gpu_pci.c`** — énumération explicite par `pci_device_find`
   + annonce du BDF sur la console (`gpu_pci_report()`).
4. **`guest/gpu/gpu_ctrl.c`** — `gpu_announce()` (lignes `TOILE: …` vidées
   immédiatement) et `gpu_rc_reason()` (motif lisible pour `TOILE: ECHEC`).
5. **`guest/gpu/gpu_main.c`** — `ThreadCtl(_NTO_TCTL_IO)` avant toute
   projection de BAR ou allocation DMA ; contexte `gpu_ctx` (≈ 35 ko, il
   contient le modèle du cœur) **sorti de la pile** (la pile du thread
   principal d'un processus lancé par une startup-script est petite) ;
   options initialisées (le `gpu_opts` non initialisé faisait parfois
   quitter le pilote au hasard) ; gestionnaire de signal fatal qui annonce
   `TOILE: signal fatal <n> addr=<p>` au lieu de mourir en silence.
6. **`guest/gpu/gpu_resmgr.c`** — gestionnaire `connect_funcs.open`
   (`iofunc_open_default`) et `connect_funcs.combo`, `io_funcs.stat`
   protégé, `resmgr_attr_t` complété (`msg_max_size`, `nparts_max`).
7. **`guest/common/toile_gpu_probe.c`** (nouveau) — client de preuve :
   il ouvre `/dev/gpu0` comme un client ordinaire et interroge le pilote
   par `devctl(DCMD_TOILE_GPU_INFO)`.

### Ce qui reste à faire (au-delà de ce lot)

- **Résoudre le défaut `connect` du resmgr** (ci-dessus) : c'est le
  bloquant pour démontrer `/dev/gpu0` *utilisable* (donc pour l'intégration
  Screen, E2).
- **Intégration Screen** (E2) : le display driver tiers (OpenWF Display /
  Wfdcfg) n'existe pas encore ; le squelette `guest/display/` n'a pas
  été touché par ce lot.
- **E5 réel** : la mesure de débit du flush 2D n'est pas faite ; ce lot
  n'apporte que la *commande* acceptée par le périphérique, pas la
  mesure de performance.
- **MSI-X / INTx** : le pilote programme MSI-X « à la main » (vecteur 0,
  adresse LAPIC), comme le transport Passerelle le fait ; le vecteur n'est
  pas alloué par le serveur PCI de QNX (`pci_device_alloc_msix()`), et le
  transport ne s'appuie pas sur les interruptions (il scrute la file
  « used »). À revoir pour un pilote de production.
- **`num_queues` lu à 0** par le transport (champ `num_queues` du common
  config) alors que la file de contrôle fonctionne : décalage d'offset à
  vérifier côté Passerelle — sans conséquence sur les phases 1 et 2 (une
  seule file est utilisée, `cursorq` n'est pas ouverte).

---

## Annexe — ce qui a été vérifié, et où

Distinction `[MACHINE]` / `[VM]` appliquée à ce lot :

- **[MACHINE]** : la compilation `qcc` du pilote et du client de preuve,
  la construction `mkifs` de l'IFS, le verdict du script
  `qemu/probe-driver.sh` (code de sortie 0), la relecture des en-têtes
  QNX 8.0 (`sys/resmgr.h`, `sys/iofunc.h`, `sys/iomsg.h`, `sys/ftype.h`,
  `sys/dispatch.h`) et de la documentation QNX 8.0 « Writing a Resource
  Manager » (pages *Fleshing Out the Skeleton*, *Connect messages*,
  *Combine Messages*, *Connect message types*) ;
- **[VM]** : tout le journal série ci-dessus, donc l'énumération, la
  négociation, l'échange `GET_DISPLAY_INFO`, la création de la ressource
  2D et la publication de `/dev/gpu0`.
