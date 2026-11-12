/* SPDX-License-Identifier: MIT */
/*
 * Toile — volet B (niveau X1) : squelette de vdev virtio-gpu côté HÔTE
 * QNX Hypervisor.
 *
 * // E4: à confirmer — LE DDK HYPERVISEUR N'EST PAS DISPONIBLE SUR CETTE
 * // E4: à confirmer — MACHINE.  Preuves relevées le 2026-11-12 dans le SDP
 * // E4: à confirmer — QNX 8.0 installé (~/qnx800) :
 * //   - aucun paquet hyperviseur installé (225 paquets dans
 * //     .packages/metadata/, aucun ne contient « hypervisor » ni « vdev ») ;
 * //   - aucun en-tête vdev/hyperviseur : `find ~/qnx800 -iname '*vdev*'`
 * //     ne renvoie qu'une page de documentation
 * //     (com.qnx.doc.neutrino.utilities/topic/m/mods-vdevpeer-net.so.html) ;
 * //   - aucune bibliothèque vdev dans target/qnx/*/lib ;
 * //   - la licence installée est « QNX Software Development Platform 8.0
 * //     Education License », fonctionnalités « SDK | GPL Utils | IDE »
 * //     (sortie réelle de `qlicense -info`) : aucune fonctionnalité
 * //     Hypervisor.
 *
 * Ce fichier ne prétend donc PAS implémenter l'API vdev.  Il fixe la
 * structure que Toile veut fournir (un périphérique virtio-gpu 2D vu par
 * l'invité) et la liste des points à confirmer dès qu'un DDK hyperviseur
 * sera accessible.
 *
 * Rôle du vdev (spécification OASIS virtio 1.x §5.7) :
 *   - exposer un périphérique PCI virtio moderne (vendor 0x1AF4,
 *     device 0x1050) à la VM invitée ;
 *   - servir les files controlq/cursorq : lire les commandes de l'invité,
 *     les interpréter avec le cœur portable (core/, même code que l'invité
 *     et que les tests PC), et produire les effets « hôte » : afficher le
 *     contenu du framebuffer dans une fenêtre de l'hôte ;
 *   - répondre aux commandes GET_DISPLAY_INFO (mode de la tête).
 */
#include <stdint.h>
#include <stdio.h>

/* E4: à confirmer — l'API réelle d'un vdev QNX (fonctions d'enregistrement,
 * callbacks de file, accès à la mémoire invitée, mapping des BAR). */
typedef struct vdev_gpu_config {
    uint32_t width;        /* mode annoncé à l'invité */
    uint32_t height;
    uint32_t format;       /* toile_fmt (CONTRACT.md §3) */
    uint32_t num_scanouts; /* nombre de têtes virtuelles */
} vdev_gpu_config;

/* E4: à confirmer — signature réelle des callbacks (file, descripteurs,
 * mémoire invitée physique vs virtuelle). */
typedef struct vdev_gpu_ops {
    int (*init)(const vdev_gpu_config *cfg);
    int (*controlq_request)(const void *cmd, size_t len,
                            void *resp, size_t *resp_len);
    int (*cursorq_request)(const void *cmd, size_t len,
                           void *resp, size_t *resp_len);
    int (*present)(uint32_t scanout, const void *pixels, uint32_t stride,
                   uint32_t width, uint32_t height);
    void (*fini)(void);
} vdev_gpu_ops;

static const vdev_gpu_ops g_ops = {NULL, NULL, NULL, NULL, NULL};

int main(void)
{
    printf("toile-vdev-gpu: squelette volet B (X1), E4 non satisfait\n");
    printf("  DDK hyperviseur : ABSENT du SDP local, licence Education\n");
    printf("  ops renseignes  : %d\n", g_ops.init != NULL ? 1 : 0);
    printf("  voie livree     : volet A (QEMU), qui n'a pas besoin du DDK\n");
    return g_ops.init != NULL ? 0 : 1;
}
