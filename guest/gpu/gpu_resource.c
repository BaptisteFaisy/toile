/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver: framebuffer, resources, backing, scanout,
 * flush (S6 socle: one resource, one scanout).
 *
 * Memory model: the framebuffer is ONE physically contiguous DMA slab,
 * so the backing is a single virtio_gpu_mem_entry {addr,length} — far
 * below the TOILE_MAX_PAGES=32 contract limit even at 4K.
 */
#include "gpu_priv.h"

#include <string.h>

int gpu_fb_alloc(gpu_ctx *g, uint32_t width, uint32_t height, uint32_t fmt)
{
    uint32_t bpp = toile_fmt_bpp((toile_fmt)fmt);
    if (g == NULL || bpp == 0u || width == 0u || height == 0u)
        return TOILE_CMD_INVALID;
    if (g->fb.va != NULL)
        gpu_fb_free(g);
    uint32_t stride = width * bpp;
    uint64_t bytes = (uint64_t)stride * (uint64_t)height;
    uint64_t pa = 0;
    void *va = toile_tr_dma_alloc(g->tr, (size_t)bytes, 4096u, &pa);
    if (va == NULL) {
        gpu_log(g, "cannot allocate %llu bytes of DMA framebuffer",
                (unsigned long long)bytes);
        return -TOILE_TR_ENOMEM;
    }
    memset(va, 0, (size_t)bytes);
    g->fb.va = va;
    g->fb.pa = pa;
    g->fb.bytes = bytes;
    g->fb.stride = stride;
    g->format = fmt;
    g->width = width;
    g->height = height;
    gpu_log(g, "framebuffer %ux%u fmt=%u stride=%u pa=0x%llx",
            (unsigned)width, (unsigned)height, (unsigned)fmt,
            (unsigned)stride, (unsigned long long)pa);
    return TOILE_OK;
}

void gpu_fb_free(gpu_ctx *g)
{
    if (g == NULL || g->fb.va == NULL)
        return;
    toile_tr_dma_free(g->tr, g->fb.va, (size_t)g->fb.bytes);
    memset(&g->fb, 0, sizeof(g->fb));
}

static int gpu_cmd_simple(gpu_ctx *g, toile_cmd *cmd)
{
    return gpu_ctrl_run(g, cmd, NULL, 0u, NULL);
}

static void gpu_cmd_init(toile_cmd *cmd, uint32_t type)
{
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = type;
}

static int gpu_resource_create(gpu_ctx *g, uint32_t id, uint32_t fmt,
                               uint32_t w, uint32_t h)
{
    toile_cmd cmd;
    gpu_cmd_init(&cmd, TOILE_VG_CMD_RESOURCE_CREATE_2D);
    cmd.u.create_2d.resource_id = id;
    cmd.u.create_2d.format = fmt;
    cmd.u.create_2d.width = w;
    cmd.u.create_2d.height = h;
    return gpu_cmd_simple(g, &cmd);
}

static int gpu_backing_attach(gpu_ctx *g, uint32_t id)
{
    toile_cmd cmd;
    gpu_cmd_init(&cmd, TOILE_VG_CMD_RESOURCE_ATTACH_BACKING);
    cmd.u.backing.resource_id = id;
    cmd.u.backing.nr_entries = 1u; /* one contiguous slab */
    cmd.u.backing.pages[0].addr = g->fb.pa;
    cmd.u.backing.pages[0].length = (uint32_t)g->fb.bytes;
    return gpu_cmd_simple(g, &cmd);
}

static int gpu_scanout_set(gpu_ctx *g, uint32_t id)
{
    toile_cmd cmd;
    gpu_cmd_init(&cmd, TOILE_VG_CMD_SET_SCANOUT);
    cmd.u.scanout.resource_id = id;
    cmd.u.scanout.scanout_id = 0u; /* socle S6: scanout unique */
    cmd.u.scanout.rect.x = 0u;
    cmd.u.scanout.rect.y = 0u;
    cmd.u.scanout.rect.w = g->width;
    cmd.u.scanout.rect.h = g->height;
    return gpu_cmd_simple(g, &cmd);
}

int gpu_scanout_create(gpu_ctx *g)
{
    if (g == NULL || g->fb.va == NULL)
        return TOILE_CMD_INVALID;
    int rc = gpu_resource_create(g, GPU_RES_SCANOUT, g->format,
                                 g->width, g->height);
    if (rc != TOILE_OK)
        return rc;
    rc = gpu_backing_attach(g, GPU_RES_SCANOUT);
    if (rc != TOILE_OK)
        return rc;
    rc = gpu_scanout_set(g, GPU_RES_SCANOUT);
    if (rc != TOILE_OK)
        return rc;
    g->ready = true;
    gpu_log(g, "scanout 0 <- resource %u (%ux%u)",
            (unsigned)GPU_RES_SCANOUT, (unsigned)g->width,
            (unsigned)g->height);
    return TOILE_OK;
}

static toile_rect gpu_rect_or_full(const gpu_ctx *g, toile_rect r)
{
    if (r.w == 0u || r.h == 0u) {
        r.x = 0u;
        r.y = 0u;
        r.w = g->width;
        r.h = g->height;
    }
    return r;
}

int gpu_present(gpu_ctx *g, toile_rect rect)
{
    if (g == NULL || !g->ready)
        return TOILE_CMD_INVALID;
    toile_rect r = gpu_rect_or_full(g, rect);
    if (r.x >= g->width || r.y >= g->height)
        return TOILE_CMD_INVALID;
    if (r.x + r.w > g->width)
        r.w = g->width - r.x;
    if (r.y + r.h > g->height)
        r.h = g->height - r.y;
    /* Le transfert 2D décrit une région LINÉAIRE du tampon attaché : l'offset
     * est la position du rectangle dans la ressource (y * stride + x * bpp),
     * sans quoi un sous-rectangle recopie le début du framebuffer (bug
     * trouvé avec la mire de trois zones, 2026-11-13). */
    uint32_t bpp = toile_fmt_bpp((toile_fmt)g->format);
    if (bpp == 0u)
        return TOILE_CMD_INVALID;
    toile_cmd cmd;
    gpu_cmd_init(&cmd, TOILE_VG_CMD_TRANSFER_TO_HOST_2D);
    cmd.u.xfer.resource_id = GPU_RES_SCANOUT;
    cmd.u.xfer.rect = r;
    cmd.u.xfer.offset = (uint64_t)r.y * g->fb.stride + (uint64_t)r.x * bpp;
    int rc = gpu_cmd_simple(g, &cmd);
    if (rc != TOILE_OK && rc != TOILE_TRANSFER_OK)
        return rc;
    gpu_cmd_init(&cmd, TOILE_VG_CMD_RESOURCE_FLUSH);
    cmd.u.flush.resource_id = GPU_RES_SCANOUT;
    cmd.u.flush.rect = r;
    return gpu_cmd_simple(g, &cmd);
}

/* Fill the public info structure from the driver context. */
void gpu_fill_info(const gpu_ctx *g, toile_gpu_info_t *info)
{
    if (g == NULL || info == NULL)
        return;
    info->width = g->width;
    info->height = g->height;
    info->format = g->format;
    info->num_scanouts = g->num_scanouts;
    info->stride = g->fb.stride;
    info->resource_id = g->ready ? GPU_RES_SCANOUT : 0u;
    info->fb_bytes = g->fb.bytes;
}
