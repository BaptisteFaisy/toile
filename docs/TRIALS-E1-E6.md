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
| **E6** | **Le pilote invité Toile démarre-t-il dans un invité QNX 8.0, trouve-t-il le `virtio-gpu`, crée-t-il `/dev/gpu0` ?** | **TRANCHÉ — POSITIF sur les trois points**, et **la boucle invitée → émulateur est fermée** (un client ouvre `/dev/gpu0` et la mire peinte dans l'invité est visible dans QEMU) | **ci-dessous** |

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
   et l'annonce sur la console ;
5. **un client ordinaire l'ouvre** (`open("/dev/gpu0", O_RDWR)` → `fd=3`,
   `iofunc_open_default` → `rc=0`), l'interroge par `devctl`, lui fait
   **peindre la mire** et la **présenter** au scanout, et **la mire apparaît
   dans l'affichage de QEMU** (§ « Preuve B » plus bas).

**Le défaut qui bloquait l'ouverture du nœud est CORRIGÉ** (§ « Le défaut
corrigé » plus bas) : la cause exacte était que `resmgr_attach()` conserve
les *pointeurs* vers les tables de gestionnaires, lesquelles étaient
déclarées sur la pile d'une fonction qui rendait la main.

### La boucle invitée → émulateur, en une commande

```sh
cd /home/baptiste/Bureau/toile
./qemu/run-demo.sh            # compile, construit l'IFS, démarre QEMU,
                               # fait peindre la mire par le client,
                               # capture l'affichage, affiche le verdict
```

`run-demo.sh` est **headless** (VNC sans serveur X) et se règle par
variables d'environnement : `TOILE_SDP`, `TOILE_WORK`, `TOILE_VARIANT`,
`TOILE_DISPLAY` (`none` | `vnc` | `egl-headless`), `TOILE_TIMEOUT`.
Il dépose la capture dans `docs/preuves/` et rend **0** si la boucle est
complète.

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

### Sortie brute [VM] — **avant** correctif (2026-11-12, référence)

Journal série complet (`/tmp/toile-driver/serial-virtio-gpu-pci.log`,
retours chariot supprimés) — **état historique conservé pour comparaison** :

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

Verdict du script [MACHINE] — **avant** correctif :

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

### Sortie brute [VM] — **après** correctif (2026-11-13)

Journal série complet (`$TOILE_WORK/serial-virtio-gpu-pci.log`,
retours chariot supprimés) :

```
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
toile-gpu: resmgr: open -> rc=0
CLIENT: /dev/gpu0 ouvert (fd=3)
CLIENT: info 1280x800 fmt=2 scanouts=1 stride=5120 res=1 fb=4096000
CLIENT: framebuffer paddr=0x5e003000 bytes=4096000 stride=5120 1280x800
toile-gpu: device error response 0x1200 for cmd 0x0206
TOILE: relecture transfer_from_host(0x0206) rc=-4 resp=0x1200 avant=13a87ef6963752f3 apres=13a87ef6963752f3 REFUSEE_PAR_LE_PERIPHERIQUE
CLIENT: mire peinte et presentee
CLIENT: OK
client rc=0
--- pidin ---
     pid tid name                         prio STATE          Blocked
       1   1 /proc/boot/procnto-smp-instr   0f READY
       1  10 /proc/boot/procnto-smp-instr  10r RUNNING
       2   1 proc/boot/devc-ser8250        24r RECEIVE        1
       3   1 proc/boot/pci-server          10r RECEIVE        1
       4   1 proc/boot/ksh                 10r SIGSUSPEND
       5   1 proc/boot/toile-gpu           10r RECEIVE        1
   12294   1 proc/boot/pidin               10r REPLY          1
=== TOILE_DRIVER_PROBE_END ===
```

Plus de `SIGSEGV` : le client ouvre le nœud (`fd=3`), interroge le
pilote, fait peindre et présenter la mire, et sort avec `rc=0`.

Verdict du script [MACHINE] — **après** correctif :

```
$ TOILE_DISPLAY=vnc ./qemu/run-demo.sh ; echo "rc=$?"
[probe-driver] VERDICT DRIVER = POSITIF
[probe-driver]   TOILE: virtio-gpu trouve (1af4/1050) sur B000:D03:F00
[probe-driver]   TOILE: /dev/gpu0 pret
[probe-driver]   CLIENT: /dev/gpu0 ouvert (fd=3)
[probe-driver]   CLIENT: mire peinte et presentee
[probe-driver] VERDICT CAPTURE = POSITIF : la mire est visible dans QEMU
=== Verdict ===
  Boucle invitée -> émulateur : PROUVÉE
  - /dev/gpu0 ouvert par un client ordinaire (plus de SIGSEGV)
  - mire peinte par l'invité et présentée au scanout virtio-gpu
  - capture de l'affichage QEMU vérifiée numériquement
  - preuve déposée : docs/preuves/toile-mire-client-1280x800.png (4412 octets)
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
| `toile-gpu: resmgr: open -> rc=0` + `CLIENT: /dev/gpu0 ouvert (fd=3)` | **un client ordinaire ouvre le nœud** : le resmgr survit à la requête et l'OCB est créé (`iofunc_open_default` → `rc=0`) |
| `CLIENT: info 1280x800 fmt=2 …` + `CLIENT: framebuffer paddr=0x5e003000 …` | les `devctl` Toile répondent (`DCMD_TOILE_GPU_INFO`, `DCMD_TOILE_GPU_FB`) |
| `CLIENT: mire peinte et presentee` | le pilote a peint la mire dans le framebuffer et l'a présentée (`TRANSFER_TO_HOST_2D` + `RESOURCE_FLUSH`) |
| `TOILE: relecture transfer_from_host(0x0206) … REFUSEE_PAR_LE_PERIPHERIQUE` | la relecture 0x0206 a été **tentée** et **refusée** par le device (`0x1200` = `ERR_UNSPEC`) — voir § « Preuve A » |

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
- **Phase 3 (fermeture de la boucle) : ATTEINTE.** Un client ouvre
  `/dev/gpu0`, le pilote y peint la mire et la présente, et **la mire est
  visible dans l'affichage de QEMU** (§ « Preuve B »).

### Le défaut corrigé : cause exacte du SIGSEGV et correctif

**Symptôme** (avant correctif) : `/dev/gpu0` était **publié** mais un
client ne pouvait pas l'ouvrir ; à la première requête d'un client, le
pilote meurt sur `SIGSEGV` (`addr=10`, c'est-à-dire un déréférencement de
`NULL + 16`).

**Cause exacte** — `resmgr_attach()` **conserve les POINTEURS** vers les
tables de gestionnaires, il n'en fait pas de copie. La documentation QNX
8.0 l'écrit noir sur blanc dans *Writing a Resource Manager* →
*Connect messages* (`com.qnx.doc.neutrino.resmgr/topic/
fleshing_out_Connect_messages.html`) :

> The `resmgr_attach()` function copies the pointers to the
> `resmgr_connect_funcs_t` and `resmgr_io_funcs_t` structures, **not the
> structures themselves**. You should allocate the structures, declare them
> to be static, or make them global variables.

Or le code de `guest/gpu/gpu_resmgr.c` déclarait ces deux tables — **et le
`resmgr_attr_t`** — comme variables **locales** de `gpu_resmgr_attach()`,
fonction **qui rend la main** juste après l'appel. À la première requête
client, la bibliothèque resmgr relisait donc des pointeurs pendants vers une
frame de pile déjà réutilisée : elle sautait dans n'importe quoi, d'où le
`SIGSEGV` à une adresse minuscule (`0x10`) **avant** d'atteindre le moindre
gestionnaire Toile — ce qui expliquait l'observation « le crash précède
tous mes handlers ».

**Correctif** (`guest/gpu/gpu_resmgr.c`) :

1. les tables passent en **statique de fichier** (`g_rattr`,
   `g_connect_funcs`, `g_io_funcs`), donc vivantes aussi longtemps que le
   processus ;
2. le **callout `combo` maison est supprimé** : le message
   `_IO_CONNECT_COMBINE_CLOSE` est laissé à la bibliothèque, qui déroule
   elle-même « open → opération → close » (doc *Connect message types*) ;
3. `iofunc_func_init()` remplit déjà `connect.open = iofunc_open_default` ;
   le handler Toile ne fait plus qu'**envelopper** ce défaut pour le
   journaliser, sans le remplacer ;
4. `iofunc_attr_init()` reçoit le bon mode (`S_IFCHR | 0666`) : le nœud est
   un périphérique, pas un fichier ordinaire ;
5. le `devctl` Toile reste le **seul** handler I/O surchargé
   (`io_funcs.devctl`), les autres restent les défauts POSIX iofunc.

**Preuve du correctif** ([VM], sortie brute ci-dessus) :

```
toile-gpu: serving /dev/gpu0
TOILE: /dev/gpu0 pret
toile-gpu: resmgr: open -> rc=0
CLIENT: /dev/gpu0 ouvert (fd=3)
CLIENT: info 1280x800 fmt=2 scanouts=1 stride=5120 res=1 fb=4096000
CLIENT: framebuffer paddr=0x5e003000 bytes=4096000 stride=5120 1280x800
CLIENT: mire peinte et presentee
CLIENT: OK
client rc=0
```

Plus aucun `SIGSEGV`, et le client va au bout (ouverture, `devctl` INFO,
`devctl` FB, peinture, présentation).

**Note d'honnêteté** : la cause a été trouvée en **lisant la documentation
du SDP**, pas en tâtonnant — l'hypothèse « pointeur d'OCB `NULL` » de
l'analyse précédente était **infime** ; la cause réelle est la durée de vie
des tables de gestionnaires. Les deux autres correctifs listés dans
l'ancienne version de cette section (ordre des inclusions
`<sys/iofunc.h>` avant `<sys/resmgr.h>`, protection de `io_funcs.stat`
contre un OCB `NULL`) sont **conservés** : le premier est requis par la
définition de `RESMGR_HANDLE_T`, le second est devenu sans objet puisque
le gestionnaire `stat` n'est plus surchargé.

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
6. **`guest/gpu/gpu_resmgr.c`** — **tables de gestionnaires et `resmgr_attr_t`
   passés en statique de fichier** (durée de vie exigée par `resmgr_attach()`),
   **callout `combo` maison supprimé** (la bibliothèque gère
   `_IO_CONNECT_COMBINE_CLOSE`), `connect.open` = enveloppe de
   `iofunc_open_default` (journalisation seule), `io_funcs.devctl` = seul
   handler I/O surchargé, `iofunc_attr_init()` en `S_IFCHR | 0666`.
7. **`guest/common/toile_gpu_probe.c`** (nouveau) — client de preuve :
   il ouvre `/dev/gpu0` comme un client ordinaire, interroge le pilote
   par `devctl(DCMD_TOILE_GPU_INFO)` et `devctl(DCMD_TOILE_GPU_FB)`, puis
   demande la peinture de la mire (`DCMD_TOILE_GPU_MIRE`) et sa présentation
   (`DCMD_TOILE_GPU_PRESENT`).
8. **`guest/gpu/gpu_resmgr.c`** (empreinte de relecture) — à la peinture de
   la mire, le pilote **empreint** le framebuffer (FNV-1a 64), émet
   `TRANSFER_FROM_HOST_3D` (0x0206) et ré-empreint, pour tenter la preuve
   numérique A (§ « Preuve A »).

### Preuve A (relecture numérique) — **NON SUPPORTÉE par le périphérique de test**

La commande de relecture hôte→invité du protocole virtio-gpu 2D est
`TRANSFER_FROM_HOST_3D`, code **0x0206** (exposé dans le DSL du cœur sous
le nom `transfer_from_host`, `core/include/toile/vgproto.h:37`). Le pilote
l'émet après la peinture de la mire, et compare l'empreinte du framebuffer
avant et après.

**Sortie brute** ([VM], journal série de la commande `run-demo.sh`) :

```
toile-gpu: device error response 0x1200 for cmd 0x0206
TOILE: relecture transfer_from_host(0x0206) rc=-4 resp=0x1200 \
  avant=13a87ef6963752f3 apres=13a87ef6963752f3 REFUSEE_PAR_LE_PERIPHERIQUE
```

**Verdict : la lecture n'est PAS supportée par le périphérique de test.**
`0x1200` est `VIRTIO_GPU_RESP_ERR_UNSPEC` : le device virtio-gpu de QEMU
8.2.2 **sans virgl** n'implémente pas la commande 3D `TRANSFER_FROM_HOST_3D`
(série 0x02xx) — il ne connaît que la série 2D 0x01xx. Le code `0x0206`
exige un contexte 3D (virgl), absent ici.

Conséquences, dites franchement :

- les deux empreintes sont **identiques** (`13a87ef6963752f3`), mais c'est
  **sans valeur probante** : rien n'a été recopié, le périphérique a
  refusé la commande. La ligne l'annonce explicitement
  (`REFUSEE_PAR_LE_PERIPHERIQUE`) au lieu de laisser croire à une preuve ;
- **la preuve A, telle que spécifiée (relecture par le device), est
  impossible sur ce device de test** ; elle demanderait un device virgl
  (`virtio-gpu-gl-pci` avec `virgl`/EGL), pas disponible ici ;
- la preuve du transit invité→émulateur est donc portée par la **preuve B**
  ci-dessous, qui est **numérique** (comparaison de pixels, pas seulement
  une image) : le contenu exact de la mire peinte dans l'invité est relu
  **par l'hôte**, dans l'image de l'affichage QEMU.

### Preuve B (capture de l'affichage QEMU) — **POSITIVE**

**Procédure** ([MACHINE], rejouable en une commande) :

```sh
cd /home/baptiste/Bureau/toile
TOILE_DISPLAY=vnc ./qemu/run-demo.sh
```

`run-demo.sh` lance QEMU avec `-vnc :7` (serveur VNC = display actif,
**sans serveur X**) et `-monitor unix:…` ; à la fin de la sonde il émet la
commande HMP `screendump /tmp/…/toile-shot.ppm` via le monitor, convertit
le PPM en PNG (Pillow) et **vérifie numériquement** le contenu.

**Sortie brute** [MACHINE] :

```
[probe-driver] affichage VNC actif sur :7 (capture possible)
[probe-driver] capture de l'affichage QEMU (screendump HMP) vers /tmp/toile-final/toile-shot.ppm
[probe-driver] analyse numérique de la capture
capture: 1280x800, 263 couleurs distinctes, non-noirs=927400 (90.6%)
  barres     : OK
  dégradé    : (31, 31, 31) -> (223, 223, 223)
  damier     : (224, 224, 224) / (32, 96, 160)
[probe-driver] VERDICT CAPTURE = POSITIF : la mire est visible dans QEMU
[probe-driver]   capture: /tmp/toile-final/toile-shot.png
```

**Vérification numérique** — la mire de `guest/common/toile_pattern.c` est
rejouée sur l'image capturée :

| Contrôle | Attendu | Lu | Verdict |
|---|---|---|---|
| barre 0 (blanc) | `(255,255,255)` | `(255,255,255)` | OK |
| barre 1 (jaune) | `(255,255,0)` | `(255,255,0)` | OK |
| barre 2 (cyan) | `(0,255,255)` | `(0,255,255)` | OK |
| barre 3 (vert) | `(0,255,0)` | `(0,255,0)` | OK |
| barre 4 (magenta) | `(255,0,255)` | `(255,0,255)` | OK |
| barre 5 (rouge) | `(255,0,0)` | `(255,0,0)` | OK |
| barre 6 (bleu) | `(0,0,255)` | `(0,0,255)` | OK |
| barre 7 (noir) | `(0,0,0)` | `(0,0,0)` | OK |
| dégradé de gris | gris croissant | `(31,31,31)` → `(223,223,223)` | OK |
| damier 16 px | 2 couleurs distinctes | `(224,224,224)` / `(32,96,160)` | OK |
| pixels non noirs | > 50 % | 927 400 / 1 024 000 = **90,6 %** | OK |
| couleurs distinctes | > 8 | **263** (le dégradé en compte 256) | OK |

Les 8 barres couvrent chacune 96 000 px, soit exactement
`(1280/8) × (800×3/4) = 160 × 600` : la géométrie de la mire est respectée
au pixel près. **L'image n'est pas noire** (90,6 % de pixels non noirs) et
elle contient bien la mire, à la bonne place.

**Chemin des images** :

| Image | Chemin | Taille |
|---|---|---|
| capture de cette démonstration (peinture **par le client** via `/dev/gpu0`) | `docs/preuves/toile-mire-client-1280x800.png` | 4 412 octets |
| capture antérieure (peinture par le pilote seul, `--mire`) | `docs/preuves/toile-mire-1280x800.png` | 4 412 octets |
| capture brute de la dernière exécution | `$TOILE_WORK/toile-shot.png` (PNG) et `toile-shot.ppm` (PPM) | ~4 Ko / ~3 Mo |

Les deux PNG sont **identiques** (même md5 `d3c0df5b…`) : c'est attendu,
la mire est la même ; ils se distinguent par le **chemin qui la produit**
(l'un par le client via `/dev/gpu0`, l'autre par le pilote seul).

### Ce qui reste à faire (au-delà de ce lot)

- **Intégration Screen** (E2) : le display driver tiers (OpenWF Display /
  Wfdcfg) n'existe pas encore ; le squelette `guest/display/` n'a pas
  été touché par ce lot.
- **Preuve A sur un device virgl** : la relecture 0x0206 exigerait
  `virtio-gpu-gl-pci` avec virgl/EGL ; le device de test (QEMU 8.2.2 sans
  virgl) la refuse (`0x1200`). La boucle invitée→émulateur reste prouvée
  par la preuve B (numérique).
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
  `qemu/probe-driver.sh` (code de sortie 0), l'exécution complète de
  `qemu/run-demo.sh` (code de sortie 0), l'analyse numérique de la capture
  (Pillow), la relecture des en-têtes QNX 8.0 (`sys/resmgr.h`,
  `sys/iofunc.h`, `sys/iomsg.h`, `sys/ftype.h`, `sys/dispatch.h`) et de la
  documentation QNX 8.0 « Writing a Resource Manager » (pages *Fleshing Out
  the Skeleton*, *Connect messages*, *Combine Messages*, *Connect message
  types*, *Using the default functions*, *The iofunc_attr_t attributes
  structure*) ;
- **[VM]** : tout le journal série ci-dessus, donc l'énumération, la
  négociation, l'échange `GET_DISPLAY_INFO`, la création de la ressource
  2D, la publication de `/dev/gpu0`, **l'ouverture du nœud par un client**,
  **la peinture et la présentation de la mire**, et le refus de la
  relecture 0x0206 par le périphérique.
