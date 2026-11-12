# TRIALS — E1, E2, E3, E4 (et E5) — fichier de vérité

**Date des vérifications : 2026-11-12. Machine : PC Linux, SDP QNX 8.0
installé dans `~/qnx800` (225 paquets), QEMU 8.2.2 (`/usr/bin/qemu-system-x86_64`),
KVM disponible.**

Règle de lecture de ce document, appliquée partout :

- **[MACHINE]** = vérifié **sur cette machine**, sortie brute ci-dessous ;
- **[VM]** = exige une VM QNX démarrée ; soit la procédure est écrite et
  reproductible, soit l'essai n'a pas pu être conduit ;
- **jamais** d'affirmation « ça marche » sans la sortie correspondante.

| Essai | Question | Verdict |
|---|---|---|
| **E1** | L'invité QNX 8.0 énumère-t-il `virtio-gpu-pci` sur son bus PCI ? | **TRANCHÉ — POSITIF** (exécuté, sortie brute) |
| **E2** | Screen accepte-t-il un display driver tiers, et quel est le contrat exact ? | **PARTIELLEMENT TRANCHÉ** — interface identifiée : OpenWF Display (WFD) + Wfdcfg ; intégration non testée (DDK hors licence) |
| **E3** | Le transport virtio de Passerelle est-il réutilisable tel quel pour virtio-gpu ? | **PARTIELLEMENT TRANCHÉ** — cœur réutilisable ; 3 listes blanches + 6 écarts d'API QNX 8.0 corrigés par un patch de 100 lignes |
| **E4** | Le DDK hyperviseur est-il accessible en licence non commerciale ? | **TRANCHÉ — NÉGATIF** (absent du SDP **et** non couvert par la licence installée) — périmètre : cette licence |
| **E5** | 15–20 images/s en 1080p via un flush 2D virtio-gpu ? | **NON MESURÉ** (hors périmètre de ce lot) |

---

## E1 — Énumération PCI du périphérique virtio-gpu dans l'invité QNX

**Question.** Un invité QNX 8.0 sous QEMU (`-device virtio-gpu-pci`)
énumère-t-il le périphérique sur son bus PCI ?

**Verdict : TRANCHÉ, POSITIF.** L'essai a été **exécuté** le 2026-11-12 :
l'invité QNX 8.0 voit le périphérique `1af4/1050` (virtio-gpu moderne) sur
son bus PCI, en `B000:D03:F00` (index 5), avec le code de classe `03/80/00`.

### Méthode employée

1. **Image de sonde minimale** (`image/toile-e1.build`, construite avec
   `mkifs` du SDP) : noyau `startup-x86` + `procnto-smp-instr`, serveur PCI
   (`pci-server` + `pci_hw-Intel_x86.so` + `pci_server.cfg`), `pci-tool`,
   `devinfo`, `ksh` sur la ligne série. Aucun disque : tout tient en RAM,
   l'IFS est chargée par `-kernel`.
2. **Lancement** :
   `qemu-system-x86_64 --enable-kvm --cpu host -m 1536 -kernel <ifs> -vga none
   -device virtio-gpu-pci -display none -serial file:<log> -monitor none`.
3. **Capture** de la console série jusqu'aux marqueurs
   `=== TOILE_E1_PROBE_BEGIN/END ===`.
4. **Verdict** automatisé : `grep '1af4/1050'`.

La procédure est scriptée et rejouable : `qemu/probe-e1.sh`
(code de sortie 0 = positif).

### Sortie brute [VM — exécuté]

```
=== TOILE_E1_PROBE_BEGIN ===
--- pci-tool ---

Partition: pci

Managing Server ID: 1, pid: 3, running
B000:D00:F00 @ idx 0
B000:D01:F00 @ idx 1
B000:D01:F01 @ idx 2
B000:D01:F03 @ idx 3
B000:D02:F00 @ idx 4
B000:D03:F00 @ idx 5

--- pci-tool -v ---

Partition: pci

Managing Server ID: 1, pid: 3, running
B000:D00:F00 @ idx 0
	vid/did: 8086/1237
	class/subclass/reg: 06/00/00

B000:D01:F00 @ idx 1
	vid/did: 8086/7000
	class/subclass/reg: 06/01/00

B000:D01:F01 @ idx 2
	vid/did: 8086/7010
	class/subclass/reg: 01/01/80

B000:D01:F03 @ idx 3
	vid/did: 8086/7113
	class/subclass/reg: 06/80/00

B000:D02:F00 @ idx 4
	vid/did: 8086/100e
	class/subclass/reg: 02/00/00

B000:D03:F00 @ idx 5
	vid/did: 1af4/1050
	class/subclass/reg: 03/80/00

--- devinfo ---
devinfo: sysctlbyname("hw.bus.info", ...) failed: Address family not supported by protocol family
devinfo: devinfo_init: Invalid argument
=== TOILE_E1_PROBE_END ===
[probe-e1] VERDICT E1 = POSITIF : périphérique virtio-gpu (1af4/1050) énuméré
```

Lecture : `1af4` = Red Hat (fournisseur virtio), `1050` = device id 16 en
virtio moderne (`0x1040 + 16`), classe `03/80/00` = contrôleur d'affichage.
Le `devinfo` échoue (`sysctlbyname("hw.bus.info")` non supporté) : l'outil
`devinfo` n'est pas exploitable dans cette image minimale, **seul `pci-tool`
fait foi** — c'est `pci-tool` qui est la preuve.

### Variante `virtio-vga` [VM — exécuté]

```
B000:D03:F00 @ idx 5
	vid/did: 1af4/1050
	class/subclass/reg: 03/00/00
```

Même périphérique, code de classe VGA compatible (`03/00/00`) au lieu de
`03/80/00`. **Les deux variantes PCI sont énumérées par l'invité.**

### Variante `virtio-gpu-device` (MMIO)

**Non applicable ici** : le transport virtio-MMIO n'existe que sur les
machines ARM (`virt` de QEMU). Un invité x86_64 n'a pas de bus MMIO
virtio : la variante suppose un invité **aarch64**, à re-tester plus tard
(qemu/variants.sh le signale et l'ignore). [VM — non exécuté]

### Ce qu'il reste à faire sur cible

- Enchaîner avec le **pilote Toile** lui-même : aujourd'hui l'image de sonde
  ne contient que `pci-tool` ; le pilote (`guest/gpu/`) est compilé et lié
  mais **n'a pas encore été démarré dans l'invité**.
- Tester la variante MMIO sur un invité aarch64 (machine `virt`).

---

## E2 — Display driver tiers pour Screen

**Question.** Screen accepte-t-il un display driver tiers ? Quel est le
contrat exact (`io-display` ? bibliothèque de display driver ? points
d'entrée ?) ?

**Verdict : PARTIELLEMENT TRANCHÉ — interface identifiée, intégration non
testée.** Le contrat réel **n'est pas** « io-display » (modèle QNX 6.x,
absent du SDP 8.0) mais un **driver OpenWF Display (WFD)** couplé à la
**bibliothèque Wfdcfg**. Les en-têtes sont présents et compilables ; ce
qui manque est la partie *serveur + plateforme* (`libWFDplatform`,
`wfd-server`, `libwfdcfg.so`), livrée dans les « Screen Board Support
packages », **hors SDP libre**. L'interface est donc connue et vérifiée ;
l'intégration ne peut pas l'être ici.

### Méthode employée

1. **Recherche d'un DDK de display driver** dans le SDP installé :
   `io-display`, `disputils.h`, `devg-*`, répertoire `ddk/` → **rien**.
2. **Inventaire des en-têtes réellement présents** : `WF/wfd.h`,
   `WF/wfdext.h`, `WF/wfdplatform.h`, `wfdqnx/wfdcfg.h`, `screen/screen.h`,
   `screen/screen_ext.h`, `unicode/udisplaycontext.h`.
3. **Lecture des prototypes** (`grep -n`) pour fixer les points d'entrée
   **et leurs numéros de ligne** (tableau ci-dessous) ;
   **vérification des bibliothèques** exportant ces symboles (`nm -D
   libWFD.so`) — pour distinguer « en-tête présent » de « API liée » ;
   **compilation réelle** du squelette contre ces en-têtes (`qcc`, §E2
   « Compilation »).

### Le contrat réel : WFD + Wfdcfg

```
Screen (gestionnaire de composition)
     │  OpenWF Display (WFD)
     ▼
driver WFD de Toile  ──►  bibliothèque Wfdcfg (modes/attributs matériel)
```

La documentation du SDP le dit noir sur blanc (plugin
`com.qnx.doc.screen.wfdcfg`, `cwfdcfg_intro.html`) : *« The Wfdcfg
library provides the modes and attributes of your display hardware to
your display driver and Screen »*, et *« Your display driver (WFD
driver) is the primary user of the Wfdcfg library »*. Autrement dit : le
« display driver tiers » de Screen 8.0 **est** un driver WFD (décision
**D17**).

### Points d'entrée vérifiés [MACHINE — `grep -n` sur les en-têtes du SDP]

| Symbole | Fichier:ligne | Rôle |
|---|---|---|
| `wfdEnumerateDevices` | `WF/wfd.h:271` | lister les appareils WFD |
| `wfdCreateDevice` | `WF/wfd.h:276` | ouvrir un appareil |
| `wfdDeviceCommit` | `WF/wfd.h:283` | valider (commit) la configuration |
| `wfdEnumeratePorts` | `WF/wfd.h:328` | lister les ports (sorties) |
| `wfdCreatePort` | `WF/wfd.h:334` | créer un port |
| `wfdSetPortMode` | `WF/wfd.h:360` | fixer le mode (résolution/réFRESH) d'un port |
| `wfdCreatePipeline` | `WF/wfd.h:444` | créer le pipeline de composition |
| `wfdCreateSourceFromImage` | `WF/wfd.h:453` | créer la source depuis une image EGL |
| `wfdBindSourceToPipeline` | `WF/wfd.h:485` | lier la source au pipeline |
| `wfdcfg_device_create` | `wfdqnx/wfdcfg.h:217` | créer un device Wfdcfg |
| `wfdcfg_port_create` | `wfdqnx/wfdcfg.h:272` | créer un port Wfdcfg |
| `wfdcfg_mode_list_create` | `wfdqnx/wfdcfg.h:325` | créer la liste des modes |
| `wfdCreateWFDEGLImagesQNX` | `WF/wfdext.h:393` | allouer les images EGL |
| `wfdCreateDestinationFromImageQNX` | `WF/wfdext.h:878` | créer une destination depuis une image |

### Pistes closes

| Piste | Verdict | Preuve |
|---|---|---|
| `io-display` (modèle QNX 6.x) | **close** | `find` ne trouve ni `io-display*`, ni `disputils*`, ni `devg-*` ; pas de répertoire `ddk/` |
| `screen/screen.h` comme DDK | **close** | c'est l'**API client** de Screen (`screen_create_context` `screen.h:6360`, `screen_register_window_manager` `:6689`, `screen_create_display` `:7566`) — pas un contrat de driver |
| `unicode/udisplaycontext.h` | **close** | en-tête **ICU** (formatage de texte : `enum UDisplayContextType`, `enum UDisplayContext`) — sans rapport avec Screen |

### Ce qui manque pour tester [MACHINE — `find` sur tout le SDP]

| Élément | État dans le SDP installé | Ce qu'il faut pour tester |
|---|---|---|
| `libWFDplatform` | **absent** — seul l'en-tête `WF/wfdplatform.h` est livré (définit `WFDint`, `WFDDevice`, `WFDEGLImage`…) | le BSP « Screen Board Support package » fournissant la couche plateforme de la carte visée |
| `wfd-server` | **absent** — n'existe que comme *documentation* (`com.qnx.doc.screen.wfd-server/topic/manual/`) | le binaire `wfd-server` : sans lui, `wfdEnumerateDevices()` renvoie 0 appareil |
| `libwfdcfg.so` | **absent** — seul l'en-tête `wfdqnx/wfdcfg.h` est livré ; `nm -D` sur tous les `.so` du SDP ne trouve **aucun** export de `wfdcfg_device_create` | l'archive Wfdcfg du BSP |

En revanche, **`libWFD.so` est présent** (`target/qnx/x86_64/usr/lib/`) et
exporte **les 15 symboles** utilisés par le squelette (§E2 « Sortie
brute »). L'API cliente OpenWF Display est donc réellement liée, pas
seulement compilée.

### Sortie brute [MACHINE — 2026-11-12]

```
$ cd ~/qnx800/target/qnx/usr/include
$ ls WF/ wfdqnx/ screen/
wfdext.h
wfd.h
wfdplatform.h

wfdcfg.h

screen_ext.h
screen.h

$ ls ddk
ls: impossible d'accéder à 'ddk': Aucun fichier ou dossier de ce nom
$ find . -name 'io-display*' -o -name 'disputils*' -o -name 'devg-*'
(rien)

$ grep -n "wfdEnumerateDevices\|wfdCreateDevice\|wfdDeviceCommit\|wfdEnumeratePorts\|wfdCreatePort\|wfdSetPortMode\|wfdCreatePipeline\|wfdCreateSourceFromImage\|wfdBindSourceToPipeline" WF/wfd.h
271:    wfdEnumerateDevices(WFDint *deviceIds,
276:    wfdCreateDevice(WFDint deviceId,
283:    wfdDeviceCommit(WFDDevice device,
328:    wfdEnumeratePorts(WFDDevice device,
334:    wfdCreatePort(WFDDevice device,
360:    wfdSetPortMode(WFDDevice device,
444:    wfdCreatePipeline(WFDDevice device,
453:    wfdCreateSourceFromImage(WFDDevice device,
485:    wfdBindSourceToPipeline(WFDDevice device,

$ grep -n "wfdcfg_device_create\|wfdcfg_port_create\|wfdcfg_mode_list_create" wfdqnx/wfdcfg.h
217:wfdcfg_device_create(struct wfdcfg_device **device, int deviceid,
272:wfdcfg_port_create(struct wfdcfg_port **port,
325:wfdcfg_mode_list_create(struct wfdcfg_mode_list **list,

$ grep -n "wfdCreateWFDEGLImagesQNX\|wfdCreateDestinationFromImageQNX" WF/wfdext.h
393:    wfdCreateWFDEGLImagesQNX(WFDDevice device, WFDint width, WFDint height, WFDint format, WFDint usage, WFDint count, WFDEGLImage *images);
878:    wfdCreateDestinationFromImageQNX(WFDDevice device, WFDPort port, WFDEGLImage image, const WFDint *attribList) WFD_APIEXIT;

$ cd ~/qnx800
$ find . -iname 'libWFDplatform*' -o -iname 'wfd-server*' -o -iname 'libwfdcfg*'
./target/qnx/usr/help/eclipse/plugins/com.qnx.doc.screen.wfd-server/images/wfd-server.png
./target/qnx/usr/help/eclipse/plugins/com.qnx.doc.screen.wfd-server/topic/manual/wfd-server-info.html
(aucune bibliothèque, aucun binaire : uniquement de la documentation)
$ ls target/qnx/x86_64/usr/lib | grep -i wfd
libWFD.so
libWFD.so.1
libWFD.so.1.sym
$ nm -D target/qnx/x86_64/usr/lib/libWFD.so | grep -c ' T wfd'
67
(les 15 symboles utilisés par le squelette sont exportés, dont
 wfdEnumerateDevices, wfdCreateDevice, wfdCreatePipeline,
 wfdCreateSourceFromImage, wfdBindSourceToPipeline,
 wfdCreateWFDEGLImagesQNX, wfdCreateDestinationFromImageQNX)

$ for f in target/qnx/x86_64/usr/lib/*.so*; do nm -D "$f" 2>/dev/null | grep -q wfdcfg_device_create && echo "trouvé dans $f"; done
(aucune sortie : wfdcfg_device_create n'est exporté par AUCUNE bibliothèque du SDP)
```

### Compilation du squelette contre l'API réelle [MACHINE — 2026-11-12]

```
$ cd /home/baptiste/Bureau/toile/guest
$ source /home/baptiste/qnx800/qnxsdp-env.sh
$ qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra -Werror \
    -I/home/baptiste/qnx800/target/qnx/usr/include \
    -I/home/baptiste/Bureau/toile/core/include -Icommon -Igpu -Ivirtio -Idisplay \
    -c display/toile_dispskel.c -o build/display/toile_dispskel.o
(aucune sortie — code 0)
$ qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra -Werror \
    -I/home/baptiste/qnx800/target/qnx/usr/include \
    -I/home/baptiste/Bureau/toile/core/include -Icommon -Igpu -Ivirtio -Idisplay \
    -c display/toile_display.c -o build/display/toile_display.o
(aucune sortie — code 0)
$ qcc -V gcc_ntox86_64 build/display/toile_display.o build/display/toile_dispskel.o \
    build/common/toile_pattern.o build/common/toile_gpu_client.o \
    -lscreen -o build/toile-display
(aucune sortie — code 0)
$ file build/toile-display
build/toile-display: ELF 64-bit LSB pie executable, x86-64, version 1 (SYSV),
dynamically linked, interpreter /usr/lib/ldqnx-64.so.2, ... not stripped
```

Le squelette **compile et se lie** contre les en-têtes réels. Il ne peut
pas **tourner** : sans `wfd-server`, `wfdEnumerateDevices()` renvoie 0.

### Ce qui reste à faire sur cible

1. **Obtenir le BSP Screen** (« Screen Board Support package ») qui fournit
   `libWFDplatform`, `libwfdcfg.so` et le `wfd-server` — c'est la condition
   **1** pour que l'intégration soit testable.
2. **Démarrer `wfd-server`** sur la cible et vérifier que
   `wfdEnumerateDevices()` renvoie l'appareil (le greffon `drm-virtio`
   livré par QNX 8.0, ou le pilote Toile lui-même).
3. **Exécuter le greffon WFD de Toile** (`guest/display/toile_dispskel.c`) :
   device Wfdcfg, port, liste de modes, pipeline, source depuis l'image
   EGL, liaison, commit — puis le brancher sur le framebuffer virtio-gpu ;
   la démo `--wfd` (`guest/display/toile_display.c`) échoue proprement et
   le dit en attendant.
4. **Décider du sort de `--screen` / `--fb`** : le chemin Screen reste
   valide (client), `--fb` reste le repli documenté (décision D18).

### Conséquence pour le projet (à porter au cahier des charges §13)

**G2** (« aucun display driver Screen pour virtio-gpu ») et **G3** (« Screen
inutilisable en invité sans passthrough ») sont **à reviser** : QNX 8.0
fournit `drm-virtio` + `gpu_virtio.so` + les allocateurs
`screen-alloc-virtio-*` pour **virgl/gfxstream** (3D). La niche de Toile
n'est donc pas « faire ce que QNX fait déjà », mais **un chemin 2D simple,
sans virgl, dans l'invité** : c'est ce que vise le socle S (scanout unique,
flush 2D) — et c'est précisément ce qui reste non couvert par QNX.
Détail et statut à jour : `docs/GAP.md` §1 et §5, décisions **D17** et **D18**.

---

## E3 — Réutilisabilité du transport virtio de Passerelle

**Question.** Le transport de Passerelle est-il réutilisable tel quel pour
virtio-gpu (files multiples `controlq`/`cursorq`, MSI-X) ? Licence
compatible ?

**Verdict : PARTIELLEMENT TRANCHÉ — réutilisable après un patch de ~100
lignes, dont 6 corrections d'API QNX 8.0 découvertes à la compilation.**

### Réutilisable tel quel [MACHINE — lecture du code de Passerelle]

| Besoin | Preuve (fichier:symbole) |
|---|---|
| Files multiples | `pgw_vq_create(pgw_vio_dev *dev, uint16_t qidx, uint16_t size, bool indirect, bool event_idx)` — générique en `qidx` ; `dev->info.num_queues` lu dans le registre `num_queues` du périphérique (`virtio/src/pci_modern.c:465`) → `controlq` (0) et `cursorq` (1) conviennent |
| MSI-X | `pgw_vio_pci_open(uint16_t device_id, const char *tag, bool allow_msix)` + `pci_msix_program()` dans `pgw_vio_pci_sim_open` ; état dans `pgw_vio_info.msix_active` |
| Soumission scatter-gather avec adresse physique | `pgw_vq_push(vq, const pgw_vio_sg *sg, size_t count, void *cookie)` ; `virtq.c:80` : `sg->phys != 0 ? sg->phys : (uint64_t)(uintptr_t)sg->addr` |
| Mémoire DMA QNX | hook `dev->vq_alloc` = `pgw_qnx_vq_alloc` (`virtio/src/qnx_mem.c` : `mmap(MAP_PHYS|PROT_NOCACHE)` + `mem_offset64`) |
| Négociation / cycle de vie | `pgw_vio_init_device(dev, features)` → ACK → DRIVER → FEATURES_OK → DRIVER_OK |
| Back-end de test sur PC | `pgw_vio_mock_open(device_id, tag, num_queues, host_features, handler, user)` |
| Licence | Apache-2.0 (`passerelle/LICENSE`, en-tête SPDX dans `virtio/include/pgw/virtio.h`) → compatible avec la licence MIT de Toile ; obligations d'attribution remplies par `NOTICE` |

### Ce qui BLOQUE tel quel [MACHINE — lecture du code]

Trois listes blanches codées en dur pour 9P/virtiofs :

| Emplacement | Blocage |
|---|---|
| `virtio/src/pci_qnx.c:169` `pci_device_id_for()` | types 9 et 26 seulement ; le type 16 (GPU) → `0` → `pgw_vio_pci_open` renvoie NULL |
| `virtio/src/pci_modern.c:393` `pci_id_matches()` | DID 0x1009/0x1049/0x105A seulement ; 0x1050 rejeté |
| `virtio/src/pci_modern.c:403` `pci_match_tag()` et `virtio/src/mmio.c:244` `match_tag()` | lisent **inconditionnellement** un « mount tag » 9P/virtiofs ; `pgw_vio_read_tag_fs` échoue (validation `num_request_queues`) → l'ouverture échoue même avec `expected_tag = NULL` |
| `virtio/src/mmio.c:283` | `pgw_vio_mmio_open` rejette tout `DeviceID` ≠ 9/26 |

### Six écarts d'API QNX 8.0 révélés à la compilation [MACHINE — compilation réelle]

Le collage QNX de Passerelle **n'avait jamais été compilé contre un vrai
SDP** (`passerelle/docs/building.md` : « the host PC used for development has
no QNX SDP installed »). En compilant pour de vrai (`qcc -V gcc_ntox86_64`),
six erreurs d'API sont apparues :

| # | Fichier | Erreur réelle | Correction (dans le patch) |
|---|---|---|---|
| 1 | `pci_qnx.c` | `unknown type name 'uint_t'` (`<pci/pci.h>` a besoin de `uint_t`) | `#include <sys/types.h>` avant `<pci/pci.h>` |
| 2 | `pci_qnx.c` | `too few arguments to 'pci_device_cfg_wr32'` | le SDP exige un 4ᵉ argument (`val_p`) → passé à `NULL` (documenté : « Pass NULL for the `<val_p>` parameter ») |
| 3 | `pci_qnx.c` | `pointer targets … differ in signedness` sur `pci_device_read_irq` | le SDP attend `int_t *` → variable passée en `int_t` |
| 4 | `pci_qnx.c` | `PCI_CMD_MEM` / `PCI_CMD_MASTER` undeclared | le SDP ne nomme pas ces bits → définis localement (PCI Local Bus Spec 3.0 §6.2.2 : `0x0002` / `0x0004`) |
| 5 | `qnx_mem.c` | `too few arguments to 'mem_offset64'` | l'API QNX 8.0 est `mem_offset64(addr, fd, len, off, contig)` → le patch passe la longueur |
| 6 | `mmio_qnx.c` | `mmap_device_io()` renvoie un **descripteur** `_Uintptrt`, pas un pointeur ; les registres ne sont plus accessibles par pointeur | **backend MMIO incompatible QNX 8.0** → exclu du build (aucun appelant de `pgw_vio_mmio_open_phys`) |

### Ce qui en est fait

`guest/virtio/toile-virtio-gpu.patch` (100 lignes, en-tête SPDX Apache-2.0,
généré par `diff -ruN` sur une **copie** de l'arbre Passerelle) :
ajoute `PGW_VIRTIO_ID_GPU` (16) / `PGW_VIRTIO_ID_INPUT` (18) et les DID
associés, corrige les trois listes blanches, court-circuite la lecture du tag
pour les périphériques qui n'en ont pas, et corrige les 5 écarts d'API
corrigeables (le 6ᵉ — le MMIO — est documenté plutôt que corrigé).

Application vérifiée : `patch -p1 --dry-run` sur 5 fichiers, sans conflit.

### Conséquence

- **Licence** : compatible (Apache-2.0 → MIT), attribution faite dans
  `NOTICE` + `guest/virtio/README.md`.
- **Candidat à contribution amont (X5)** : le patch est petit, ciblé et sans
  effet de bord sur 9P/virtiofs.
- **Compilation integrée acquise** : le transport **compile et se lie** pour
  QNX x86_64 avec `qcc -V gcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror`,
  0 avertissement (recette exacte dans `guest/README.md`).

---

## E4 — Accès au DDK hyperviseur en licence non commerciale

**Question.** L'accès au DDK hyperviseur QNX est-il permis sous licence non
commerciale (QNX Everywhere) ?

**Verdict : TRANCHÉ, NÉGATIF sur cette machine.** Le DDK hyperviseur n'est
**ni installé**, **ni couvert** par la licence installée. Le volet B (X1) est
donc bloqué ; le volet A (QEMU) n'en dépend pas (cahier §12, décision D3).

### Méthode employée

1. **Compter les paquets installés** : `ls .packages/metadata/ | wc -l` → 225 ;
   **chercher un paquet hyperviseur / vdev** dans ces métadonnées
   (`grep -Ei 'hyper|vdev'`) → **aucune correspondance**.
2. **Chercher les en-têtes et bibliothèques vdev** dans tout le SDP
   (`find -iname 'vdev_*' -o -iname 'libvdev*' -o -iname '*hypervisor*'`) →
   **rien** hors `/help/`.
3. **Lire la licence installée** (`qlicense -info`) et ses **fonctionnalités** ;
   **vérifier l'absence de DDK** (`ls .../usr/include/ddk`).

### Preuves [MACHINE]

```
$ ls ~/qnx800/.packages/metadata/ | wc -l
225
$ ls ~/qnx800/.packages/metadata/ | grep -Ei 'hyper|vdev'
(aucune correspondance)
$ ls ~/qnx800/.packages/metadata/ | grep -i virt
com.qnx.qnx800.target.net.devsvirtio
com.qnx.qnx800.target.qemuvirt
(les deux seules entrées « virt » : le pilote réseau virtio et l'image QEMU — pas un hyperviseur)
$ find ~/qnx800 -iname 'vdev_*' -o -iname 'libvdev*' -o -iname '*hypervisor*' | grep -v /help/
(aucune sortie)

$ find ~/qnx800 -iname '*vdev*'
/home/baptiste/qnx800/target/qnx/usr/help/eclipse/plugins/com.qnx.doc.neutrino.utilities/topic/m/mods-vdevpeer-net.so.html
(une seule occurrence : une page d'aide sur un module réseau, pas un framework vdev)

$ ls ~/qnx800/target/qnx/usr/include/ddk
ls: impossible d'accéder à '.../usr/include/ddk': Aucun fichier ou dossier de ce nom
$ source ~/qnx800/qnxsdp-env.sh && qlicense -info
CN52-415Z-T3YL-2B9S-75E7 (SN: 10384620)
  QNX Software Development Platform 8.0 Education License
  Features: SDK | GPL Utils | IDE
  License key does not require remote activation
$ qlicense -info | grep -i hypervisor
(aucune correspondance)
```

Lecture : la licence installée est une **Education License** dont les
fonctionnalités sont `SDK | GPL Utils | IDE` — **aucune fonctionnalité
Hypervisor**. Le texte de licence lui-même n'est pas lisible dans le SDP
(le paquet `eula.qdl` ne contient que des métadonnées :
`AssetDescriptor.json`, `assets.list`, `buildinfo`), mais l'absence de la
fonctionnalité dans la licence suffit à trancher : le DDK hyperviseur et
les vdev ne sont **ni installés (225 paquets, aucun), ni couverts par la
licence**.

### Ce qu'il faudrait pour débloquer

1. une licence QNX incluant l'hyperviseur (SDK « QNX Hypervisor ») ;
2. les en-têtes du framework vdev et les vdev livrés comme référence ;
3. un hôte sous **QNX Hypervisor** (pas QEMU).

Tant que ce n'est pas le cas, `host-qnx/vdev_gpu.c` reste un squelette
documenté (`// E4: à confirmer` partout).

### Portée du verdict

Le verdict porte sur **cette machine et cette licence** (SDP 8.0 Education
License) : le DDK hyperviseur est inaccessible **dans le périmètre du
concours**, et c'est ce périmètre qui décide du repli (décision D3 : volet A
d'abord).

## E5 (bonus) — Performances d'un flush 2D

**Non mesuré.** La mesure suppose le pilote en fonctionnement dans l'invité
(le flush n'a pas encore été exécuté sur cible) et un compteur d'images/s
dans la boucle de présentation ; à faire après le premier démarrage réel du
pilote. D'ici là, aucune affirmation de performance n'est faite.

---

## Récapitulatif et conséquences

| Lot | Conséquence de ces essais |
|---|---|
| L4 (transport + énumération) | **débloqué et compilant** ; le transport est celui de Passerelle + patch amont ; l'énumération PCI est **prouvée** (E1) |
| L5 (controlq, ressources, scanout, flush) | code écrit et lié ; **reste à exécuter dans l'invité** |
| L6 (display driver) | **débloqué sur le plan de l'interface** : le contrat réel est **OpenWF Display (WFD) + Wfdcfg** (pas `io-display`), en-têtes relus et squelette compilé ; **l'intégration reste non testée** (`libWFDplatform`, `wfd-server`, `libwfdcfg.so` hors SDP libre) ; livrable de repli : client Screen + framebuffer direct `/dev/gpu0` (décision D18) |
| L7 (image + scripts QEMU + démo) | **images construites** (`mkifs` OK) ; **scripts QEMU écrits et testés** ; l'image graphique démarre-t-elle ? à valider |
| L11 (volet B) | **bloqué par E4** (licence sans fonctionnalité Hypervisor + DDK absent des 225 paquets installés) — périmètre : cette licence |
| §13 du cahier des charges | **G2/G3 à corriger** : QNX 8.0 fournit déjà `drm-virtio` + `gpu_virtio.so` + allocateurs `screen-alloc-virtio-*` (virgl/gfxstream) ; la niche de Toile devient « 2D simple, sans virgl, dans l'invité ». **L'interface display driver est désormais vérifiée** : OpenWF Display (WFD) + Wfdcfg, en-têtes `WF/wfd.h` / `WF/wfdext.h` / `wfdqnx/wfdcfg.h` du SDP 8.0 ; intégration non testée faute de DDK hors licence |
