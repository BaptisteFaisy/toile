# guest/ — code invité QNX 8.0 (lots L4–L8)

Ce dossier contient **tout ce qui tourne réellement sur QNX** : le pilote
virtio-gpu, son resource manager `/dev/gpu0`, la couche transport adaptée de
Passerelle, la démo d'affichage et le squelette virtio-input.  Le cœur
portable (`core/`, C11 pur) n'est pas ici : il est consommé, pas modifié.

## Contenu

| Dossier | Rôle |
|---|---|
| `virtio/` | couche transport (adaptation de Passerelle, Apache-2.0) + patch amont minimal |
| `gpu/` | pilote virtio-gpu : énumération PCI, commandes §3 via le cœur, framebuffer, scanout, resource manager `/dev/gpu0` |
| `display/` | démo d'affichage : client Screen (mire dans une fenêtre) + chemin framebuffer direct `/dev/gpu0` (repli officiel, D18) + squelette de display driver **OpenWF Display (WFD) + Wfdcfg** (E2 partiellement tranché) |
| `input/` | virtio-input minimal (clavier/souris), **squelette non livré au 15/12** (niveau C, décision D7) |
| `common/` | code partagé : peintre de mire (`toile_pattern`) et bibliothèque cliente `/dev/gpu0` (`toile_gpu_client`) |
| `common.mk` | build invité (qcc) — **jamais appelé par le Makefile racine** |

## Recette de compilation (vérifiée le 2026-11-12)

```sh
source /home/baptiste/qnx800/qnxsdp-env.sh      # met qcc/mkifs dans PATH, exporte QNX_TARGET
cd /home/baptiste/Bureau/toile/guest
make -f common.mk                                # pilote + démo + input + client de preuve
make -f common.mk driver                         # le seul pilote (guest/build/toile-gpu)
make -f common.mk probe                          # client de preuve /dev/gpu0
make -f common.mk PASSERELLE_DIR=/autre/chemin/passerelle
make -f common.mk clean
```

Le Makefile :

1. copie les sources de Passerelle (`$(PASSERELLE_DIR)/virtio/src`,
   `$(PASSERELLE_DIR)/common/{include,src}`) dans `build/pgw/` — **l'arbre
   Passerelle n'est jamais modifié** ;
2. y applique `virtio/toile-virtio-gpu.patch` (types de périphérique 16 et
   18 + corrections d'API QNX 8.0) ;
3. compile tout avec `qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra
   -Werror` ;
4. lie trois binaires.

Sorties produites (vérifiées : `file` = ELF 64-bit QNX, `interpreter
/usr/lib/ldqnx-64.so.2`) :

```
build/toile-gpu       94968 octets   pilote + resource manager /dev/gpu0
build/toile-display   19280 octets   démo d'affichage (-lscreen), 2026-11-12
build/toile-input     52760 octets   squelette virtio-input
build/toile-gpu-probe 13880 octets  client de preuve : ouvre /dev/gpu0,
                                   interroge le pilote par devctl, fait
                                   peindre la mire et la présente
```

### Commande exacte (pour reproduction manuelle d'un fichier)

```sh
qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra -Werror \
    -I../core/include -Icommon -Igpu -Ivirtio \
    -Ibuild/pgw/common/include -Ibuild/pgw/virtio/include -Ibuild/pgw/virtio/src \
    -c gpu/gpu_pci.c -o build/gpu/gpu_pci.o
```

Liaison du pilote :

```sh
qcc -V gcc_ntox86_64 build/gpu/*.o build/virtio/*.o build/common/*.o \
    build/core/*.o build/pgw/*.o -lpci -o build/toile-gpu
```

## Choix de compilation documentés

- **`-std=gnu11` et non `-std=c11`** : en C11 strict, QNX 8.0 masque les
  extensions UNIX (`uint_t`, utilisé par `<pci/pci.h>`) et les prototypes
  POSIX (`clock_gettime`).  `gnu11` est le standard par défaut de `qcc`.
  Le cœur portable reste C11 strict : la CI racine le compile avec
  `-std=c11 -Werror` (Makefile racine, inchangé).
- **`-D_POSIX_C_SOURCE` n'est PAS utilisé** : il désactive `__EXT_UNIX_MISC`
  et casse `<pci/pci.h>` (erreur réelle observée : `unknown type name
  'uint_t'`).
- **`mmio_qnx.c` de Passerelle est exclu** du build : sur QNX 8.0
  `mmap_device_io()` renvoie un descripteur opaque (accès par
  `devctl`/`mdevctl`), pas une adresse utilisable au pointeur.  Aucun
  appelant ne référence `pgw_vio_mmio_open_phys()`, l'exclusion ne casse pas
  l'édition de liens.  Détail dans `docs/TRIALS-E1-E4.md` §E3.

## Ce qui compile aujourd'hui / ce qui manque

| Élément | État |
|---|---|
| Transport virtio (adaptation Passerelle) | **compilé QNX x86_64, 0 warning** |
| Pilote virtio-gpu + resource manager `/dev/gpu0` | **compilé et lié** ; **exécuté dans un invité QNX 8.0 le 2026-11-12** (`docs/TRIALS-E1-E6.md` §E6) : le pilote trouve le `virtio-gpu` (1af4/1050), négocie, reçoit `GET_DISPLAY_INFO`, fait accepter `RESOURCE_CREATE_2D` + backing + `SET_SCANOUT` et publie `/dev/gpu0`. **Le nœud est utilisable** : un client l'ouvre (`fd=3`), interroge le pilote par `devctl`, fait peindre la mire et la présente — **la mire est visible dans l'affichage QEMU** (preuve B, `docs/preuves/toile-mire-client-1280x800.png`) |
| Démo d'affichage (client Screen + framebuffer direct) | **compilée et liée** avec `-lscreen` ; chemins `--screen`, `--fb` (repli, D18) et `--wfd` |
| Display driver tiers | **interface identifiée** (E2) : **OpenWF Display (WFD) + Wfdcfg**, pas `io-display` — en-têtes `WF/wfd.h`, `WF/wfdext.h`, `wfdqnx/wfdcfg.h` du SDP 8.0 ; squelette **compilé et lié** (`qcc … -lscreen`, 0 warning) ; **intégration non testée** : `libWFDplatform`, `wfd-server` et `libwfdcfg.so` sont absents du SDP libre (BSP « Screen Board Support package ») |
| virtio-input | **compilé**, squelette : pas d'injection d'événements dans Screen |
| MMIO (variante `virtio-gpu-device`) | **non compilé** : backend Passerelle incompatible QNX 8.0 (voir ci-dessus) |
| Exécution réelle du pilote dans une VM | **faite le 2026-11-12** (`qemu/probe-driver.sh`, `image/toile-driver.build` → `docs/TRIALS-E1-E6.md` §E6). **La boucle invitée → émulateur est fermée** : un client ouvre `/dev/gpu0`, la mire peinte dans l'invité est visible dans l'affichage de QEMU (`qemu/run-demo.sh`, preuve B). **Reste ouvert** : la relecture numérique `TRANSFER_FROM_HOST_3D` (0x0206) est **refusée par le device de test** (QEMU 8.2.2 sans virgl, `0x1200`) — voir `docs/TRIALS-E1-E6.md` § « Preuve A » |

## Contraintes du resource manager QNX 8.0 (à ne pas oublier)

`guest/gpu/gpu_resmgr.c` suit le modèle iofunc canonique. Deux règles
viennent de la documentation du SDP et sont **structurelles** :

1. **`resmgr_attach()` conserve les POINTEURS** vers
   `resmgr_connect_funcs_t` / `resmgr_io_funcs_t` (il n'en fait pas de
   copie) : ces tables — et le `resmgr_attr_t` — doivent être **statiques
   de fichier**, pas des variables locales d'une fonction qui rend la main.
   Sinon la bibliothèque relit des pointeurs pendants à la première
   requête client et le pilote meurt sur `SIGSEGV`.
   Source : *Writing a Resource Manager* → *Connect messages*,
   `com.qnx.doc.neutrino.resmgr/topic/fleshing_out_Connect_messages.html`.
2. **Le message `_IO_CONNECT_COMBINE_CLOSE` se laisse à la bibliothèque** :
   elle déroule « open → opération → close » toute seule. Il ne faut pas
   écrire de callout `combo` maison.
   Source : *Connect message types*,
   `com.qnx.doc.neutrino.resmgr/topic/combine_ConnectMsgStyles.html`.

Ordre des inclusions : `<sys/iofunc.h>` **avant** `<sys/resmgr.h>`, sinon
`RESMGR_HANDLE_T` vaut `void` et le prototype du gestionnaire `open` ne
correspond plus.

## Lancer dans une VM

Voir `../qemu/README.md` (scripts `run-toile.sh`, `variants.sh`,
`probe-e1.sh`) et `../image/README.md` (construction de l'image invitée).

### La démo complète, en une commande

```sh
cd /home/baptiste/Bureau/toile
./qemu/run-demo.sh                # compile, construit l'IFS, démarre QEMU,
                                  # fait peindre la mire par le client,
                                  # capture l'affichage, affiche le verdict
```

`run-demo.sh` est **headless** (VNC sans serveur X). Variables
d'environnement :

| Variable | Défaut | Rôle |
|---|---|---|
| `TOILE_SDP` | `~/qnx800` | racine du SDP QNX 8.0 (`qcc`, `mkifs`) |
| `TOILE_WORK` | `/tmp/toile-demo` | répertoire de travail (image IFS, journaux, capture) |
| `TOILE_VARIANT` | `virtio-gpu-pci` | périphérique QEMU |
| `TOILE_DISPLAY` | `vnc` | `none` \| `vnc` \| `egl-headless` — `vnc` et `egl-headless` permettent la capture |
| `TOILE_TIMEOUT` | `120` | secondes d'attente de la sonde |

Code de sortie **0** = la boucle invitée → émulateur est prouvée de bout en
bout : `/dev/gpu0` ouvert par un client, mire peinte dans l'invité,
**mire visible dans la capture de l'affichage QEMU** (vérifiée numériquement
par le script). La capture est déposée dans `docs/preuves/`.

### La sonde seule (pilote + client, sans capture)

```sh
cd /home/baptiste/Bureau/toile
./qemu/probe-driver.sh            # construit le pilote + l'image, lance QEMU,
                                  # capture la console série, rend le verdict
TOILE_DISPLAY=vnc ./qemu/probe-driver.sh   # idem + capture d'écran vérifiée
```

Code de sortie 0 = le pilote a trouvé le `virtio-gpu`, **créé et publié**
`/dev/gpu0`, et **le client l'a ouvert** (`CLIENT: /dev/gpu0 ouvert
(fd=3)`), a fait peindre la mire et l'a présentée (journal série complet
dans `docs/TRIALS-E1-E6.md` §E6).

### Recette manuelle (si l'on veut voir chaque étape)

```sh
source /home/baptiste/qnx800/qnxsdp-env.sh
cd /home/baptiste/Bureau/toile/guest && make -f common.mk driver probe
cd /home/baptiste/Bureau/toile && mkifs image/toile-driver.build /tmp/toile-driver.ifs
qemu-system-x86_64 -m 1536 -kernel /tmp/toile-driver.ifs \
    -vga none -device virtio-gpu-pci -vnc :7 \
    -serial file:/tmp/toile-driver/serial.log \
    -monitor unix:/tmp/toile-driver/mon.sock,server,nowait \
    -enable-kvm -cpu host
# puis, depuis le monitor HMP : screendump /tmp/toile-driver/shot.ppm
```
