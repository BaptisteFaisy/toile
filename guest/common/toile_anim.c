/* SPDX-License-Identifier: MIT */
/*
 * Toile — animated plasma painter, implementation (see header).
 *
 * Cost per pixel: three table lookups, one multiply (vignette) and one more
 * (speed scaling is done by the caller's clock). Palettes are pre-packed for
 * the current pixel format, so the inner loop only writes 32-bit words.
 */
#include "toile_anim.h"

#include <stdio.h>
#include <string.h>

#include "toile_pattern.h"

#define ANIM_LUT 256u

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
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'x', {0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x00}},
};

/* Three 256-entry palettes, packed per (palette, format). */
static uint32_t g_pal[TOILE_ANIM_PALETTES][ANIM_LUT];
static uint32_t g_pal_fmt = 0xFFFFFFFFu;
static uint8_t g_hump[ANIM_LUT];
static int g_ready;

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

static void anim_draw_glyph(void *pixels, uint32_t stride, uint32_t w,
                            uint32_t h, uint32_t x, uint32_t y, uint32_t scale,
                            uint32_t color, const uint8_t *rows)
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

/* Interpolation signée : en uint32, (b - a) avec b < a déborde et salit
 * l'octet X du pixel — l'empreinte de l'invité ne correspondait plus aux
 * pixels rendus par l'hôte (X est ignoré à l'affichage). */
static uint32_t anim_lerp(uint32_t a, uint32_t b, uint32_t num, uint32_t den)
{
    int32_t d = (int32_t)b - (int32_t)a;
    int32_t v = (int32_t)a + (d * (int32_t)num) / (int32_t)den;
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (uint32_t)v;
}

/* One 4-stop gradient: 0-63 A->B, 64-127 B->C, 128-191 C->D, 192-255 D->A. */
static uint32_t anim_gradient(uint32_t fmt, uint32_t i, const uint8_t st[4][3])
{
    uint32_t seg = i / 64u;
    uint32_t k = i % 64u;
    const uint8_t *a = st[seg];
    const uint8_t *b = st[(seg + 1u) % 4u];
    return toile_pattern_pack(fmt, anim_lerp(a[0], b[0], k, 64u),
                              anim_lerp(a[1], b[1], k, 64u),
                              anim_lerp(a[2], b[2], k, 64u));
}

static void anim_palettes(uint32_t fmt)
{
    static const uint8_t ice_fire[4][3] = {
        {10, 20, 90}, {0, 200, 220}, {255, 255, 255}, {255, 60, 0}};
    static const uint8_t ocean[4][3] = {
        {2, 8, 40}, {0, 90, 190}, {40, 210, 210}, {235, 250, 255}};
    static const uint8_t neon[4][3] = {
        {20, 0, 40}, {255, 0, 200}, {255, 240, 0}, {0, 255, 120}};
    for (uint32_t i = 0; i < ANIM_LUT; i++) {
        g_pal[0][i] = anim_gradient(fmt, i, ice_fire);
        g_pal[1][i] = anim_gradient(fmt, i, ocean);
        g_pal[2][i] = anim_gradient(fmt, i, neon);
    }
    g_pal_fmt = fmt;
}

/* Parabola hump 0 -> 255 -> 0: smooth, integer, libm-free "sine". */
static void anim_init(void)
{
    if (g_ready)
        return;
    for (uint32_t i = 0; i < ANIM_LUT; i++) {
        uint32_t d = i < 128u ? 128u - i : i - 128u;
        uint32_t k = 128u - d;
        g_hump[i] = (uint8_t)((255u * k * k) / (128u * 128u));
    }
    g_ready = 1;
}

void toile_anim_rows(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                     uint32_t fmt, uint32_t t_ms, uint32_t frame,
                     uint32_t y0, uint32_t y1, const toile_anim_cfg_t *cfg)
{
    (void)frame;
    if (pixels == NULL || w < 8u || h < 8u || stride < w * 4u)
        return;
    anim_init();
    if (g_pal_fmt != fmt)
        anim_palettes(fmt);
    uint32_t pal = cfg != NULL ? cfg->palette % TOILE_ANIM_PALETTES : 0u;
    const uint32_t *pal_rows = g_pal[pal];
    uint32_t t = t_ms / 7u;
    uint32_t hw = w / 2u;
    uint32_t hh = h / 2u;
    if (y1 > h)
        y1 = h;
    uint8_t *base = (uint8_t *)pixels;
    for (uint32_t y = y0; y < y1; y++) {
        uint32_t *line = (uint32_t *)(void *)(base + (size_t)y * stride);
        uint32_t sy = g_hump[(y + t) & 255u];
        uint32_t dy = (y > hh ? y - hh : hh - y);
        uint32_t vy = dy * dy;
        for (uint32_t x = 0; x < w; x++) {
            uint32_t v = (g_hump[(x + (t << 1)) & 255u] + sy +
                          g_hump[(((x + y) >> 1) + t) & 255u]) / 3u;
            uint32_t dx = (x > hw ? x - hw : hw - x);
            uint32_t q = (dx * dx + vy) >> 12; /* vignette */
            uint32_t dim = q < 255u ? 255u - q : 0u;
            line[x] = pal_rows[(v * dim) >> 8];
        }
    }
}

void toile_anim_overlay(void *pixels, uint32_t stride, uint32_t w, uint32_t h,
                        uint32_t fmt, uint32_t t_ms, uint32_t frame,
                        const char *title, uint32_t y0, uint32_t y1)
{
    uint32_t scale = h / 100u > 2u ? h / 100u : 2u;
    uint32_t margin = 3u * scale;
    uint32_t white = toile_pattern_pack(fmt, 255u, 255u, 255u);
    uint32_t black = toile_pattern_pack(fmt, 0u, 0u, 0u);
    uint32_t fps = t_ms > 0u ? (frame * 1000u) / t_ms : 0u;
    char buf[32];

    const char *titre = title != NULL ? title : "TOILE";
    toile_anim_text(pixels, stride, w, h, margin + scale, y0 + margin + scale,
                    scale, black, titre);
    toile_anim_text(pixels, stride, w, h, margin, y0 + margin, scale, white,
                    titre);

    snprintf(buf, sizeof(buf), "#%u", frame);
    toile_anim_text(pixels, stride, w, h, margin + scale,
                    y1 - margin - 6u * scale, scale, black, buf);
    toile_anim_text(pixels, stride, w, h, margin, y1 - margin - 7u * scale,
                    scale, white, buf);

    snprintf(buf, sizeof(buf), "FPS %u", fps);
    uint32_t len = (uint32_t)strlen(buf) * 6u * scale;
    uint32_t x = w > len + margin + scale ? w - len - margin : margin;
    toile_anim_text(pixels, stride, w, h, x + scale, y1 - margin - 6u * scale,
                    scale, black, buf);
    toile_anim_text(pixels, stride, w, h, x, y1 - margin - 7u * scale, scale,
                    white, buf);
}
