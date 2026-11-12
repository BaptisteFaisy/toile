/* SPDX-License-Identifier: MIT */
/*
 * Toile — squelette de display driver Screen (S7).
 *
 * // E2: à confirmer — L'INTERFACE EXACTE DU DISPLAY DRIVER N'EST PAS
 * // E2: à confirmer — CONFIRMÉE SUR CETTE MACHINE.  Voici ce qui a été
 * // E2: à confirmer — vérifié dans le SDP QNX 8.0 installé le 2026-11-12 :
 *
 *  1. Aucun DDK de display driver n'est présent dans le SDP :
 *       - pas de répertoire /usr/include/ddk (vérifié : absent) ;
 *       - pas d'en-tête io-display ni de « disputils.h » dans
 *         /usr/include (vérifié : aucun fichier de ce nom) ;
 *       - pas de binaire io-display ni de pilote devg-* dans
 *         target/qnx/x86_64 (vérifié : absent) ;
 *       - les paquets installés sont screen.base, screen.device_input,
 *         screen.kalman, screen.video_capture, graphics.group — c'est-à-dire
 *         le serveur Screen et ses greffons, pas un DDK de driver.
 *  2. Le modèle graphique de QNX 8.0 est basé sur DRM : la documentation
 *     installée (usr/help/eclipse/plugins/com.qnx.doc.screen/topic/manual/
 *     rscreen_drm.html) décrit les serveurs « drm-rpi4 », « drm-virtio »
 *     et « drm-vmwgfx », avec leurs options (DRM_CMDLINE, modules
 *     pci_cap-*, etc.).
 *  3. L'image QNX Everywhere (QSTI) installée dans le SDP contient
 *     lib/gpu_virtio.so, lib/screen-alloc-virtio-virgl.so,
 *     lib/screen-alloc-virtio-gfxstream.so, lib/libwfdcfg-virtio.so :
 *     les greffons virtio de QNX 8.0 existent donc déjà pour virgl et
 *     gfxstream (proof: system.build de l'image quickstart).
 *
 * CONSÉQUENCE : le contrat d'écriture d'un display driver tiers n'est pas
 * vérifiable ici (ni en-têtes, ni documentation du guide développeur de
 * l'écran).  Ce fichier est donc un SQUELETTE : il ne prétend PAS
 * implémenter une API QNX, il fixe la liste des points à confirmer et la
 * structure attendue (une tête, un framebuffer, un flip, une synchronisation
 * verticale) pour que le travail reprenne dès que le DDK sera disponible.
 *
 * Le chemin qui fonctionne TODAY sans Display Driver tiers est le mode
 * « --fb » de toile_display.c : le pilote /dev/gpu0 expose le framebuffer
 * virtio-gpu et une application peint directement dedans.
 */
#include <stdint.h>
#include <stdio.h>

/* E2: à confirmer — l'entrée réelle du display driver QNX 8.0 (nom du
 * symbole exporté, prototype, ordre d'appel) n'est pas documentée dans le
 * SDP installé.  Les points ci-dessous sont l'interface que Toile VEUT
 * utiliser ; ils ne sont pas une API vérifiée. */
typedef struct toile_disp_head {
    uint32_t width;      /* pixels */
    uint32_t height;
    uint32_t format;     /* toile_fmt (CONTRACT.md §3) */
    uint32_t buffers;    /* number of framebuffers (double buffering = 2) */
} toile_disp_head;

/* E2: à confirmer — « flip » = présenter le buffer N à la tête (equivalent
 * virtio-gpu : TRANSFER_TO_HOST_2D puis RESOURCE_FLUSH). */
typedef struct toile_disp_ops {
    int (*init)(void);
    int (*set_mode)(const toile_disp_head *head);
    int (*flip)(uint32_t buffer_id);
    int (*wait_vsync)(void);
    void (*fini)(void);
} toile_disp_ops;

/* E2: à confirmer — où vivent ces points d'entrée (module .so chargé par
 * le serveur drm-*, ou greffon io-display) et comment le serveur les
 * découvre (fichier de configuration, variable d'environnement). */
static const toile_disp_ops g_ops = {
    NULL, NULL, NULL, NULL, NULL
};

void toile_dispskel_report(void)
{
    printf("toile-dispskel: squelette de display driver (E2 non tranche)\n");
    printf("  interface verifiee : aucune (pas de DDK dans le SDP local)\n");
    printf("  modele reel QNX 8.0 : serveurs drm-* (drm-virtio, drm-rpi4)\n");
    printf("  mode de repli livre : /dev/gpu0 + client direct (toile-display --fb)\n");
    printf("  points d'entree attendus (NON verifies) : init/set_mode/flip/wait_vsync\n");
    printf("  ops renseignes : %d\n", g_ops.init != NULL ? 1 : 0);
}
