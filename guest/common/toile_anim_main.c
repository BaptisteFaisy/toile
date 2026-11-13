/* SPDX-License-Identifier: MIT */
/*
 * Toile — plasma client for /dev/gpu0 (the live vitrine's moving picture).
 *
 * Modes (all through the typed devctl API; nothing is painted by the driver):
 *   toile-anim                        whole screen, 30 im/s, shared palette
 *   toile-anim --zone 1 --zones 3     one vertical band of a 3-client wall
 *   toile-anim --palette 1            local palette offset (per panel)
 *   toile-anim --threads 4            paint the band with 4 threads
 *   toile-anim --bench 300            N frames unpaced, then im/s and MB/s
 *   toile-anim --hash-hold            print the FNV-1a of each held frame
 *
 * Console lines (grep-able prefixes):
 *   ANIM: /dev/gpu0 ouvert (fd=N)
 *   ANIM: framebuffer mappe 1280x800 stride=5120 fmt=2 zone=0/3 ...
 *   ANIM: N images, X im/s, present_ko=K, t=MS ms, palette=P speed=S ...
 *   HASH: frame=N w=1280 h=800 stride=5120 fnv=HEX hold_ms=1200
 *   BENCH: 1280x800 frames=N total_ms=T paint_im_s=X MB_s=Y present_us=Z
 *   ANIM: ECHEC <raison>
 */
#include "toile_anim.h"
#include "toile_gpu_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/neutrino.h> /* ThreadCreate/ThreadJoin (threads QNX natifs) */

#define ANIM_TARGET_FPS 30u
#define ANIM_OPEN_TRIES 40
#define ANIM_OPEN_DELAY_MS 250u
#define ANIM_REPORT_MS 5000u
#define ANIM_HASH_EVERY 90u
#define ANIM_HOLD_MS 2500u
#define ANIM_STATE_EVERY 10u
#define ANIM_MAX_THREADS 16u

typedef struct anim_opts {
    uint32_t zone, zones, palette, threads;
    uint32_t bench, hash_hold, fps;
} anim_opts;

typedef struct anim_band {
    void *px;
    uint32_t stride, w, h, fmt, t, frame;
    uint32_t y0, y1;
    const toile_anim_cfg_t *cfg;
} anim_band;

typedef struct anim_ctx {
    anim_opts o;
    int fd;
    toile_gpu_fb_t fb;
    void *px;
    toile_anim_cfg_t cfg;
    anim_band band;
    toile_gpu_rect_t rect;
    char titre[16];
} anim_ctx;

static uint32_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0u;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

static void wait_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

static int anim_open(void)
{
    for (int i = 0; i < ANIM_OPEN_TRIES; i++) {
        int fd = toile_gpu_open(NULL);
        if (fd >= 0)
            return fd;
        wait_ms(ANIM_OPEN_DELAY_MS);
    }
    return -1;
}

static void *anim_thread(void *arg)
{
    anim_band *b = (anim_band *)arg;
    toile_anim_rows(b->px, b->stride, b->w, b->h, b->fmt, b->t, b->frame,
                    b->y0, b->y1, b->cfg);
    return NULL;
}

/* Paint [y0, y1) with `threads` bands (1 = straight through). */
static void anim_paint(anim_band *b, uint32_t threads)
{
    if (threads <= 1u) {
        toile_anim_rows(b->px, b->stride, b->w, b->h, b->fmt, b->t, b->frame,
                        b->y0, b->y1, b->cfg);
        return;
    }
    if (threads > ANIM_MAX_THREADS)
        threads = ANIM_MAX_THREADS;
    pthread_t tid[ANIM_MAX_THREADS];
    anim_band parts[ANIM_MAX_THREADS];
    uint32_t rows = (b->y1 - b->y0 + threads - 1u) / threads;
    for (uint32_t i = 0; i < threads; i++) {
        parts[i] = *b;
        parts[i].y0 = b->y0 + i * rows;
        parts[i].y1 = parts[i].y0 + rows > b->y1 ? b->y1 : parts[i].y0 + rows;
        tid[i] = ThreadCreate(0, anim_thread, &parts[i], NULL);
        if (tid[i] == 0)
            parts[i].y0 = parts[i].y1; /* nothing to paint for this one */
    }
    for (uint32_t i = 0; i < threads; i++)
        if (tid[i] != 0)
            ThreadJoin(tid[i], NULL);
}

/* FNV-1a 64 over the visible bytes, row-major: the host rebuilds exactly the
 * same byte sequence from a screendump (propre C). */
static uint64_t anim_hash(const void *px, uint32_t stride, uint32_t w,
                          uint32_t h)
{
    const uint8_t *base = (const uint8_t *)px;
    uint64_t hl = 1469598103934665603ull;
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *row = base + (size_t)y * stride;
        for (uint32_t i = 0; i < w * 4u; i++) {
            hl ^= (uint64_t)row[i];
            hl *= 1099511628211ull;
        }
    }
    return hl;
}

static int anim_bench(int fd, const toile_gpu_fb_t *fb, void *px,
                      const anim_opts *o, const toile_anim_cfg_t *cfg)
{
    uint32_t t0 = now_ms();
    uint64_t present_us = 0u;
    for (uint32_t i = 0; i < o->bench; i++) {
        anim_band b = {px, fb->stride, fb->width, fb->height, fb->format,
                       i * 33u, i, 0u, fb->height, cfg};
        anim_paint(&b, o->threads);
        uint32_t p0 = now_ms();
        (void)toile_gpu_present(fd, NULL);
        present_us += (uint64_t)(now_ms() - p0) * 1000u;
    }
    uint32_t total = now_ms() - t0;
    uint64_t bytes = (uint64_t)o->bench * fb->width * fb->height * 4u;
    printf("BENCH: %ux%u frames=%u total_ms=%u paint_im_s=%.1f MB_s=%.1f "
           "present_us=%.0f threads=%u\n",
           (unsigned)fb->width, (unsigned)fb->height, (unsigned)o->bench,
           (unsigned)total,
           total > 0u ? (double)o->bench * 1000.0 / total : 0.0,
           total > 0u ? (double)bytes / 1e3 / (double)total : 0.0,
           (double)present_us / (double)(o->bench > 0u ? o->bench : 1u),
           (unsigned)o->threads);
    fflush(stdout);
    return 0;
}

static void anim_parse(int argc, char **argv, anim_opts *o)
{
    memset(o, 0, sizeof(*o));
    o->zones = 1u;
    o->threads = 1u;
    o->fps = ANIM_TARGET_FPS;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        int has_val = i + 1 < argc;
        if (has_val && strcmp(a, "--zone") == 0)
            o->zone = (uint32_t)atoi(argv[++i]);
        else if (has_val && strcmp(a, "--zones") == 0)
            o->zones = (uint32_t)atoi(argv[++i]);
        else if (has_val && strcmp(a, "--palette") == 0)
            o->palette = (uint32_t)atoi(argv[++i]);
        else if (has_val && strcmp(a, "--threads") == 0)
            o->threads = (uint32_t)atoi(argv[++i]);
        else if (has_val && strcmp(a, "--bench") == 0)
            o->bench = (uint32_t)atoi(argv[++i]);
        else if (has_val && strcmp(a, "--fps") == 0)
            o->fps = (uint32_t)atoi(argv[++i]);
        else if (strcmp(a, "--hash-hold") == 0)
            o->hash_hold = 1u;
    }
    if (o->zones == 0u)
        o->zones = 1u;
    if (o->zone >= o->zones)
        o->zone = o->zones - 1u;
    if (o->fps == 0u)
        o->fps = 1u;
    if (o->threads == 0u)
        o->threads = 1u;
}

/* Ouvre le noeud, recupere le framebuffer et l'etat d'animation partage. */
static int anim_ctx_open(anim_ctx *c)
{
    c->fd = anim_open();
    if (c->fd < 0) {
        printf("ANIM: ECHEC open /dev/gpu0\n");
        return -1;
    }
    toile_gpu_info_t info;
    if (toile_gpu_info(c->fd, &info) != 0 || toile_gpu_fb(c->fd, &c->fb) != 0) {
        printf("ANIM: ECHEC devctl INFO/FB\n");
        return -1;
    }
    c->px = toile_gpu_mmap(c->fd, &c->fb, NULL);
    if (c->px == NULL) {
        printf("ANIM: ECHEC mmap paddr=0x%llx bytes=%llu\n",
               (unsigned long long)c->fb.paddr, (unsigned long long)c->fb.bytes);
        return -1;
    }
    c->cfg.palette = 0u;
    c->cfg.speed = 1u;
    c->cfg.paused = 0u;
    if (toile_gpu_anim_get(c->fd, &c->cfg) != 0)
        c->cfg.speed = 1u;
    printf("ANIM: /dev/gpu0 ouvert (fd=%d)\n", c->fd);
    printf("ANIM: framebuffer mappe %ux%u stride=%u fmt=%u zone=%u/%u "
           "threads=%u fps=%u\n",
           (unsigned)c->fb.width, (unsigned)c->fb.height,
           (unsigned)c->fb.stride, (unsigned)c->fb.format, (unsigned)c->o.zone,
           (unsigned)c->o.zones, (unsigned)c->o.threads, (unsigned)c->o.fps);
    fflush(stdout);
    return 0;
}

/* Decoupe la surface : une bande verticale par client du mur. */
static void anim_ctx_zone(anim_ctx *c)
{
    uint32_t y0 = (uint32_t)((uint64_t)c->fb.height * c->o.zone / c->o.zones);
    uint32_t y1 =
        (uint32_t)((uint64_t)c->fb.height * (c->o.zone + 1u) / c->o.zones);
    c->rect.x = 0u;
    c->rect.y = y0;
    c->rect.w = c->fb.width;
    c->rect.h = y1 - y0;
    c->band.px = c->px;
    c->band.stride = c->fb.stride;
    c->band.w = c->fb.width;
    c->band.h = c->fb.height;
    c->band.fmt = c->fb.format;
    c->band.t = 0u;
    c->band.frame = 0u;
    c->band.y0 = y0;
    c->band.y1 = y1;
    c->band.cfg = &c->cfg;
    snprintf(c->titre, sizeof(c->titre), "TOILE %u/%u",
             (unsigned)(c->o.zone + 1u), (unsigned)c->o.zones);
    printf("ANIM: titre=%s y0=%u y1=%u pal_off=%u\n", c->titre, (unsigned)y0,
           (unsigned)y1, (unsigned)c->o.palette);
    fflush(stdout);
}

/* Compte-rendu periodique (une ligne toutes les ANIM_REPORT_MS). */
static void anim_report(const anim_ctx *c, uint32_t frames, uint32_t present_ko,
                         uint32_t t0, uint32_t *last)
{
    uint32_t now = now_ms();
    if (now - *last < ANIM_REPORT_MS)
        return;
    printf("ANIM: %u images, %.1f im/s, present_ko=%u, t=%u ms, "
           "palette=%u speed=%u paused=%u\n",
           (unsigned)frames,
           (double)frames * 1000.0 / (double)(now - t0),
           (unsigned)present_ko, (unsigned)(now - t0),
           (unsigned)c->cfg.palette, (unsigned)c->cfg.speed,
           (unsigned)c->cfg.paused);
    fflush(stdout);
    *last = now;
}

/* Empreinte FNV-1a de l'image courante, puis tenue 2,5 s : l'hote capture
 * exactement la meme image (PREUVE C, transit invite -> emulateur). */
static void anim_hash_hold(const anim_ctx *c, uint32_t frames)
{
    printf("HASH: frame=%u w=%u h=%u stride=%u fnv=%016llx hold_ms=%u\n",
           (unsigned)frames, (unsigned)c->fb.width, (unsigned)c->fb.height,
           (unsigned)c->fb.stride,
           (unsigned long long)anim_hash(c->px, c->fb.stride, c->fb.width,
                                         c->fb.height),
           (unsigned)ANIM_HOLD_MS);
    fflush(stdout);
    wait_ms(ANIM_HOLD_MS);
}

/* Boucle d'animation : peint, presente, cadence. */
static int anim_run(anim_ctx *c)
{
    uint32_t t0 = now_ms();
    uint32_t frames = 0u, last = t0, present_ko = 0u, frozen_t = 0u;
    for (;;) {
        if (frames % ANIM_STATE_EVERY == 0u)
            (void)toile_gpu_anim_get(c->fd, &c->cfg);
        toile_anim_cfg_t local = c->cfg; /* decalage de palette par panneau */
        local.palette = (c->cfg.palette + c->o.palette) % TOILE_ANIM_PALETTES;
        if (!c->cfg.paused)
            frozen_t = (now_ms() - t0) * (c->cfg.speed > 8u ? 8u : c->cfg.speed);
        c->band.t = frozen_t;
        c->band.frame = frames;
        c->band.cfg = &local;
        anim_paint(&c->band, c->o.threads);
        toile_anim_overlay(c->px, c->fb.stride, c->fb.width, c->fb.height,
                           c->fb.format, frozen_t, frames, c->titre,
                           c->band.y0, c->band.y1);
        if (toile_gpu_present(c->fd, c->o.zones > 1u ? &c->rect : NULL) != 0)
            present_ko++;
        if (c->o.hash_hold && frames % ANIM_HASH_EVERY == 0u)
            anim_hash_hold(c, frames);
        frames++;
        anim_report(c, frames, present_ko, t0, &last);
        uint32_t due = t0 + (uint32_t)((uint64_t)frames * 1000u / c->o.fps);
        uint32_t now = now_ms();
        if (due > now)
            wait_ms(due - now);
    }
    return 0; /* unreachable : boucle infinie */
}

int main(int argc, char **argv)
{
    anim_ctx c;
    memset(&c, 0, sizeof(c));
    anim_parse(argc, argv, &c.o);
    if (anim_ctx_open(&c) != 0)
        return 1;
    if (c.o.bench > 0u)
        return anim_bench(c.fd, &c.fb, c.px, &c.o, &c.cfg);
    anim_ctx_zone(&c);
    return anim_run(&c);
}
