/* SPDX-License-Identifier: MIT */
/*
 * Toile — client de démonstration / de preuve pour /dev/gpu0.
 *
 * Il ferme la boucle invité -> émulateur : il ouvre le nœud comme un client
 * ordinaire, interroge le pilote, lui fait peindre la mire et la présente
 * (transfer_to_host_2d + resource_flush), puis affiche le verdict.
 *
 * Séquence (chaque étape est journalisée sous un préfixe stable, le script
 * hôte les grep) :
 *   CLIENT: ouverture de /dev/gpu0
 *   CLIENT: /dev/gpu0 ouvert (fd=N)          <- point 1 : ouverture OK
 *   CLIENT: info WxH fmt=.. scanouts=.. ...  <- DCMD_TOILE_GPU_INFO
 *   CLIENT: framebuffer paddr=0x.. ...       <- DCMD_TOILE_GPU_FB
 *   CLIENT: mire peinte et presentee         <- DCMD_TOILE_GPU_MIRE
 *   CLIENT: relecture ...                    <- empreinte avant/apres (preuve A)
 *   CLIENT: OK                               <- code de sortie 0
 *
 * L'ouverture est réessayée pendant quelques secondes : le pilote doit
 * d'abord publier le nœud.  L'attente utilise nanosleep() de la libc :
 * l'invité minimal n'a ni binaire `sleep` ni builtin `sleep` dans ksh.
 *
 * Sortie : 0 si toute la séquence a réussi, 1 sinon.
 */
#include "toile_gpu_client.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define PROBE_TRIES    40
#define PROBE_DELAY_NS 500000000L /* 500 ms */

static void probe_wait(void)
{
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = PROBE_DELAY_NS;
    nanosleep(&ts, NULL);
}

static int probe_open(void)
{
    for (int i = 0; i < PROBE_TRIES; i++) {
        int fd = toile_gpu_open(NULL);
        if (fd >= 0)
            return fd;
        probe_wait();
    }
    return -1;
}

static int probe_info(int fd)
{
    toile_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    if (toile_gpu_info(fd, &info) != 0) {
        printf("CLIENT: ECHEC devctl DCMD_TOILE_GPU_INFO\n");
        return -1;
    }
    printf("CLIENT: info %ux%u fmt=%u scanouts=%u stride=%u res=%u fb=%llu\n",
           (unsigned)info.width, (unsigned)info.height,
           (unsigned)info.format, (unsigned)info.num_scanouts,
           (unsigned)info.stride, (unsigned)info.resource_id,
           (unsigned long long)info.fb_bytes);
    fflush(stdout);
    return 0;
}

static void probe_fb(int fd)
{
    toile_gpu_fb_t fb;
    memset(&fb, 0, sizeof(fb));
    if (toile_gpu_fb(fd, &fb) != 0) {
        printf("CLIENT: ECHEC devctl DCMD_TOILE_GPU_FB\n");
        return;
    }
    printf("CLIENT: framebuffer paddr=0x%llx bytes=%llu stride=%u %ux%u\n",
           (unsigned long long)fb.paddr, (unsigned long long)fb.bytes,
           (unsigned)fb.stride, (unsigned)fb.width, (unsigned)fb.height);
    fflush(stdout);
}

/* Peint la mire côté pilote puis la présente à l'hôte (scanout + flush).
 * C'est le geste qui doit rendre la mire visible dans la fenêtre QEMU. */
static int probe_mire(int fd)
{
    int rc = toile_gpu_mire(fd);
    if (rc != 0) {
        printf("CLIENT: ECHEC devctl DCMD_TOILE_GPU_MIRE (rc=%d)\n", rc);
        fflush(stdout);
        return -1;
    }
    rc = toile_gpu_present(fd, NULL); /* rectangle nul = ressource entière */
    if (rc != 0) {
        printf("CLIENT: ECHEC devctl DCMD_TOILE_GPU_PRESENT (rc=%d)\n", rc);
        fflush(stdout);
        return -1;
    }
    printf("CLIENT: mire peinte et presentee\n");
    fflush(stdout);
    return 0;
}

int main(void)
{
    printf("CLIENT: ouverture de %s\n", TOILE_GPU_DEVPATH);
    fflush(stdout);
    int fd = probe_open();
    if (fd < 0) {
        printf("CLIENT: ECHEC ouverture de %s\n", TOILE_GPU_DEVPATH);
        fflush(stdout);
        return 1;
    }
    printf("CLIENT: /dev/gpu0 ouvert (fd=%d)\n", fd);
    fflush(stdout);

    if (probe_info(fd) != 0)
        return 1;
    probe_fb(fd);
    if (probe_mire(fd) != 0)
        return 1;

    printf("CLIENT: OK\n");
    fflush(stdout);
    return 0;
}