/* SPDX-License-Identifier: MIT */
/*
 * Toile — DSL fixture parser (CONTRACT.md sections 3 and 4).
 *
 * One command per line: "name key=value ...". No allocation, no I/O: tokens
 * are read in place from the caller's line buffer. Values are collected in a
 * plain struct and copied into the command union only once the line is
 * valid, so no union slot is ever aliased by another key.
 */
#include <string.h>

#include "toile/cmdproc.h"

/* --- string helpers (no <string.h>: zero dependency) --------------------- */

static int tok_eq(const char *a, size_t n, const char *lit)
{
    size_t i = 0;

    while (i < n && lit[i] && a[i] == lit[i])
        i++;
    return i == n && lit[i] == '\0';
}

static int is_space(int c)
{
    return c == ' ' || c == '\t';
}

/* --- integer parsing: decimal or 0x... ----------------------------------- */

static int parse_uint_tok(const char *s, size_t n, uint64_t *out)
{
    uint64_t v = 0;
    size_t i = 0;
    int base = 10, d;

    if (n == 0)
        return 0;
    if (n >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    }
    if (i == n)
        return 0;
    for (; i < n; i++) {
        unsigned c = (unsigned char)s[i];

        if (c >= '0' && c <= '9') d = (int)c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = (int)c - 'a' + 10;
        else if (base == 16 && c >= 'A' && c <= 'F') d = (int)c - 'A' + 10;
        else return 0;
        if (v > (UINT64_MAX - (uint64_t)d) / (uint64_t)base)
            return 0; /* overflow */
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return 1;
}

/* --- parsed values -------------------------------------------------------- */

typedef struct pvals {
    uint32_t id, format, width, height, scanout, x, y, hot_x, hot_y, n_pages;
    uint64_t offset;
    toile_page pages[TOILE_MAX_PAGES];
} pvals;

typedef struct pctx {
    toile_cmd *cmd;
    pvals *vals;
    char *err;
    size_t err_cap;
    unsigned long line_no;
    const char *line;
    size_t pos;
    unsigned long seen; /* bit per key: duplicate detection */
    const char *reason; /* specific reason of the last failure, or NULL */
} pctx;

/* Writes "<line_no>:<reason>" into err (truncated, NUL guaranteed) and
 * reports the invalid command. */
static int fail(pctx *p, const char *reason)
{
    char *err = p->err;
    size_t cap = p->err_cap;
    char prefix[24];
    size_t i = 0, digits = 0;
    unsigned long v = p->line_no;

    if (!err || cap == 0)
        return TOILE_CMD_INVALID;
    do {
        prefix[digits++] = (char)('0' + (v % 10));
        v /= 10;
    } while (v && digits < 20);
    while (digits > 0 && i + 1 < cap)
        err[i++] = prefix[--digits];
    if (i + 1 < cap)
        err[i++] = ':';
    while (*reason && i + 1 < cap)
        err[i++] = *reason++;
    err[i] = '\0';
    return TOILE_CMD_INVALID;
}

/* --- command names (CONTRACT.md section 3, via the frozen name table) ----- */

static const uint32_t cmd_types[] = {
    TOILE_VG_CMD_GET_DISPLAY_INFO, TOILE_VG_CMD_RESOURCE_CREATE_2D,
    TOILE_VG_CMD_RESOURCE_UNREF, TOILE_VG_CMD_SET_SCANOUT,
    TOILE_VG_CMD_RESOURCE_FLUSH, TOILE_VG_CMD_TRANSFER_TO_HOST_2D,
    TOILE_VG_CMD_RESOURCE_ATTACH_BACKING, TOILE_VG_CMD_RESOURCE_DETACH_BACKING,
    TOILE_VG_CMD_TRANSFER_FROM_HOST_3D, TOILE_VG_CMD_UPDATE_CURSOR,
    TOILE_VG_CMD_MOVE_CURSOR,
};

static uint32_t name_to_type(const char *s, size_t n)
{
    for (size_t i = 0; i < sizeof cmd_types / sizeof cmd_types[0]; i++) {
        const char *nm = toile_vg_cmd_name(cmd_types[i]);

        if (nm && tok_eq(s, n, nm))
            return cmd_types[i];
    }
    return 0;
}

/* --- key table with aliasing (id=resource, w=width, h=height) ------------- */

typedef struct key_ent {
    const char *name;
    unsigned bit;
} key_ent;

static const key_ent keys[] = {
    { "id", 0 }, { "resource", 0 }, { "format", 1 }, { "width", 2 },
    { "w", 2 }, { "height", 3 }, { "h", 3 }, { "scanout", 4 }, { "x", 5 },
    { "y", 6 }, { "offset", 7 }, { "pages", 8 }, { "hot_x", 9 },
    { "hot_y", 10 },
};

static int key_lookup(const char *s, size_t n, unsigned *bit)
{
    for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
        if (tok_eq(s, n, keys[i].name)) {
            *bit = keys[i].bit;
            return 1;
        }
    }
    return 0;
}

/* Required key bitmask per command type (CONTRACT.md section 3). */
static unsigned long required_mask(uint32_t type)
{
    switch (type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:      return 0xFUL;
    case TOILE_VG_CMD_RESOURCE_UNREF:
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
    case TOILE_VG_CMD_RESOURCE_FLUSH:
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:   return 0x1UL;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING: return 0x101UL;
    case TOILE_VG_CMD_UPDATE_CURSOR:
    case TOILE_VG_CMD_MOVE_CURSOR:             return 0x70UL;
    case TOILE_VG_CMD_SET_SCANOUT:             return 0x7DUL;
    default:                                   return 0UL;
    }
}

/* --- pages=addr:len[,addr:len] (at most TOILE_MAX_PAGES entries) ---------- */

static int parse_pages(pvals *d, const char *v, size_t n)
{
    size_t pos = 0;

    d->n_pages = 0;
    if (n == 0)
        return 0;
    while (pos < n) {
        size_t cs = pos, ce = pos, k = pos;
        uint64_t addr = 0, len = 0;

        while (ce < n && v[ce] != ',')
            ce++;
        while (k < ce && v[k] != ':')
            k++;
        if (k == cs || k == ce)
            return 0; /* no separator */
        if (!parse_uint_tok(v + cs, k - cs, &addr) ||
            !parse_uint_tok(v + k + 1, ce - k - 1, &len))
            return 0;
        if (d->n_pages >= TOILE_MAX_PAGES)
            return -1; /* more than 32 entries */
        d->pages[d->n_pages].addr = addr;
        d->pages[d->n_pages].length = (uint32_t)len;
        d->n_pages++;
        pos = (ce < n) ? ce + 1 : ce;
        if (ce < n && pos == n)
            return 0; /* trailing comma */
    }
    return d->n_pages > 0;
}

static int parse_fmt_tok(const char *v, size_t n, uint32_t *dst)
{
    char buf[24];
    toile_fmt f = TOILE_FMT_INVALID;

    if (n == 0 || n >= sizeof buf)
        return 0;
    for (size_t i = 0; i < n; i++)
        buf[i] = v[i];
    buf[n] = '\0';
    if (toile_fmt_parse(buf, &f) != 0)
        return 0;
    *dst = (uint32_t)f;
    return 1;
}

/* Stores one key value in the flat value set. */
static int set_kv(pctx *p, unsigned bit, const char *v, size_t n)
{
    pvals *d = p->vals;
    uint64_t u = 0;

    if (bit == 8) {
        int r = parse_pages(d, v, n);

        if (r < 0)
            p->reason = "backing_entry_count";
        return r > 0;
    }
    if (bit == 1)
        return parse_fmt_tok(v, n, &d->format);
    if (!parse_uint_tok(v, n, &u))
        return 0;
    switch (bit) {
    case 0: d->id = (uint32_t)u; break;
    case 2: d->width = (uint32_t)u; break;
    case 3: d->height = (uint32_t)u; break;
    case 4: d->scanout = (uint32_t)u; break;
    case 5: d->x = (uint32_t)u; break;
    case 6: d->y = (uint32_t)u; break;
    case 7: d->offset = u; break;
    case 9: d->hot_x = (uint32_t)u; break;
    default: d->hot_y = (uint32_t)u; break;
    }
    return 1;
}

/* --- copy the parsed values into the command union ------------------------ */

static toile_rect rect_of(const pvals *d)
{
    return (toile_rect){ d->x, d->y, d->width, d->height };
}

static void finalize(toile_cmd *c, const pvals *d)
{
    switch (c->type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        c->u.create_2d.resource_id = d->id; c->u.create_2d.format = d->format;
        c->u.create_2d.width = d->width; c->u.create_2d.height = d->height;
        break;
    case TOILE_VG_CMD_RESOURCE_UNREF: c->u.unref.resource_id = d->id; break;
    case TOILE_VG_CMD_SET_SCANOUT:
        c->u.scanout.resource_id = d->id; c->u.scanout.scanout_id = d->scanout;
        c->u.scanout.rect = rect_of(d);
        break;
    case TOILE_VG_CMD_RESOURCE_FLUSH:
        c->u.flush.resource_id = d->id; c->u.flush.rect = rect_of(d);
        break;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:
        c->u.xfer.resource_id = d->id; c->u.xfer.rect = rect_of(d);
        c->u.xfer.offset = d->offset;
        break;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        c->u.backing.resource_id = d->id; c->u.backing.nr_entries = d->n_pages;
        for (uint32_t i = 0; i < d->n_pages; i++)
            c->u.backing.pages[i] = d->pages[i];
        break;
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
        c->u.backing.resource_id = d->id;
        break;
    case TOILE_VG_CMD_UPDATE_CURSOR:
    case TOILE_VG_CMD_MOVE_CURSOR:
        c->u.cursor.scanout_id = d->scanout; c->u.cursor.x = d->x;
        c->u.cursor.y = d->y;
        if (c->type == TOILE_VG_CMD_UPDATE_CURSOR) {
            c->u.cursor.resource_id = d->id;
            c->u.cursor.hot_x = d->hot_x; c->u.cursor.hot_y = d->hot_y;
        }
        break;
    default:
        break;
    }
}

static int finish_cmd(pctx *p)
{
    unsigned long req = required_mask(p->cmd->type);

    if ((p->seen & req) != req)
        return fail(p, "missing_required_key");
    if (p->cmd->type == TOILE_VG_CMD_RESOURCE_ATTACH_BACKING &&
        p->vals->n_pages == 0)
        return fail(p, "backing_entry_count");
    return 0;
}

/* Parses the "key=value" list of the current line. */
static int parse_keys(pctx *p)
{
    const char *line = p->line;

    while (line[p->pos]) {
        size_t eq, vs, ve;
        unsigned bit = 0;

        while (is_space((unsigned char)line[p->pos]))
            p->pos++;
        if (!line[p->pos])
            break;
        ve = p->pos;
        while (line[ve] && !is_space((unsigned char)line[ve]))
            ve++;
        eq = p->pos;
        while (eq < ve && line[eq] != '=')
            eq++;
        if (eq == p->pos || eq == ve ||
            !key_lookup(line + p->pos, eq - p->pos, &bit))
            return fail(p, "unknown_key");
        if (p->seen & (1UL << bit))
            return fail(p, "duplicate_key");
        vs = eq + 1;
        if (vs == ve)
            return fail(p, "bad_value");
        if (!set_kv(p, bit, line + vs, ve - vs))
            return fail(p, p->reason ? p->reason : "bad_value");
        p->seen |= 1UL << bit;
        p->pos = ve;
    }
    return 0;
}

int toile_cmd_parse_line(const char *line, unsigned long line_no,
                         toile_cmd *out, char *err, size_t err_cap)
{
    pctx p;
    pvals vals;
    size_t start;

    if (err && err_cap > 0)
        err[0] = '\0';
    if (!line || !out)
        return TOILE_CMD_INVALID;
    memset(&vals, 0, sizeof vals);
    memset(out, 0, sizeof *out);
    p = (pctx){ out, &vals, err, err_cap, line_no, line, 0, 0, NULL };
    while (is_space((unsigned char)line[p.pos]))
        p.pos++;
    if (line[p.pos] == '\0' || line[p.pos] == '#')
        return 0; /* blank line or comment: out->type stays 0 */
    start = p.pos;
    while (line[p.pos] && !is_space((unsigned char)line[p.pos]))
        p.pos++;
    out->type = name_to_type(line + start, p.pos - start);
    if (out->type == 0)
        return fail(&p, "unknown_command");
    if (parse_keys(&p) != 0)
        return TOILE_CMD_INVALID;
    if (finish_cmd(&p) != 0)
        return TOILE_CMD_INVALID;
    finalize(out, &vals);
    return 0;
}

const char *toile_cmd_name(uint32_t type)
{
    return toile_vg_cmd_name(type);
}

size_t toile_verdict_count_severity(const toile_verdict *v, size_t n,
                                    const char *severity)
{
    size_t count = 0;

    if (!v || !severity)
        return 0;
    for (size_t i = 0; i < n; i++) {
        const char *s = toile_verdict_severity(v[i].code);
        size_t k = 0;

        while (s[k] && s[k] == severity[k])
            k++;
        if (s[k] == '\0' && severity[k] == '\0')
            count++;
    }
    return count;
}
