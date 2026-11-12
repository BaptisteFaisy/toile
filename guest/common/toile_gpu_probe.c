/* SPDX-License-Identifier: MIT */
/*
 * Toile — client de preuve pour /dev/gpu0 (essai « le pilote invité
 * démarre dans l'invité »).
 *
 * Ce programme est le témoin côté invité : il ouvre /dev/gpu0 comme
 * un client ordinaire et interroge le pilote par devctl.  C'est la preuve
 * que le noeud existe RÉELLEMENT dans l'espace de noms du noyau et que le
 * resource manager répond — pas seulement que le pilote l'a annoncé.
 *
 * L'ouverture est réessayée pendant quelques secondes : le pilote doit
 * d'abord publier le noeud (le serveur PCI et la file de controle le
 * précèdent).  L'attente utilise nanosleep() de la libc : l'invité
 * minimal n'a ni binaire `sleep` ni builtin `sleep` dans ksh.
 *
 * Sortie : 0 si /dev/gpu0 a été ouvert et a répondu, 1 sinon.
 */
#include "toile_gpu_client.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define PROBE_TRIES   40
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

    toile_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    if (toile_gpu_info(fd, &info) != 0) {
        printf("CLIENT: ECHEC devctl DCMD_TOILE_GPU_INFO\n");
        fflush(stdout);
        return 1;
    }
    printf("CLIENT: info %ux%u fmt=%u scanouts=%u stride=%u res=%u fb=%llu\n",
           (unsigned)info.width, (unsigned)info.height,
           (unsigned)info.format, (unsigned)info.num_scanouts,
           (unsigned)info.stride, (unsigned)info.resource_id,
           (unsigned long long)info.fb_bytes);
    fflush(stdout);

    toile_gpu_fb_t fb;
    memset(&fb, 0, sizeof(fb));
    if (toile_gpu_fb(fd, &fb) == 0)
        printf("CLIENT: framebuffer paddr=0x%llx bytes=%llu stride=%u\n",
               (unsigned long long)fb.paddr, (unsigned long long)fb.bytes,
               (unsigned)fb.stride);
    fflush(stdout);
    printf("CLIENT: OK\n");
    fflush(stdout);
    return 0;
}
