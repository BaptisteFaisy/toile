/* SPDX-License-Identifier: MIT */
/*
 * Toile — client library for the /dev/gpu0 resource manager.
 *
 * Shared by the display demos (guest/display/) and by any application that
 * wants to drive the Toile GPU driver through its typed devctl API
 * (guest/gpu/gpu_api.h).  One implementation, no duplication.
 */
#ifndef TOILE_GPU_CLIENT_H
#define TOILE_GPU_CLIENT_H

#include <stddef.h>

#include "gpu_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TOILE_GPU_DEVPATH "/dev/gpu0"

/* Open the driver.  Returns a file descriptor, or -1 on failure (errno). */
int toile_gpu_open(const char *path);

/* DCMD_TOILE_GPU_INFO.  0 on success. */
int toile_gpu_info(int fd, toile_gpu_info_t *out);

/* DCMD_TOILE_GPU_FB: framebuffer descriptor (physical address + geometry). */
int toile_gpu_fb(int fd, toile_gpu_fb_t *out);

/* Map the framebuffer for direct pixel access (MAP_PHYS, requires the
 * process to hold the capability — root in the demo image).  Returns the
 * mapping, or NULL.  `len` receives the mapped length. */
void *toile_gpu_mmap(int fd, const toile_gpu_fb_t *fb, size_t *len);

/* DCMD_TOILE_GPU_PRESENT: push `rect` to the host then flush.  A zero
 * rectangle means the whole resource.  0 on success. */
int toile_gpu_present(int fd, const toile_gpu_rect_t *rect);

/* DCMD_TOILE_GPU_MIRE: paint the driver's static test pattern and present
 * it.  0 on success. */
int toile_gpu_mire(int fd);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_GPU_CLIENT_H */
