/* SPDX-License-Identifier: MIT */
/*
 * Toile — canonical state serialization and FNV-1a 64-bit hash
 * (CONTRACT.md §8). No allocation: lines are rendered in a small local
 * buffer and streamed to a sink (hash accumulator or caller string).
 *
 * Canonical layout, active entries only:
 *   "toile-state-v1\n"
 *   R id format width height n_pages      (resources, ascending ids)
 *   P %016x_addr length                   (pages, attachment order,
 *                                          right after their R line)
 *   S scanout resource x y w h flush_count (scanouts, ascending ids)
 *   C resource scanout x y hot_x hot_y visible
 *   K applied rejected flushes bytes
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "toile/resource.h"
#include "toile/vgproto.h"

#define TOILE_HASH_LINE_MAX 128u
#define TOILE_FNV64_BASE    UINT64_C(0xcbf29ce484222325)
#define TOILE_FNV64_PRIME   UINT64_C(0x100000001b3)
#define TOILE_FNV64_MASK    UINT64_C(0xFFFFFFFFFFFFFFFF)

typedef void (*toile_sink_fn)(const char *bytes, size_t n, void *ctx);

/* Active resource with the smallest id greater than `after` (use -1 to
 * start); NULL at end. int64_t so that id 0 is reachable. */
static const toile_resource *next_resource(const toile_model *m, int64_t after)
{
    const toile_resource *best = NULL;
    size_t i;

    for (i = 0; i < TOILE_MAX_RESOURCES; i++) {
        const toile_resource *r = &m->resources[i];

        if (!r->in_use || (int64_t)r->id <= after)
            continue;
        if (best == NULL || r->id < best->id)
            best = r;
    }
    return best;
}

/* Active scanout with the smallest id greater than `after` (use -1 to
 * start); NULL at end. */
static const toile_scanout *next_scanout(const toile_model *m, int64_t after)
{
    const toile_scanout *best = NULL;
    size_t i;

    for (i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        const toile_scanout *s = &m->scanouts[i];

        if (!s->in_use || (int64_t)s->id <= after)
            continue;
        if (best == NULL || s->id < best->id)
            best = s;
    }
    return best;
}

/* Render one printf-formatted line and stream it to the sink. */
static void emitf(toile_sink_fn fn, void *ctx, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void emitf(toile_sink_fn fn, void *ctx, const char *fmt, ...)
{
    char line[TOILE_HASH_LINE_MAX];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (n > 0)
        fn(line, (size_t)n < sizeof line ? (size_t)n : sizeof line - 1, ctx);
}

/* Walk the whole canonical serialization (CONTRACT.md §8). */
static void state_walk(const toile_model *m, toile_sink_fn fn, void *ctx)
{
    const toile_resource *r;
    const toile_scanout *s;
    int64_t prev = -1;
    uint32_t i;

    fn("toile-state-v1\n", 15, ctx);
    for (r = next_resource(m, prev); r != NULL; r = next_resource(m, prev)) {
        prev = r->id;
        emitf(fn, ctx, "R %u %u %u %u %u\n", r->id, (unsigned)r->fmt,
              r->width, r->height, r->n_pages);
        for (i = 0; i < r->n_pages && i < TOILE_MAX_PAGES; i++)
            emitf(fn, ctx, "P %016" PRIx64 " %u\n", r->pages[i].addr,
                  r->pages[i].length);
    }
    prev = -1;
    for (s = next_scanout(m, prev); s != NULL; s = next_scanout(m, prev)) {
        const toile_resource *sr = toile_resource_find(m, s->resource_id);

        prev = s->id;
        emitf(fn, ctx, "S %u %u %u %u %u %u %" PRIu64 "\n", s->id,
              s->resource_id, s->rect.x, s->rect.y, s->rect.w, s->rect.h,
              sr != NULL ? sr->flush_count : UINT64_C(0));
    }
    emitf(fn, ctx, "C %u %u %u %u %u %u %u\n", m->cursor.resource_id,
          m->cursor.scanout_id, m->cursor.x, m->cursor.y, m->cursor.hot_x,
          m->cursor.hot_y, m->cursor.visible);
    emitf(fn, ctx, "K %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
          m->cmds_applied, m->cmds_rejected, m->flushes, m->bytes_transferred);
}

/* FNV-1a 64-bit sink. */
static void sink_hash(const char *bytes, size_t n, void *ctx)
{
    uint64_t *h = ctx;
    size_t i;

    for (i = 0; i < n; i++) {
        *h ^= (uint64_t)(unsigned char)bytes[i];
        *h = (*h * TOILE_FNV64_PRIME) & TOILE_FNV64_MASK;
    }
}

uint64_t toile_model_state_hash(const toile_model *m)
{
    uint64_t h = TOILE_FNV64_BASE;

    if (m == NULL)
        return h;
    state_walk(m, sink_hash, &h);
    return h;
}

/* String sink: appends to a caller buffer (snprintf truncation semantics). */
typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} toile_str_sink;

static void sink_str(const char *bytes, size_t n, void *ctx)
{
    toile_str_sink *s = ctx;
    size_t room;

    if (s->buf != NULL && s->len + 1 < s->cap) {
        room = s->cap - 1 - s->len;
        memcpy(s->buf + s->len, bytes, n < room ? n : room);
        s->len += n < room ? n : room;
        s->buf[s->len] = '\0';
    } else {
        s->len += n;
    }
}

/*
 * Alignment helper (not part of the frozen §10 API): renders the canonical
 * §8 serialization into buf. Returns the full canonical length; the output
 * is NUL-terminated and truncated to cap-1 bytes when cap is too small.
 * buf may be NULL to only measure the length. Used by tests and by the
 * C/Python differential harness to display the hashed bytes.
 */
size_t toile_model_state_serialize(const toile_model *m, char *buf, size_t cap)
{
    toile_str_sink s;

    s.buf = buf;
    s.cap = cap;
    s.len = 0;
    if (buf != NULL && cap > 0)
        buf[0] = '\0';
    if (m == NULL)
        return 0;
    state_walk(m, sink_str, &s);
    return s.len;
}
