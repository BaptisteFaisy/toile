# host-qnx — volet B (niveau X1) : vdev virtio-gpu côté hôte QNX Hypervisor

**Verdict E4 : NÉGATIF sur cette machine.** Le DDK hyperviseur de QNX n'est ni
installé ni couvert par la licence présente dans le SDP local.  Le volet B est
donc **bloqué** ; le volet A (QEMU, `qemu/` + `guest/`) n'en a pas besoin et
reste la voie livrable au 15/12 (décision D3 du cahier des charges).

## Preuves relevées le 2026-11-12 (SDP `~/qnx800`)

| Vérification | Commande | Résultat |
|---|---|---|
| Paquets hyperviseur installés | `ls ~/qnx800/.packages/metadata/ \| grep -Ei 'hyper\|vdev'` | **aucun** (sur 225 paquets) ; les seules entrées « virt » sont `net.devsvirtio` (pilotes réseau) et `qemuvirt` (image QSTI) |
| En-têtes vdev/hyperviseur | `find ~/qnx800 -iname '*vdev*'` | une seule page de documentation : `target/qnx/usr/help/eclipse/plugins/com.qnx.doc.neutrino.utilities/topic/m/mods-vdevpeer-net.so.html` |
| Répertoire DDK | `ls ~/qnx800/target/qnx/usr/include/ddk` | « Aucun fichier ou dossier de ce nom » |
| Bibliothèques vdev | `find ~/qnx800/target/qnx -name '*vdev*' -o -name '*hypervisor*'` | **rien** |
| Licence installée | `source ~/qnx800/qnxsdp-env.sh && qlicense -info` | `CN52-415Z-T3YL-2B9S-75E7 (SN: 10384620)` — **QNX Software Development Platform 8.0 Education License** ; `Features: SDK \| GPL Utils \| IDE` ; « License key does not require remote activation » |

La licence ne liste donc **aucune fonctionnalité Hypervisor** : même si les
en-têtes étaient obtenus séparément, leur usage ne serait pas couvert par la
licence non commerciale installée ici.

## Ce qui existe quand même

`vdev_gpu.c` est un **squelette documenté** : il ne contient aucun appel à une
API hyperviseur (il ne compilerait pas, faute d'en-têtes), il fixe la structure
que Toile veut fournir — configuration annoncée à l'invité (mode, nombre de
têtes), callbacks `controlq`/`cursorq`, effet « hôte » `present()` — et la liste
des points à confirmer (`// E4: à confirmer`).

Il compile et s'exécute tel quel (il affiche son état) :

```sh
source ~/qnx800/qnxsdp-env.sh
qcc -V gcc_ntox86_64 -std=gnu11 -Wall -Wextra -Werror host-qnx/vdev_gpu.c -o /tmp/toile-vdev-gpu
/tmp/toile-vdev-gpu   # sur cible ; ou simplement le lire
```

## Ce qu'il faudrait pour débloquer le volet B

1. Une licence QNX incluant l'hyperviseur (le DDK hyperviseur et les
   bibliothèques `vdev`), ou un accès au SDK « QNX Hypervisor » ;
2. Les en-têtes du framework vdev (`vdev_*`, bus virtuel, accès à la mémoire
   invitée) et les ~20 vdev livrés comme référence ;
3. Un hôte QNX (pas QEMU) : le volet B s'exécute sous QNX Hypervisor, avec
   une VM invitée QNX.

Jusque-là, la démonstration de Toile passe par le volet A : QEMU fournit déjà
le périphérique `virtio-gpu-pci`, et l'invité QNX l'énumère (essai **E1 :
positif**, voir `docs/TRIALS-E1-E4.md`).
