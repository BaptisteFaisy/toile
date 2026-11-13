/* SPDX-License-Identifier: MIT */
/*
 * Toile — /dev/gpu0 client library, implementation.
 *
 * devctl signature vérifiée dans le SDP installé
 * (target/qnx/usr/include/devctl.h:64) :
 *   int devctl(int fd, int dcmd, void *dev_data_ptr, size_t nbytes,
 *              int *dev_info_ptr);
 * Le code de retour est le statut (0 = succès), pas -1/errno comme sur
 * d'autres systèmes.  Les devctl côté serveur suivent le patron officiel
 * du guide Resource Manager du SDP (messages_Sample_IO_DEVCTL.html).
 */
#include "toile_gpu_client.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

int toile_gpu_open(const char *path)
{
    const char *target = path != NULL ? path : TOILE_GPU_DEVPATH;
    return open(target, O_RDWR);
}

int toile_gpu_info(int fd, toile_gpu_info_t *out)
{
    if (fd < 0 || out == NULL)
        return EINVAL;
    return devctl(fd, DCMD_TOILE_GPU_INFO, out, sizeof(*out), NULL);
}

int toile_gpu_fb(int fd, toile_gpu_fb_t *out)
{
    if (fd < 0 || out == NULL)
        return EINVAL;
    return devctl(fd, DCMD_TOILE_GPU_FB, out, sizeof(*out), NULL);
}

void *toile_gpu_mmap(int fd, const toile_gpu_fb_t *fb, size_t *len)
{
    if (fd < 0 || fb == NULL || fb->paddr == 0 || fb->bytes == 0)
        return NULL;
    /* MAP_PHYS : le descripteur doit être NOFD et l'offset porte l'adresse
     * physique (mmap(2) QNX 8.0 ; passer `fd` échoue avec EINVAL). */
    void *p = mmap(NULL, (size_t)fb->bytes, PROT_READ | PROT_WRITE,
                   MAP_PHYS | MAP_SHARED, NOFD, (off64_t)fb->paddr);
    if (p == MAP_FAILED)
        return NULL;
    if (len != NULL)
        *len = (size_t)fb->bytes;
    return p;
}

int toile_gpu_present(int fd, const toile_gpu_rect_t *rect)
{
    toile_gpu_rect_t local;
    if (fd < 0)
        return EINVAL;
    memset(&local, 0, sizeof(local));
    if (rect != NULL)
        local = *rect; /* devctl prend un tampon non const */
    return devctl(fd, DCMD_TOILE_GPU_PRESENT, &local, sizeof(local), NULL);
}

int toile_gpu_mire(int fd)
{
    uint32_t status = 0;
    if (fd < 0)
        return EINVAL;
    return devctl(fd, DCMD_TOILE_GPU_MIRE, &status, sizeof(status), NULL);
}

int toile_gpu_anim_get(int fd, toile_anim_cfg_t *out)
{
    if (fd < 0 || out == NULL)
        return EINVAL;
    return devctl(fd, DCMD_TOILE_GPU_ANIM_GET, out, sizeof(*out), NULL);
}

int toile_gpu_anim_set(int fd, const toile_anim_cfg_t *cfg)
{
    toile_anim_cfg_t local;
    if (fd < 0 || cfg == NULL)
        return EINVAL;
    local = *cfg; /* devctl prend un tampon non const */
    return devctl(fd, DCMD_TOILE_GPU_ANIM_SET, &local, sizeof(local), NULL);
}
