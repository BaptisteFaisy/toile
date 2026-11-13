/* SPDX-License-Identifier: MIT */
/*
 * Toile — static test pattern painter, implementation (see header).
 */
#include "toile_pattern.h"

#include "toile/vgproto.h" /* toile_fmt values */

#define PATTERN_BARS 8u

int toile_pattern_fmt_is_bgra(uint32_t fmt)
{
    switch (fmt) {
    case TOILE_FMT_B8G8R8A8_UNORM:
    case TOILE_FMT_B8G8R8X8_UNORM:
    case TOILE_FMT_A8R8G8B8_UNORM:
    case TOILE_FMT_X8R8G8B8_UNORM:
        return 1; /* byte 0 = blue */
    default:
        return 0; /* RGBA family: byte 0 = red */
    }
}

uint32_t toile_pattern_pack(uint32_t fmt, uint32_t r, uint32_t g, uint32_t b)
{
    uint32_t word;
    r &= 0xFFu; /* X stays 0: the packed word is 24 bits of colour only, */
    g &= 0xFFu; /* so the guest hash and the host pixels agree byte-wise. */
    b &= 0xFFu;
    if (toile_pattern_fmt_is_bgra(fmt))
        word = (r << 16) | (g << 8) | b; /* word bytes: B,G,R,X on LE */
    else
        word = (b << 16) | (g << 8) | r; /* word bytes: R,G,B,X on LE */
    return word & 0x00FFFFFFu;
}

static const uint32_t pattern_bar_rgb[PATTERN_BARS][3] = {
    {0xFFu, 0xFFu, 0xFFu}, /* white   */
    {0xFFu, 0xFFu, 0x00u}, /* yellow  */
    {0x00u, 0xFFu, 0xFFu}, /* cyan    */
    {0x00u, 0xFFu, 0x00u}, /* green   */
    {0xFFu, 0x00u, 0xFFu}, /* magenta */
    {0xFFu, 0x00u, 0x00u}, /* red     */
    {0x00u, 0x00u, 0xFFu}, /* blue    */
    {0x00u, 0x00u, 0x00u}, /* black   */
};

static uint32_t pattern_color(uint32_t fmt, uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    uint32_t r = 0, g = 0, b = 0;
    if (y < (h * 3u) / 4u) {
        uint32_t bar = (x * PATTERN_BARS) / w;
        if (bar >= PATTERN_BARS)
            bar = PATTERN_BARS - 1u;
        r = pattern_bar_rgb[bar][0];
        g = pattern_bar_rgb[bar][1];
        b = pattern_bar_rgb[bar][2];
    } else if (y < (h * 7u) / 8u) {
        uint32_t v = (x * 255u) / (w != 0u ? w : 1u);
        r = v;
        g = v;
        b = v;
    } else {
        /* 16-px checkerboard: proves per-pixel addressing. */
        if (((x / 16u) + (y / 16u)) % 2u == 0u) {
            r = 0x20u;
            g = 0x60u;
            b = 0xA0u;
        } else {
            r = 0xE0u;
            g = 0xE0u;
            b = 0xE0u;
        }
    }
    return toile_pattern_pack(fmt, r, g, b);
}

void toile_pattern_paint(void *pixels, uint32_t stride,
                         uint32_t width, uint32_t height, uint32_t fmt)
{
    if (pixels == NULL || width == 0u || height == 0u || stride < width * 4u)
        return;
    uint8_t *base = (uint8_t *)pixels;
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *line = (uint32_t *)(void *)(base + (size_t)y * stride);
        for (uint32_t x = 0; x < width; x++)
            line[x] = pattern_color(fmt, x, y, width, height);
    }
}