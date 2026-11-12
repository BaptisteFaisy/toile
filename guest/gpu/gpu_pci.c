/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver: PCI enumeration and device bring-up.
 *
 * Enumeration itself is delegated to the Passerelle libpci glue through
 * the Toile transport layer (guest/virtio/): pci_device_find/attach,
 * BAR mapping and MSI-X programming all live in Passerelle
 * (virtio/src/pci_qnx.c), extended to virtio-gpu by
 * toile-virtio-gpu.patch.  This file only sequences the bring-up:
 * open -> negotiate -> queues -> config space -> GET_DISPLAY_INFO.
 */
#include "gpu_priv.h"

#include <string.h>

/* struct virtio_gpu_config (OASIS virtio 1.x §5.7.4), all le32. */
#define GPU_CFG_EVENTS_READ_OFF  0u
#define GPU_CFG_NUM_SCANOUTS_OFF 8u
#define GPU_CFG_SIZE             16u

/* GET_DISPLAY_INFO response: 24-byte control header followed by
 * VIRTIO_GPU_MAX_SCANOUTS (16) display modes of 24 bytes each:
 * rect {x,y,width,height} (16) + enabled (4) + flags (4). */
#define GPU_DI_HDR_LEN       TOILE_VG_HDR_LEN
#define GPU_DI_MODE_LEN      24u
#define GPU_DI_MODE_COUNT    16u
#define GPU_DI_RESP_LEN      (GPU_DI_HDR_LEN + GPU_DI_MODE_COUNT * GPU_DI_MODE_LEN)

static uint32_t gpu_rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int gpu_parse_display_info(gpu_ctx *g, const uint8_t *resp, size_t len)
{
    if (gpu_resp_type(resp, len) != TOILE_VG_RESP_OK_DISPLAY_INFO ||
        len < GPU_DI_HDR_LEN + GPU_DI_MODE_LEN) {
        gpu_log(g, "display info unavailable, keeping %ux%u default",
                (unsigned)g->width, (unsigned)g->height);
        return TOILE_OK; /* not fatal: keep the default mode */
    }
    for (uint32_t i = 0; i < GPU_DI_MODE_COUNT; i++) {
        size_t off = GPU_DI_HDR_LEN + (size_t)i * GPU_DI_MODE_LEN;
        if (off + GPU_DI_MODE_LEN > len)
            break;
        uint32_t enabled = gpu_rd_le32(resp + off + 16u);
        uint32_t w = gpu_rd_le32(resp + off + 8u);
        uint32_t h = gpu_rd_le32(resp + off + 12u);
        if (enabled != 0u && w != 0u && h != 0u) {
            g->width = w;
            g->height = h;
            gpu_log(g, "scanout %u mode %ux%u (enabled)", (unsigned)i,
                    (unsigned)w, (unsigned)h);
            return TOILE_OK;
        }
    }
    gpu_log(g, "no enabled display mode reported, keeping %ux%u",
            (unsigned)g->width, (unsigned)g->height);
    return TOILE_OK;
}

int gpu_setup_display(gpu_ctx *g)
{
    toile_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = TOILE_VG_CMD_GET_DISPLAY_INFO;
    uint8_t resp[GPU_DI_RESP_LEN];
    size_t got = 0;
    int rc = gpu_ctrl_run(g, &cmd, resp, sizeof(resp), &got);
    if (rc != TOILE_OK)
        return rc;
    return gpu_parse_display_info(g, resp, got);
}

static int gpu_open_queues(gpu_ctx *g)
{
    int rc = toile_tr_queue_open(g->tr, TOILE_VQ_CONTROLQ, 0u);
    if (rc != TOILE_TR_OK)
        return rc;
    if (toile_tr_queue_count(g->tr) > TOILE_VQ_CURSORQ) {
        (void)toile_tr_queue_open(g->tr, TOILE_VQ_CURSORQ, 0u);
    } else {
        gpu_log(g, "no cursorq: hardware cursor disabled (level C)");
    }
    return TOILE_TR_OK;
}

int gpu_pci_init(gpu_ctx *g, bool allow_msix)
{
    if (g == NULL)
        return -TOILE_TR_EINVAL;
    g->tr = toile_tr_open_pci(TOILE_VIRTIO_ID_GPU, allow_msix);
    if (g->tr == NULL) {
        gpu_log(g, "no virtio-gpu device found on the PCI bus");
        return -TOILE_TR_ENODEV;
    }
    /* No device-specific features for the 2D socle: the base device
     * (no VIRTIO_GPU_F_VIRGL) is exactly the contract scope. */
    int rc = toile_tr_init(g->tr, 0u);
    if (rc != TOILE_TR_OK) {
        gpu_log(g, "feature negotiation failed (%d)", rc);
        toile_tr_close(g->tr);
        g->tr = NULL;
        return rc;
    }
    gpu_log(g, "device up: %u queues, msix=%s, features=0x%llx",
            (unsigned)toile_tr_queue_count(g->tr),
            toile_tr_msix_active(g->tr) ? "yes" : "no",
            (unsigned long long)toile_tr_features(g->tr));
    rc = gpu_open_queues(g);
    if (rc != TOILE_TR_OK)
        return rc;
    uint8_t cfg[GPU_CFG_SIZE];
    memset(cfg, 0, sizeof(cfg));
    (void)toile_tr_read_config(g->tr, 0u, cfg, sizeof(cfg));
    g->num_scanouts = gpu_rd_le32(cfg + GPU_CFG_NUM_SCANOUTS_OFF);
    if (g->num_scanouts == 0u)
        g->num_scanouts = 1u;
    toile_model_init(&g->model);
    g->format = TOILE_FMT_B8G8R8X8_UNORM;
    g->width = GPU_DEF_WIDTH;
    g->height = GPU_DEF_HEIGHT;
    return gpu_setup_display(g);
}

void gpu_pci_fini(gpu_ctx *g)
{
    if (g == NULL)
        return;
    gpu_fb_free(g);
    if (g->tr != NULL) {
        toile_tr_close(g->tr);
        g->tr = NULL;
    }
}
