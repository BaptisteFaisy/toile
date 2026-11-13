/* SPDX-License-Identifier: MIT */
/*
 * Toile — animated plasma painter, implementation (see header).
 *
 * Cost per pixel is a few table lookups and one multiply: the palette holds
 * ready-packed 32-bit pixels for the current format, so the inner loop only
 * writes words (no shifts, no per-pixel packing).
 */
#include "toile_anim.h"

#include <stdio.h>
#include <string.h>

#include "toile_pattern.h"

#define ANIM_LUT 256u

/* 5x7 bitmap font: digits, the wordmark letters and the FPS letters. */
typedef struct anim_glyph {
    char c;
    uint8_t rows[7];
} anim_glyph;

static const anim_glyph g_font[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'#', {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A}},
};

static uint8_t g_hump[ANIM_LUT];
static uint32_t g_pal[ANIM_LUT];
static int g_ready;
static uint32_t g_pal_fmt = 0xFFFFFFFFu;

static void anim_rect(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                      uint32_t x, uint32_t y, uint32_t rw, uint32_t rh,
                      uint32_t color)
{
    uint8_t *base = (uint8_t *)pixels;
    for (uint32_t dy = 0; dy < rh && y + dy < h; dy++) {
        uint32_t *line = (uint32_t *)(void *)(base + (size_t)(y + dy) * stride);
        for (uint32_t dx = 0; dx < rw && x + dx < w; dx++)
            line[x + dx] = color;
    }
}

static const uint8_t *anim_glyph_rows(char c)
{
    for (size_t i = 0; i < sizeof(g_font) / sizeof(g_font[0]); i++)
        if (g_font[i].c == c)
            return g_font[i].rows;
    return NULL;
}

/* One glyph: 7 rows of 5 columns, each font pixel drawn as scale×scale. */
static void anim_draw_glyph(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                       uint32_t x, uint32_t y, uint32_t scale, uint32_t color,
                       const uint8_t *rows)
{
    for (uint32_t r = 0; r < 7u; r++)
        for (uint32_t c = 0; c < 5u; c++)
            if ((rows[r] >> (4u - c)) & 1u)
                anim_rect(pixels, stride, w, h, x + c * scale, y + r * scale,
                          scale, scale, color);
}

void toile_anim_text(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                     uint32_t x, uint32_t y, uint32_t scale, uint32_t color,
                     const char *text)
{
    if (pixels == NULL || text == NULL || scale == 0u)
        return;
    for (const char *p = text; *p != '\0'; p++) {
        const uint8_t *rows = anim_glyph_rows(*p);
        if (rows != NULL)
            anim_draw_glyph(pixels, stride, w, h, x, y, scale, color, rows);
        x += 6u * scale; /* 5 columns + 1 blank */
    }
}

static uint32_t anim_lerp(uint32_t a, uint32_t b, uint32_t num, uint32_t den)
{
    return a + ((b - a) * num) / den;
}

/* Ice -> fire palette, packed for `fmt` once per format. */
static void anim_palette(uint32_t fmt)
{
    for (uint32_t i = 0; i < ANIM_LUT; i++) {
        uint32_t r, g, b;
        if (i < 64u) {
            r = anim_lerp(10u, 0u, i, 64u);
            g = anim_lerp(20u, 200u, i, 64u);
            b = anim_lerp(90u, 220u, i, 64u);
        } else if (i < 128u) {
            r = anim_lerp(0u, 255u, i - 64u, 64u);
            g = anim_lerp(200u, 255u, i - 64u, 64u);
            b = anim_lerp(220u, 255u, i - 64u, 64u);
        } else if (i < 192u) {
            r = 255u;
            g = anim_lerp(255u, 140u, i - 128u, 64u);
            b = anim_lerp(255u, 0u, i - 128u, 64u);
        } else {
            r = anim_lerp(255u, 120u, i - 192u, 64u);
            g = anim_lerp(140u, 0u, i - 192u, 64u);
            b = anim_lerp(0u, 10u, i - 192u, 64u);
        }
        g_pal[i] = toile_pattern_pack(fmt, r, g, b);
    }
    g_pal_fmt = fmt;
}

/* Parabola hump 0 -> 255 -> 0: a smooth, integer, libm-free "sine". */
static void anim_init(void)
{
    if (g_ready)
        return;
    for (uint32_t i = 0; i < ANIM_LUT; i++) {
        uint32_t d = i < 128u ? 128u - i : i - 128u; /* distance to peak */
        uint32_t k = 128u - d;
        g_hump[i] = (uint8_t)((255u * k * k) / (128u * 128u));
    }
    g_ready = 1;
}

static void anim_overlay(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                         uint32_t fmt, uint32_t t_ms, uint32_t frame)
{
    uint32_t scale = h / 100u > 2u ? h / 100u : 2u;
    uint32_t margin = 3u * scale;
    uint32_t white = toile_pattern_pack(fmt, 255u, 255u, 255u);
    uint32_t black = toile_pattern_pack(fmt, 0u, 0u, 0u);
    char buf[24];
    uint32_t fps = t_ms > 0u ? (frame * 1000u) / t_ms : 0u;

    /* shadow first (offset by one font pixel), then the glyphs */
    toile_anim_text(pixels, stride, w, h, margin + scale, margin + scale,
                    scale, black, "TOILE");
    toile_anim_text(pixels, stride, w, h, margin, margin, scale, white, "TOILE");

    snprintf(buf, sizeof(buf), "#%u", frame);
    toile_anim_text(pixels, stride, w, h, margin + scale, h - margin - 6u * scale,
                    scale, black, buf);
    toile_anim_text(pixels, stride, w, h, margin, h - margin - 7u * scale,
                    scale, white, buf);

    snprintf(buf, sizeof(buf), "FPS %u", fps);
    uint32_t len = (uint32_t)strlen(buf) * 6u * scale;
    uint32_t x = w > len + margin + scale ? w - len - margin : margin;
    toile_anim_text(pixels, stride, w, h, x + scale, h - margin - 6u * scale,
                    scale, black, buf);
    toile_anim_text(pixels, stride, w, h, x, h - margin - 7u * scale,
                    scale, white, buf);
}

void toile_anim_frame(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                      uint32_t fmt, uint32_t t_ms, uint32_t frame)
{
    if (pixels == NULL || w < 8u || h < 8u || stride < w * 4u)
        return;
    anim_init();
    if (g_pal_fmt != fmt)
        anim_palette(fmt);
    uint32_t t = t_ms / 7u;                    /* slow drift */
    uint32_t hw = w / 2u;
    uint32_t hh = h / 2u;
    uint8_t *base = (uint8_t *)pixels;
    for (uint32_t y = 0; y < h; y++) {
        uint32_t *line = (uint32_t *)(void *)(base + (size_t)y * stride);
        uint32_t sy = g_hump[(y + t) & 255u];
        uint32_t dy = (y > hh ? y - hh : hh - y);
        uint32_t vy = dy * dy;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = (g_hump[(x + (t << 1)) & 255u] + sy +
                          g_hump[(((x + y) >> 1) + t) & 255u]) / 3u;
            uint32_t dx = (x > hw ? x - hw : hw - x);
            uint32_t q = (dx * dx + vy) >> 12;          /* vignette, >= 0 */
            uint32_t dim = q < 255u ? 255u - q : 0u;
            line[x] = g_pal[(v * dim) >> 8];
        }
    }
    anim_overlay(pixels, stride, w, h, fmt, t_ms, frame);
}