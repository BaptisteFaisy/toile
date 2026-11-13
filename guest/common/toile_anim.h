/* SPDX-License-Identifier: MIT */
/*
 * Toile — animated plasma painter for the guest framebuffer (see .c).
 *
 * Split so that several clients / several threads can paint disjoint bands of
 * the same scanout without any shared library state:
 *   - `toile_anim_rows()` paints rows [y0, y1) of the surface;
 *   - `toile_anim_overlay()` draws the wordmark, the frame counter and the
 *     on-screen FPS inside one band.
 *
 * The animation parameters (palette, speed, pause) are owned by the GPU
 * driver and read through DCMD_TOILE_GPU_ANIM_GET: a client never keeps its
 * own copy for long, so the keyboard driver and the painters agree.
 */
#ifndef TOILE_ANIM_H
#define TOILE_ANIM_H

#include <stdint.h>

#include "gpu_api.h" /* toile_anim_cfg_t (devctl DCMD_TOILE_GPU_ANIM_*) */

#ifdef __cplusplus
extern "C" {
#endif

/* Paint rows [y0, y1) of the plasma into the framebuffer.
 * `t_ms` is the caller's clock (frozen by the caller when paused). */
void toile_anim_rows(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                     uint32_t fmt, uint32_t t_ms, uint32_t frame,
                     uint32_t y0, uint32_t y1, const toile_anim_cfg_t *cfg);

/* Wordmark (`title`), frame counter and on-screen FPS, drawn inside the band
 * [y0, y1) of the full surface (w x h). */
void toile_anim_overlay(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                        uint32_t fmt, uint32_t t_ms, uint32_t frame,
                        const char *title, uint32_t y0, uint32_t y1);

/* 5x7 bitmap text, scale = pixels per font pixel, one font pixel of shadow. */
void toile_anim_text(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                     uint32_t x, uint32_t y, uint32_t scale, uint32_t color,
                     const char *text);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_ANIM_H */