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
build/toile-gpu       94504 octets   pilote + resource manager /dev/gpu0
build/toile-display   19280 octets   démo d'affichage (-lscreen), 2026-11-12
build/toile-input     52760 octets   squelette virtio-input
build/toile-gpu-probe 13848 octets  client de preuve : ouvre /dev/gpu0 et
                                   interroge le pilote par devctl
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
| Pilote virtio-gpu + resource manager `/dev/gpu0` | **compilé et lié** ; **exécuté dans un invité QNX 8.0 le 2026-11-12** (`docs/TRIALS-E1-E6.md` §E6) : le pilote trouve le `virtio-gpu` (1af4/1050), négocie, reçoit `GET_DISPLAY_INFO`, fait accepter `RESOURCE_CREATE_2D` + backing + `SET_SCANOUT` et publie `/dev/gpu0` |
| Démo d'affichage (client Screen + framebuffer direct) | **compilée et liée** avec `-lscreen` ; chemins `--screen`, `--fb` (repli, D18) et `--wfd` |
| Display driver tiers | **interface identifiée** (E2) : **OpenWF Display (WFD) + Wfdcfg**, pas `io-display` — en-têtes `WF/wfd.h`, `WF/wfdext.h`, `wfdqnx/wfdcfg.h` du SDP 8.0 ; squelette **compilé et lié** (`qcc … -lscreen`, 0 warning) ; **intégration non testée** : `libWFDplatform`, `wfd-server` et `libwfdcfg.so` sont absents du SDP libre (BSP « Screen Board Support package ») |
| virtio-input | **compilé**, squelette : pas d'injection d'événements dans Screen |
| MMIO (variante `virtio-gpu-device`) | **non compilé** : backend Passerelle incompatible QNX 8.0 (voir ci-dessus) |
| Exécution réelle du pilote dans une VM | **faite le 2026-11-12** (`qemu/probe-driver.sh`, `image/toile-driver.build` → `docs/TRIALS-E1-E6.md` §E6). **Reste ouvert** : un client ne peut pas encore *ouvrir* `/dev/gpu0` (la bibliothèque resmgr QNX 8.0 plante sur le message `_IO_CONNECT` du client, avant le moindre gestionnaire Toile) |

## Lancer dans une VM

Voir `../qemu/README.md` (scripts `run-toile.sh`, `variants.sh`,
`probe-e1.sh`) et `../image/README.md` (construction de l'image invitée).
Pour la preuve « le pilote invite démarre dans l'invité », c'est le script
dédié :

```sh
cd /home/baptiste/Bureau/toile
./qemu/probe-driver.sh        # construit le pilote + l'image, lance QEMU,
                              # capture la console série, rend le verdict
```

Code de sortie 0 = le pilote a trouvé le `virtio-gpu` **et** créé
`/dev/gpu0` (journal série complet dans `docs/TRIALS-E1-E6.md` §E6).
