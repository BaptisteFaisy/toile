# guest/display — affichage dans l'invité (S7)

Objectif S7 : **un framebuffer, une tête, affichage d'une mire statique puis
du bureau**. Ce dossier contient deux choses, très différentes par nature :

| Fichier | Nature | État |
|---|---|---|
| `toile_display.c` | **client Screen** : crée une fenêtre dans le serveur Screen qui tourne dans l'invité, y peint la mire statique, puis publie le buffer (`screen_post_window`). Chemin `--fb` : accès direct au framebuffer exposé par `/dev/gpu0` (repli documenté, cahier des charges §12) | **compilé et vérifié** (API lue dans `screen/screen.h` du SDP installé) |
| `toile_dispskel.c` | **squelette de display driver tiers** : la liste des points d'entrée que Toile voudrait fournir à Screen | **squelette assumé**, tout est marqué `// E2: à confirmer` |

## E2 — ce qui a été vérifié sur cette machine (SDP QNX 8.0)

| Fait vérifié | Preuve (chemin exact) |
|---|---|
| Aucun DDK de display driver dans le SDP | pas de `/usr/include/ddk` ; aucun `disputils.h` ni `io-display` dans `/usr/include` ; pas de `io-display` ni `devg-*` dans `target/qnx/x86_64` |
| Le modèle graphique de QNX 8.0 est basé sur **DRM** | `usr/help/eclipse/plugins/com.qnx.doc.screen/topic/manual/rscreen_drm.html` : serveurs `drm-rpi4`, **`drm-virtio`**, `drm-vmwgfx` |
| QNX 8.0 livre déjà des greffons virtio pour Screen | image QSTI : `lib/gpu_virtio.so`, `lib/screen-alloc-virtio-virgl.so`, `lib/screen-alloc-virtio-gfxstream.so`, `lib/libwfdcfg-virtio.so` (`system.build` de `~/qnx800/images/qemu`) |
| L'API **client** Screen est disponible et connue | `usr/include/screen/screen.h` (507 Ko) : `screen_create_context` (l. 6360), `screen_create_window` (9782), `screen_set_window_property_iv` (10543), `screen_create_window_buffers` (9852), `screen_get_window_property_pv` (10167), `screen_get_buffer_property_pv` (6172), `screen_get_buffer_property_iv` (6124), `screen_post_window` (10359), `SCREEN_FORMAT_RGBX8888 = 9` (3958) |

## E2 — ce qui reste à confirmer

- Le **contrat exact d'un display driver tiers** (points d'entrée, ABI du
  module, mode de découverte par le serveur `drm-*`) : non documenté dans le
  SDP installé, aucun en-tête disponible.
- Si `drm-virtio` (fourni par QNX) suffit à la démo — auquel cas le besoin
  d'un driver Toile se réduit à un pilote **2D simple, sans virgl** : c'est
  précisément la niche de Toile (voir `docs/TRIALS-E1-E4.md` §E2).
- La voie de partage du framebuffer avec Screen (callout `mmap` du
  resource manager, ou mémoire objet partagée) : le mode `--fb` utilise
  aujourd'hui `MAP_PHYS` côté client (démonstration root), ce qui n'est pas
  la voie de production.

## Compilation

```sh
source ~/qnx800/qnxsdp-env.sh
make -f common.mk display        # -> build/toile-display
```

`toile_display` se lie avec `-lscreen` (`libscreen.so` présent dans
`target/qnx/x86_64/usr/lib`).
