/* SPDX-License-Identifier: MIT */
/*
 * Toile — public typed API of the /dev/gpu0 resource manager.
 *
 * Consumed by the Screen display driver skeleton (guest/display/), by the
 * demo application and by any client that wants to drive the virtio-gpu
 * device without knowing the wire protocol.  All structures are fixed
 * layout, no pointers cross the devctl boundary.
 *
 * devctl numbers: class _DCMD_MISC (0x05, devctl.h:109), subcodes 0x50+
 * chosen to stay clear of the QNX-reserved DCMD_MISC_* subcodes 1..4
 * (sys/dcmd_misc.h:34-47).
 */
#ifndef TOILE_GPU_API_H
#define TOILE_GPU_API_H

#include <stdint.h>

#include <devctl.h> /* _DCMD_MISC, __DIOF, __DIOT */

#ifdef __cplusplus
extern "C" {
#endif

/* Snapshot of the driver state (DCMD_TOILE_GPU_INFO). */
typedef struct toile_gpu_info {
    uint32_t width;          /* active scanout-0 mode, pixels */
    uint32_t height;
    uint32_t format;         /* toile_fmt value (CONTRACT.md §3) */
    uint32_t num_scanouts;   /* host-reported scanouts */
    uint32_t stride;         /* framebuffer bytes per line */
    uint32_t resource_id;    /* virtio-gpu resource bound to scanout 0 */
    uint64_t fb_bytes;       /* framebuffer size */
} toile_gpu_info_t;

/* Framebuffer descriptor for direct pixel access (DCMD_TOILE_GPU_FB).
 * `paddr` is the guest physical address of the contiguous framebuffer;
 * demo/root clients mmap it with MAP_PHYS.  The production path for
 * Screen is the mmap resmgr callout — see guest/display/README.md (E2). */
typedef struct toile_gpu_fb {
    uint64_t paddr;
    uint64_t bytes;
    uint32_t stride;
    uint32_t format;   /* toile_fmt */
    uint32_t width;
    uint32_t height;
} toile_gpu_fb_t;

/* Rectangle in pixels; all-zero means "whole resource" (CONTRACT.md §3). */
typedef struct toile_gpu_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
} toile_gpu_rect_t;

#define DCMD_TOILE_GPU_INFO    __DIOF(_DCMD_MISC, 0x50, toile_gpu_info_t)
#define DCMD_TOILE_GPU_FB      __DIOF(_DCMD_MISC, 0x51, toile_gpu_fb_t)
/* Push the given rectangle guest->host then flush it to the display. */
#define DCMD_TOILE_GPU_PRESENT __DIOT(_DCMD_MISC, 0x52, toile_gpu_rect_t)
/* Paint the built-in static test pattern (mire) and present it. */
#define DCMD_TOILE_GPU_MIRE    __DIOF(_DCMD_MISC, 0x53, uint32_t)

/* Shared animation state (palette, speed, pause) of the demo clients.
 * GET reads it, SET writes it: the keyboard driver is the only writer, the
 * painters are readers, so no client keeps a private copy. */
typedef struct toile_anim_cfg {
    uint32_t magic;   /* TOILE_ANIM_MAGIC */
    uint32_t palette; /* 0..TOILE_ANIM_PALETTES-1 */
    uint32_t speed;   /* 1..8 */
    uint32_t paused;  /* 0 = running, 1 = frozen */
} toile_anim_cfg_t;

#define TOILE_ANIM_MAGIC 0x70016E75u /* 'p','n','u' + version */
#define TOILE_ANIM_PALETTES 3u

#define DCMD_TOILE_GPU_ANIM_GET __DIOF(_DCMD_MISC, 0x54, toile_anim_cfg_t)
#define DCMD_TOILE_GPU_ANIM_SET __DIOT(_DCMD_MISC, 0x55, toile_anim_cfg_t)

#ifdef __cplusplus
}
#endif

#endif /* TOILE_GPU_API_H */
