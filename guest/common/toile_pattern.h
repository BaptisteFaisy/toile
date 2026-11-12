/* SPDX-License-Identifier: MIT */
/*
 * Toile — shared static test pattern ("mire") painter.
 *
 * Single implementation reused by the GPU driver demo path
 * (guest/gpu/gpu_mire.c) and by the Screen client demo
 * (guest/display/toile_display.c): zero duplication.
 *
 * Pixel formats are the CONTRACT.md §3 values (toile_fmt); all are
 * 4 bytes/pixel.  Two memory-order families:
 *   - BGRA family (fmt 1..4):   byte 0 = Blue   -> word = R<<16|G<<8|B
 *   - RGBA family (fmt 67,68,121,134): byte 0 = Red -> word = B<<16|G<<8|R
 * (little-endian guest; names read most-significant byte first).
 */
#ifndef TOILE_PATTERN_H
#define TOILE_PATTERN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True when the format stores Blue in byte 0 (BGRA family). */
int toile_pattern_fmt_is_bgra(uint32_t fmt);

/* Paint the static test pattern into a pixel buffer.
 * `pixels`  : framebuffer base (32-bit pixels)
 * `stride`  : bytes per line
 * `width`   : visible width in pixels
 * `height`  : visible height in pixels
 * `fmt`     : toile_fmt value; BGRA/RGBA family selects byte order. */
void toile_pattern_paint(void *pixels, uint32_t stride,
                         uint32_t width, uint32_t height, uint32_t fmt);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_PATTERN_H */
