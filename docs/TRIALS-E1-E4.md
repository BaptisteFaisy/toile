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
| **E2** | Screen accepte-t-il un display driver tiers, et quel est le contrat exact ? | **NON TRANCHÉ** — DDK absent du SDP ; modèle DRM documenté ; chemin de repli livré |
| **E3** | Le transport virtio de Passerelle est-il réutilisable tel quel pour virtio-gpu ? | **PARTIELLEMENT TRANCHÉ** — cœur réutilisable ; 3 listes blanches + 6 écarts d'API QNX 8.0 corrigés par un patch de 100 lignes |
| **E4** | Le DDK hyperviseur est-il accessible en licence non commerciale ? | **TRANCHÉ — NÉGATIF** (absent du SDP **et** non couvert par la licence installée) |
| **E5** | 15–20 images/s en 1080p via un flush 2D virtio-gpu ? | **NON MESURÉ** (hors périmètre de ce lot) |

---

## E1 — Énumération PCI du périphérique virtio-gpu dans l'invité QNX

**Question.** Un invité QNX 8.0 sous QEMU (`-device virtio-gpu-pci`)
énumère-t-il le périphérique sur son bus PCI ?

**Verdict : TRANCHÉ, POSITIF.** L'essai a été **exécuté** le 2026-11-12 :
l'invité QNX 8.0 voit le périphérique `1af4/1050` (virtio-gpu moderne) sur
son bus PCI, en `B3:D0:F0`, avec le code de classe `03/80/00`.

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

**Verdict : NON TRANCHÉ.** L'interface n'est pas documentable sur cette
machine : le DDK de display driver **n'existe pas dans le SDP installé**.
Ce qui **est** établi, c'est le modèle graphique réel de QNX 8.0 et le fait
que QNX 8.0 livre déjà des greffons virtio pour Screen.

### Faits vérifiés [MACHINE]

| Fait | Preuve |
|---|---|
| Aucun DDK de display driver dans le SDP | `ls ~/qnx800/target/qnx/usr/include/ddk` → « Aucun fichier ou dossier de ce nom » ; `find ~/qnx800/target/qnx -name 'io-display*' -o -name 'devg-*' -o -name '*disputils*'` → **rien** |
| Le serveur Screen et son API client sont présents | `~/qnx800/target/qnx/usr/include/screen/{screen.h,screen_ext.h}` ; `target/qnx/x86_64/sbin/screen`, `usr/lib/libscreen.so`, `lib/dll/screen-sw.so` |
| Le modèle graphique de QNX 8.0 est **DRM** | `~/qnx800/target/qnx/usr/help/eclipse/plugins/com.qnx.doc.screen/topic/manual/rscreen_drm.html` : serveurs `drm-rpi4`, **`drm-virtio`**, `drm-vmwgfx` (options `-o`, `DRM_CMDLINE`, modules `pci_cap-*`) |
| QNX 8.0 livre déjà des greffons virtio pour Screen | image QSTI installée (`~/qnx800/images/qemu/qemu/output/build/system.build`) : `lib/gpu_virtio.so`, `lib/screen-alloc-virtio-virgl.so`, `lib/screen-alloc-virtio-gfxstream.so`, `lib/libwfdcfg-virtio.so`, `lib/libWFDvirtio.so`, `lib/virtio_dri.so`, `lib/vulkan_virtio.so` |

### Conséquence pour le projet (à porter au cahier des charges §13)

Les affirmations **G2** (« aucun display driver Screen pour virtio-gpu ») et
**G3** (« Screen inutilisable en invité sans passthrough ») sont **à
reviser** : QNX 8.0 fournit `drm-virtio` + `gpu_virtio.so` + les allocateurs
`screen-alloc-virtio-*` pour **virgl/gfxstream** (3D). La niche de Toile
n'est donc pas « faire ce que QNX fait déjà », mais **un chemin 2D simple,
sans virgl, dans l'invité** : c'est ce que vise le socle S (scanout unique,
flush 2D) — et c'est précisément ce qui reste non couvert par QNX.

### Points `// E2: à confirmer` encore ouverts

1. Le contrat d'écriture d'un module de display driver QNX 8.0 (points
   d'entrée, ABI, mode de découverte par le serveur `drm-*`).
2. La voie de partage du framebuffer entre le pilote `/dev/gpu0` et Screen
   (callout `mmap` du resource manager ? mémoire objet partagée ?).
3. Le rôle exact des greffons `libwfdcfg-virtio.so` (configuration WFD) et
   `screen-alloc-virtio-virgl.so` (allocation) dans la chaîne DRM.
4. Si `drm-virtio` suffit pour la démo (auquel cas Toile se réduit à un
   pilote 2D, ce qui reste utile et démontrable).

### Ce qui est livré malgré tout

- `guest/display/toile_display.c` : **client** Screen (API lue dans
  `screen.h`, chaque symbole cité en commentaire) qui peint la mire dans une
  fenêtre, + mode `--fb` : accès direct au framebuffer via `/dev/gpu0`
  (repli documenté au cahier §12). Compilé et lié (`-lscreen`).
- `guest/display/toile_dispskel.c` : squelette du driver tiers, entièrement
  marqué `// E2: à confirmer`, qui ne prétend implémenter aucune API QNX.

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

### Preuves [MACHINE]

```
$ ls ~/qnx800/.packages/metadata/ | grep -Ei 'hyper|vdev'
(comptage : 225 paquets installés — aucun paquet hyperviseur/vdev ;
 les seules entrées « virt » sont net.devsvirtio et qemuvirt)

$ find ~/qnx800 -iname '*vdev*'
/home/baptiste/qnx800/target/qnx/usr/help/eclipse/plugins/com.qnx.doc.neutrino.utilities/topic/m/mods-vdevpeer-net.so.html
(aucune en-tête, aucune bibliothèque)

$ ls ~/qnx800/target/qnx/usr/include/ddk
ls: impossible d'accéder à '.../usr/include/ddk': Aucun fichier ou dossier de ce nom

$ source ~/qnx800/qnxsdp-env.sh && qlicense -info
CN52-415Z-T3YL-2B9S-75E7 (SN: 10384620)
  QNX Software Development Platform 8.0 Education License
  Features: SDK | GPL Utils | IDE
  License key does not require remote activation
```

La licence installée est une **Education License** dont les fonctionnalités
sont `SDK | GPL Utils | IDE` : **aucune fonctionnalité Hypervisor**. Le texte
de licence lui-même n'est pas lisible dans le SDP (le paquet `eula.qdl` ne
contient que des métadonnées), mais l'absence de la fonctionnalité dans la
licence suffit à trancher.

### Ce qu'il faudrait pour débloquer

1. une licence QNX incluant l'hyperviseur (SDK « QNX Hypervisor ») ;
2. les en-têtes du framework vdev et les vdev livrés comme référence ;
3. un hôte sous **QNX Hypervisor** (pas QEMU).

Tant que ce n'est pas le cas, `host-qnx/vdev_gpu.c` reste un squelette
documenté (`// E4: à confirmer` partout).

---

## E5 (bonus) — Performances d'un flush 2D

**Non mesuré.** La mesure suppose le pilote en fonctionnement dans l'invité
(le flush n'a pas encore été exécuté sur cible) et un compteur d'images/s
dans la boucle de présentation. À faire après le premier démarrage réel du
pilite ; d'ici là, aucune affirmation de performance n'est faite.

---

## Récapitulatif et conséquences

| Lot | Conséquence de ces essais |
|---|---|
| L4 (transport + énumération) | **débloqué et compilant** ; le transport est celui de Passerelle + patch amont ; l'énumération PCI est **prouvée** (E1) |
| L5 (controlq, ressources, scanout, flush) | code écrit et lié ; **reste à exécuter dans l'invité** |
| L6 (display driver) | **bloqué sur E2** : l'interface tiers n'est pas vérifiable ici ; livrable de repli : client Screen + framebuffer direct `/dev/gpu0` |
| L7 (image + scripts QEMU + démo) | **images construites** (`mkifs` OK) ; **scripts QEMU écrits et testés** ; l'image graphique démarre-t-elle ? à valider |
| L11 (volet B) | **bloqué par E4** (licence + DDK absents) |
| §13 du cahier des charges | **G2/G3 à corriger** : QNX 8.0 fournit déjà `drm-virtio` + `gpu_virtio.so` + allocateurs `screen-alloc-virtio-*` (virgl/gfxstream) ; la niche de Toile devient « 2D simple, sans virgl, dans l'invité » |
