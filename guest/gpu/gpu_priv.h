/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver, private shared definitions.
 *
 * Architecture (cahier des charges §4, decision D1): the portable core
 * (core/, pure C11) is the driver's authoritative state model.  Every
 * command is validated + applied to `model` FIRST (toile_cmdproc_run,
 * fail fast per CONTRACT.md §1); only a command the model accepts is
 * encoded (toile_vg_encode) and submitted to the device through the
 * Passerelle-based transport (guest/virtio/).
 */
#ifndef TOILE_GPU_PRIV_H
#define TOILE_GPU_PRIV_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "toile/cmdproc.h"
#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/vgproto.h"
#include "toile_transport.h"

#include "gpu_api.h"

/* Resource ids used by the single-scanout socle (S6/S7). */
#define GPU_RES_SCANOUT 1u /* framebuffer bound to scanout 0 */
#define GPU_RES_CURSOR  2u /* hardware cursor (level C, reserved) */

/* Default mode when the host reports no enabled display mode. */
#define GPU_DEF_WIDTH  1280u
#define GPU_DEF_HEIGHT 800u

/* Timeout for one synchronous controlq/cursorq exchange. */
#define GPU_CMD_TIMEOUT_MS 3000

/* Framebuffer: one physically contiguous DMA slab (one backing entry,
 * well under the TOILE_MAX_PAGES=32 contract limit). */
typedef struct gpu_fb {
    void *va;
    uint64_t pa;
    uint64_t bytes;
    uint32_t stride;
} gpu_fb;

typedef struct gpu_ctx {
    toile_transport *tr;
    toile_model model;      /* portable core shadow state (D1) */
    gpu_fb fb;
    uint32_t width;
    uint32_t height;
    uint32_t format;        /* toile_fmt of the framebuffer */
    uint32_t num_scanouts;  /* from virtio_gpu_config */
    bool ready;             /* scanout configured + fb presented once */
    int verbose;
} gpu_ctx;

/* --- gpu_pci.c: enumeration + device bring-up ------------------------- */
/* Phase-1 console evidence: find the virtio-gpu PCI device and print the
 * BDF (or an ECHEC line).  Returns 0 when found. */
int gpu_pci_report(gpu_ctx *g);
int gpu_pci_init(gpu_ctx *g, bool allow_msix);
void gpu_pci_fini(gpu_ctx *g);
/* GET_DISPLAY_INFO: fills g->width/height/num_scanouts. */
int gpu_setup_display(gpu_ctx *g);

/* --- gpu_ctrl.c: command pipeline (model -> encode -> wire) ----------- */
/* Run one command: validate+apply to the model, then submit to the
 * device.  `resp`/`resp_cap`/`resp_len` may be NULL/0.  Returns TOILE_OK
 * (0), a positive core verdict code, or a negative transport error. */
int gpu_ctrl_run(gpu_ctx *g, const toile_cmd *cmd,
                 uint8_t *resp, size_t resp_cap, size_t *resp_len);
/* Response header helpers (little-endian wire format). */
uint32_t gpu_resp_type(const uint8_t *resp, size_t len);
int gpu_ctrl_queue_of(uint32_t cmd_type); /* TOILE_VQ_CONTROLQ/CURSORQ */

/* --- gpu_resource.c: framebuffer, resources, scanout, flush ----------- */
int gpu_fb_alloc(gpu_ctx *g, uint32_t width, uint32_t height, uint32_t fmt);
void gpu_fb_free(gpu_ctx *g);
/* resource_create_2d + attach_backing + set_scanout for scanout 0. */
int gpu_scanout_create(gpu_ctx *g);
/* transfer_to_host_2d(rect) + resource_flush(rect). Zero rect = all. */
int gpu_present(gpu_ctx *g, toile_rect rect);
/* Fill the public devctl info structure from the driver context. */
void gpu_fill_info(const gpu_ctx *g, toile_gpu_info_t *info);

/* --- gpu_mire.c: static test pattern (S7 demo path) -------------------- */
void gpu_paint_mire(const gpu_fb *fb, uint32_t width, uint32_t height,
                    uint32_t fmt);

/* --- gpu_resmgr.c: /dev/gpu0 resource manager -------------------------- */
int gpu_resmgr_attach(gpu_ctx *g);
void gpu_resmgr_loop(void); /* does not return */

/* Logging: driver-side only (core/ stays silent, CONTRACT.md §2). */
void gpu_log(const gpu_ctx *g, const char *fmt, ...);
/* Console announcement for the phase-1 serial log (see gpu_ctrl.c). */
void gpu_announce(const char *fmt, ...);
/* Short reason string for an ECHEC console line. */
const char *gpu_rc_reason(int rc);

#endif /* TOILE_GPU_PRIV_H */
