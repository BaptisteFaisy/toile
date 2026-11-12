# guest/display — affichage dans l'invité (S7)

Objectif S7 : **un framebuffer, une tête, affichage d'une mire statique puis
du bureau**. Ce dossier contient deux choses, très différentes par nature :

| Fichier | Nature | État |
|---|---|---|
| `toile_display.c` | **démonstrateur** à trois chemins : `--screen` (client Screen : crée une fenêtre dans le serveur Screen qui tourne dans l'invité, y peint la mire statique, puis publie le buffer), `--fb` (accès direct au framebuffer exposé par `/dev/gpu0`, **repli officiel** — décision D18), `--wfd` (session OpenWF Display) | **compilé et lié** (`-lscreen`, 0 avertissement) ; le chemin `--wfd` échoue proprement et le dit, faute de pile serveur WFD |
| `toile_dispskel.c` | **squelette du display driver tiers**, écrit contre l'**API réelle** : OpenWF Display (WFD) + Wfdcfg. Aucun appel « maison » | **compilé et lié** ; l'**intégration n'est pas testée** (voir « ce qui manque ») |

## E2 — l'interface réelle : OpenWF Display (WFD) + Wfdcfg

Ce n'est **pas** `io-display` (modèle QNX 6.x, absent du SDP 8.0). Le contrat
d'un display driver tiers de Screen en QNX 8.0 est un **driver WFD** couplé
à la **bibliothèque Wfdcfg** :

```
Screen (gestionnaire de composition)
     │  OpenWF Display (WFD)
     ▼
driver WFD de Toile  ──►  bibliothèque Wfdcfg (modes/attributs matériel)
```

La documentation du SDP le dit : *« Your display driver (WFD driver) is the
primary user of the Wfdcfg library »* (plugin `com.qnx.doc.screen.wfdcfg`).

### Ce qui a été vérifié sur cette machine (SDP QNX 8.0 installé)

| Fait vérifié | Preuve (chemin exact) |
|---|---|
| **Aucun DDK `io-display`** dans le SDP | pas de `/usr/include/ddk` ; aucun `io-display*`, `disputils*` ni `devg-*` dans `/usr/include` |
| **Les en-têtes WFD sont là et compilent** | `WF/wfd.h`, `WF/wfdext.h`, `WF/wfdplatform.h`, `wfdqnx/wfdcfg.h` — compilation réelle par `qcc`, 0 warning |
| **Les points d'entrée** | `wfdEnumerateDevices` (`wfd.h:271`), `wfdCreateDevice` (`:276`), `wfdDeviceCommit` (`:283`), `wfdEnumeratePorts` (`:328`), `wfdCreatePort` (`:334`), `wfdSetPortMode` (`:360`), `wfdCreatePipeline` (`:444`), `wfdCreateSourceFromImage` (`:453`), `wfdBindSourceToPipeline` (`:485`) ; `wfdcfg_device_create` (`wfdcfg.h:217`), `wfdcfg_port_create` (`:272`), `wfdcfg_mode_list_create` (`:325`) ; `wfdCreateWFDEGLImagesQNX` (`wfdext.h:393`), `wfdCreateDestinationFromImageQNX` (`:878`) |
| **`libWFD.so` est présent** | `target/qnx/x86_64/usr/lib/libWFD.so` ; `nm -D` confirme l'export des 15 symboles utilisés |
| **L'API cliente Screen est disponible et connue** | `usr/include/screen/screen.h` : `screen_create_context` (l. 6360), `screen_create_window` (9782), `screen_set_window_property_iv` (10543), `screen_create_window_buffers` (9852), `screen_get_window_property_pv` (10167), `screen_get_buffer_property_pv` (6172), `screen_get_buffer_property_iv` (6124), `screen_post_window` (10359), `SCREEN_FORMAT_RGBX8888 = 9` (3958) |

## E2 — ce qui manque pour **tester** (intégration non testée)

| Élément | État dans le SDP | Ce qu'il faut |
|---|---|---|
| `libWFDplatform` | **absent** — seul l'en-tête `WF/wfdplatform.h` est livré (il définit `WFDint`, `WFDDevice`, `WFDEGLImage`…) | le BSP « Screen Board Support package » fournissant la couche plateforme de la carte visée |
| `wfd-server` | **absent** — n'existe que comme documentation (`com.qnx.doc.screen.wfd-server/`) | le binaire `wfd-server` ; sans lui `wfdEnumerateDevices()` renvoie 0 appareil et le chemin `--wfd` s'arrête net |
| `libwfdcfg.so` | **absent** — seul l'en-tête `wfdqnx/wfdcfg.h` est livré ; `nm -D` sur tous les `.so` du SDP ne trouve **aucun** export de `wfdcfg_device_create` | l'archive Wfdcfg du BSP |

C'est pourquoi les points d'entrée WFD et Wfdcfg sont résolus par
`dlopen`/`dlsym` : c'est la seule façon de **lier** sans `libwfdcfg.so`, et
cela permet au binaire de dire précisément ce qui manque au lieu d'échouer au
chargement.

## Ce qui reste à confirmer sur cible

- **La voie de partage du framebuffer** entre le pilote `/dev/gpu0` et la
  source WFD : le mode `--fb` utilise aujourd'hui `MAP_PHYS` côté client
  (démonstration root), ce qui n'est pas la voie de production.
- **Le mode exact du port** (`wfdSetPortMode` attend un `WFDPortMode`) :
  il dépend du matériel cible, il est laissé à 0 tant que le BSP n'est pas là.
- **Si `drm-virtio` (fourni par QNX) suffit** à la démo — auquel cas le
  besoin d'un driver Toile se réduit à un pilote **2D simple, sans virgl** :
  c'est précisément la niche de Toile (voir `docs/TRIALS-E1-E4.md` §E2).

## Compilation

```sh
source ~/qnx800/qnxsdp-env.sh
make -f common.mk display        # -> build/toile-display
```

Recette exacte, exécutée le 2026-11-12 :

```sh
qcc -V gcc_ntox86_64 -std=gnu11 -O2 -Wall -Wextra -Werror \
    -I/home/baptiste/qnx800/target/qnx/usr/include \
    -I/home/baptiste/Bureau/toile/core/include -Icommon -Igpu -Ivirtio -Idisplay \
    -c display/toile_dispskel.c -o build/display/toile_dispskel.o

qcc -V gcc_ntox86_64 build/display/toile_display.o build/display/toile_dispskel.o \
    build/common/toile_pattern.o build/common/toile_gpu_client.o \
    -lscreen -o build/toile-display
```

`toile_display` se lie avec `-lscreen` (`libscreen.so` présent dans
`target/qnx/x86_64/usr/lib`). Aucune modification de `guest/common.mk` n'a
été nécessaire : les symboles WFD sont résolus à l'exécution.
