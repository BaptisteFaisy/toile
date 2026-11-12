/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver: static test pattern (mire) on the framebuffer.
 *
 * The painter itself lives in guest/common/toile_pattern.c and is shared
 * with the Screen client demo (guest/display/): one implementation, two
 * callers (zero duplication rule).
 */
#include "gpu_priv.h"

#include "toile_pattern.h"

void gpu_paint_mire(const gpu_fb *fb, uint32_t width, uint32_t height,
                    uint32_t fmt)
{
    if (fb == NULL || fb->va == NULL)
        return;
    toile_pattern_paint(fb->va, fb->stride, width, height, fmt);
}
