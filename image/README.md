# image/ — images QNX invitées (mkifs)

Deux buildfiles, deux usages :

| Fichier | Rôle | État |
|---|---|---|
| `toile-e1.build` | IFS **minimal de sonde** : noyau + serveur PCI + `pci-tool` + shell série. Sert à l'essai E1 (l'invité voit-il le périphérique virtio-gpu ?) | **construit et démarré** le 2026-11-12 (4 137 028 octets) |
| `toile.build` | IFS **graphique** : noyau + serveur PCI + pilote Toile (`/dev/gpu0`) + serveur Screen + démo (mire) | **construit** le 2026-11-12 (7 929 924 octets) ; démarrage sur cible à valider |

## Prérequis

1. **Une licence QNX 8.0** (gratuite en usage non commercial : QNX
   Everywhere, `https://www.qnx.com/getqnx`) et le **SDP QNX 8.0 installé**
   (ici : `~/qnx800`).  La licence installée sur cette machine est une
   *QNX Software Development Platform 8.0 Education License*
   (fonctionnalités `SDK | GPL Utils | IDE`, vérifié par `qlicense -info`).
2. Les outils `mkifs` et `qcc` du SDP, mis dans le `PATH` par
   `source ~/qnx800/qnxsdp-env.sh`.
3. Pour `toile.build` : les binaires du pilote et de la démo, construits
   avant (`make -f guest/common.mk`), car le buildfile les cherche dans
   `guest/build` (chemin relatif → **lancer mkifs depuis la racine du
   dépôt**).

## Construction

```sh
source ~/qnx800/qnxsdp-env.sh
cd /home/baptiste/Bureau/toile

# sonde E1 (aucun binaire Toile requis)
mkifs image/toile-e1.build /tmp/toile-e1.ifs

# image graphique (nécessite guest/build/toile-gpu et guest/build/toile-display)
make -f guest/common.mk
mkifs image/toile.build /tmp/toile.ifs
```

## Démarrage

```sh
qemu-system-x86_64 --enable-kvm --cpu host -m 2048 \
    -kernel /tmp/toile.ifs -vga none -device virtio-gpu-pci \
    -display none -serial mon:stdio
```

`-kernel` charge l'IFS en multiboot : **aucun disque n'est nécessaire** pour
ces deux images (tout tient en RAM).  Pour une image avec disque
(`/system`, `/data`), voir l'image QSTI du SDP
(`~/qnx800/images/qemu/unpack_qemu_image.sh`).

## Points techniques vérifiés (et pièges évités)

- **Les dépendances partagées ne sont PAS résolues automatiquement** par
  mkifs : sans les `.so` listés explicitement, le démarrage échoue avec
  `Unable to start "<binaire>" (83)` — 83 = `ELIBACC` (« Can't access shared
  library », `errno.h:142`).  La liste des `NEEDED` a été relevée avec
  `readelf -d` sur chaque binaire (`libc.so.6`, `libgcc_s.so.1`,
  `libsecpol.so.1`, `libpci.so.3.0`, `libsocket.so.4`, `libslog2.so.1`,
  `libscrmem.so.1`, `libmemobj.so.1`, `libcam.so.2`, `libWFD.so.1`) et
  `ldqnx-64.so.2` doit être inclus **en plus** du lien symbolique.
- **Le serveur PCI a besoin d'un module matériel** : sans
  `PCI_HW_MODULE=pci_hw-Intel_x86.so` (déclaré dans `pci_server.cfg`),
  `pci-tool` ne liste rien et `devinfo` échoue.  Contenu des deux fichiers
  de configuration repris de l'image QSTI installée
  (`~/qnx800/images/qemu/qemu/output/build/ifs.build`, lignes 134-160).
- **`ls`, `cat`, `echo`, `sleep` n'existent pas** comme binaires dans le
  SDP 8.0 cible : les scripts embarqués utilisent `ksh` et ses internes.
- Le shell série est lancé par `on -d -t /dev/ser1 ksh -l` (recette de
  l'image QSTI, `startup.sh` ligne 22) ; `devc-ser8250 -e` crée `/dev/ser1`.
