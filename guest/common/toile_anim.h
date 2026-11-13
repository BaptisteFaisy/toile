/* SPDX-License-Identifier: MIT */
/*
 * Toile — animated plasma painter for the guest framebuffer.
 *
 * Pure CPU painting: no GPU 3D, no shader — the same bytes the driver would
 * get from an application.  Used by the live vitrine (guest/common/
 * toile_anim_main.c) to prove that the guest keeps driving the display, not
 * just once at boot.
 *
 * The painter is time-driven: `frame` and `t_ms` come from the caller, so the
 * module stays testable and clock-free (same rule as core/).
 */
#ifndef TOILE_ANIM_H
#define TOILE_ANIM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Paint one animation frame over the whole buffer, then overlay the frame
 * counter (bottom left) and the "TOILE" wordmark (top left).
 *   pixels : framebuffer base (32-bit pixels)
 *   stride : bytes per line
 *   w, h   : visible geometry
 *   fmt    : toile_fmt value (byte order; see toile_pattern_pack)
 *   t_ms   : monotonic time in milliseconds (drives the motion)
 *   frame  : frame index (displayed on screen) */
void toile_anim_frame(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                      uint32_t fmt, uint32_t t_ms, uint32_t frame);

/* Draw a 5x7 bitmap string (scale = pixel size of one font pixel) with a
 * one-font-pixel black shadow.  `color` is a packed pixel value. */
void toile_anim_text(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                     uint32_t x, uint32_t y, uint32_t scale, uint32_t color,
                     const char *text);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_ANIM_H */