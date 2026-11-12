/* SPDX-License-Identifier: MIT */
/*
 * Toile — couche fil virtio-gpu 2D : implémentation de toile/vgproto.h.
 *
 * Encodage/décodage petit-boutien EXPLICITE (helpers put_leNN / get_leNN),
 * jamais de memcpy de structure. Disposition fil (CONTRACT.md §3, §9) :
 * en-tête de contrôle 24 octets (type u32, flags u32, fence_id u64,
 * ctx_id u32, ring_idx u8, padding[3]) puis la charge utile :
 *   rect          = x,y,w,h          (4 × u32, 16 o)
 *   create_2d     = id,format,w,h    (4 × u32)
 *   unref/detach  = id,padding       (2 × u32)
 *   set_scanout   = rect,scanout,id  (6 × u32)  — ordre du standard
 *   flush         = rect,id,padding  (6 × u32)
 *   xfer (2d/3d)  = rect,offset u64,id,padding (8 × u32) — ordre du standard
 *   attach        = id,nr_entries    (2 × u32) + nr × mem_entry (u64 addr,
 *                   u32 length, u32 padding = 16 o)
 *   cursor_pos    = scanout,x,y,padding (4 × u32)
 *   update_cursor = cursor_pos + id,hot_x,hot_y,padding (8 × u32)
 *   move_cursor   = cursor_pos       (4 × u32)
 */
#include "toile/vgproto.h"

#include "toile/verdict.h" /* TOILE_CMD_INVALID (code de retour) */

/* --- helpers petit-boutiens explicites ----------------------------------- */

static void put_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put_le64(uint8_t *p, uint64_t v)
{
    put_le32(p, (uint32_t)(v & 0xFFFFFFFFu));
    put_le32(p + 4, (uint32_t)(v >> 32));
}

static uint32_t get_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_le64(const uint8_t *p)
{
    return (uint64_t)get_le32(p) | ((uint64_t)get_le32(p + 4) << 32);
}

/* --- formats de pixels ---------------------------------------------------- */

uint32_t toile_fmt_bpp(toile_fmt fmt)
{
    switch (fmt) {
    case TOILE_FMT_B8G8R8A8_UNORM:
    case TOILE_FMT_B8G8R8X8_UNORM:
    case TOILE_FMT_A8R8G8B8_UNORM:
    case TOILE_FMT_X8R8G8B8_UNORM:
    case TOILE_FMT_R8G8B8A8_UNORM:
    case TOILE_FMT_X8B8G8R8_UNORM:
    case TOILE_FMT_A8B8G8R8_UNORM:
    case TOILE_FMT_R8G8B8X8_UNORM:
        return 4u; /* §3 : tous à 4 octets/pixel */
    default:
        return 0u;
    }
}

static const struct { toile_fmt fmt; const char *name; } fmt_table[] = {
    { TOILE_FMT_B8G8R8A8_UNORM, "B8G8R8A8_UNORM" },
    { TOILE_FMT_B8G8R8X8_UNORM, "B8G8R8X8_UNORM" },
    { TOILE_FMT_A8R8G8B8_UNORM, "A8R8G8B8_UNORM" },
    { TOILE_FMT_X8R8G8B8_UNORM, "X8R8G8B8_UNORM" },
    { TOILE_FMT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM" },
    { TOILE_FMT_X8B8G8R8_UNORM, "X8B8G8R8_UNORM" },
    { TOILE_FMT_A8B8G8R8_UNORM, "A8B8G8R8_UNORM" },
    { TOILE_FMT_R8G8B8X8_UNORM, "R8G8B8X8_UNORM" },
};

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

const char *toile_fmt_name(toile_fmt fmt)
{
    for (size_t i = 0; i < sizeof fmt_table / sizeof fmt_table[0]; i++) {
        if (fmt_table[i].fmt == fmt)
            return fmt_table[i].name;
    }
    return NULL;
}

/* Nombre décimal ou 0x… (sans préfixe de type) ; NULL si refusé. */
static const char *parse_uint(const char *s, uint32_t *out)
{
    uint32_t base = 10u;
    uint64_t v = 0u;

    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16u;
        s += 2;
    }
    if (*s == '\0')
        return NULL;
    for (;;) {
        unsigned d;
        if (*s >= '0' && *s <= '9')
            d = (unsigned)(*s - '0');
        else if (base == 16u && *s >= 'a' && *s <= 'f')
            d = (unsigned)(*s - 'a') + 10u;
        else if (base == 16u && *s >= 'A' && *s <= 'F')
            d = (unsigned)(*s - 'A') + 10u;
        else
            break;
        v = v * base + d;
        if (v > 0xFFFFFFFFull)
            return NULL;
        s++;
    }
    *out = (uint32_t)v;
    return s;
}

int toile_fmt_parse(const char *name, toile_fmt *out)
{
    uint32_t v = 0u;

    if (!name || !out || !*name)
        return TOILE_CMD_INVALID;
    for (size_t i = 0; i < sizeof fmt_table / sizeof fmt_table[0]; i++) {
        if (str_eq(name, fmt_table[i].name)) {
            *out = fmt_table[i].fmt;
            return 0;
        }
    }
    if (!parse_uint(name, &v) || toile_fmt_bpp((toile_fmt)v) == 0u)
        return TOILE_CMD_INVALID;
    *out = (toile_fmt)v;
    return 0;
}

const char *toile_vg_cmd_name(uint32_t type)
{
    switch (type) {
    case TOILE_VG_CMD_GET_DISPLAY_INFO:        return "get_display_info";
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:      return "resource_create_2d";
    case TOILE_VG_CMD_RESOURCE_UNREF:          return "resource_unref";
    case TOILE_VG_CMD_SET_SCANOUT:             return "set_scanout";
    case TOILE_VG_CMD_RESOURCE_FLUSH:          return "resource_flush";
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:     return "transfer_to_host_2d";
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING: return "resource_attach_backing";
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING: return "resource_detach_backing";
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:   return "transfer_from_host";
    case TOILE_VG_CMD_UPDATE_CURSOR:           return "update_cursor";
    case TOILE_VG_CMD_MOVE_CURSOR:             return "move_cursor";
    default:                                   return NULL;
    }
}

/* --- encodeurs (petit-boutien explicite) ---------------------------------- */

static void enc_hdr(uint8_t *p, const toile_cmd *cmd)
{
    put_le32(p, cmd->type);
    put_le32(p + 4, cmd->flags);
    put_le64(p + 8, cmd->fence_id);
    put_le32(p + 16, 0u); /* ctx_id : hors socle 2D */
    p[20] = 0u;           /* ring_idx */
    p[21] = 0u;
    p[22] = 0u;
    p[23] = 0u;
}

static void enc_rect(uint8_t *p, const toile_rect *r)
{
    put_le32(p, r->x);
    put_le32(p + 4, r->y);
    put_le32(p + 8, r->w);
    put_le32(p + 12, r->h);
}

static void enc_cursor_pos(uint8_t *p, const toile_cmd *cmd)
{
    put_le32(p, cmd->u.cursor.scanout_id);
    put_le32(p + 4, cmd->u.cursor.x);
    put_le32(p + 8, cmd->u.cursor.y);
    put_le32(p + 12, 0u);
}

static void enc_xfer(uint8_t *p, const toile_cmd *cmd)
{
    enc_rect(p, &cmd->u.xfer.rect);
    put_le64(p + 16, cmd->u.xfer.offset);
    put_le32(p + 24, cmd->u.xfer.resource_id);
    put_le32(p + 28, 0u);
}

static void enc_set_scanout(uint8_t *p, const toile_cmd *cmd)
{
    enc_rect(p, &cmd->u.scanout.rect);
    put_le32(p + 16, cmd->u.scanout.scanout_id);
    put_le32(p + 20, cmd->u.scanout.resource_id);
}

static void enc_flush(uint8_t *p, const toile_cmd *cmd)
{
    enc_rect(p, &cmd->u.flush.rect);
    put_le32(p + 16, cmd->u.flush.resource_id);
    put_le32(p + 20, 0u);
}

static void enc_attach(uint8_t *p, const toile_cmd *cmd)
{
    uint32_t n = cmd->u.backing.nr_entries;

    put_le32(p, cmd->u.backing.resource_id);
    put_le32(p + 4, n); /* nr_entries : partie fixe = 8 o (cf. TOILE_VG_MAX_CMD_LEN) */
    p += 8;
    for (uint32_t i = 0; i < n; i++) {
        put_le64(p, cmd->u.backing.pages[i].addr);
        put_le32(p + 8, cmd->u.backing.pages[i].length);
        put_le32(p + 12, 0u);
        p += 16;
    }
}

static void enc_cursor(uint8_t *p, const toile_cmd *cmd)
{
    enc_cursor_pos(p, cmd);
    if (cmd->type == TOILE_VG_CMD_UPDATE_CURSOR) {
        put_le32(p + 16, cmd->u.cursor.resource_id);
        put_le32(p + 20, cmd->u.cursor.hot_x);
        put_le32(p + 24, cmd->u.cursor.hot_y);
        put_le32(p + 28, 0u);
    }
}

/* Longueur de charge utile pour un type connu (attach : -1, variable). */
static size_t payload_len(uint32_t type, uint32_t nr_entries)
{
    switch (type) {
    case TOILE_VG_CMD_GET_DISPLAY_INFO:        return 0u;
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:      return 16u;
    case TOILE_VG_CMD_RESOURCE_UNREF:          return 8u;
    case TOILE_VG_CMD_SET_SCANOUT:             return 24u;
    case TOILE_VG_CMD_RESOURCE_FLUSH:          return 24u;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:     return 32u;
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:   return 32u;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING: return 8u + (size_t)nr_entries * 16u;
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING: return 8u;
    case TOILE_VG_CMD_UPDATE_CURSOR:           return 32u;
    case TOILE_VG_CMD_MOVE_CURSOR:             return 16u;
    default:                                   return (size_t)-1;
    }
}

/* Encode la charge utile (hors en-tête de 24 octets) selon cmd->type. */
static void enc_payload(uint8_t *p, const toile_cmd *cmd)
{
    switch (cmd->type) {
    case TOILE_VG_CMD_GET_DISPLAY_INFO:
        break;
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        put_le32(p, cmd->u.create_2d.resource_id);
        put_le32(p + 4, cmd->u.create_2d.format);
        put_le32(p + 8, cmd->u.create_2d.width);
        put_le32(p + 12, cmd->u.create_2d.height);
        break;
    case TOILE_VG_CMD_RESOURCE_UNREF:
        put_le32(p, cmd->u.unref.resource_id);
        put_le32(p + 4, 0u);
        break;
    case TOILE_VG_CMD_SET_SCANOUT:
        enc_set_scanout(p, cmd);
        break;
    case TOILE_VG_CMD_RESOURCE_FLUSH:
        enc_flush(p, cmd);
        break;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:
        enc_xfer(p, cmd);
        break;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        enc_attach(p, cmd);
        break;
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
        put_le32(p, cmd->u.backing.resource_id);
        put_le32(p + 4, 0u);
        break;
    case TOILE_VG_CMD_UPDATE_CURSOR:
    case TOILE_VG_CMD_MOVE_CURSOR:
        enc_cursor(p, cmd);
        break;
    default:
        break; /* inaccessible : type filtré par toile_vg_encode */
    }
}

int toile_vg_encode(const toile_cmd *cmd, uint8_t *buf, size_t cap,
                    size_t *written)
{
    size_t need;

    if (!cmd || !buf || !written)
        return TOILE_CMD_INVALID;
    if (cmd->type == TOILE_VG_CMD_RESOURCE_ATTACH_BACKING &&
        cmd->u.backing.nr_entries > TOILE_MAX_PAGES)
        return TOILE_CMD_INVALID;
    need = payload_len(cmd->type, cmd->u.backing.nr_entries);
    if (need == (size_t)-1 || cap < TOILE_VG_HDR_LEN + need)
        return TOILE_CMD_INVALID;
    enc_hdr(buf, cmd);
    enc_payload(buf + TOILE_VG_HDR_LEN, cmd);
    *written = TOILE_VG_HDR_LEN + need;
    return 0;
}

/* --- décodeurs ------------------------------------------------------------- */

static void dec_hdr(const uint8_t *p, toile_cmd *out)
{
    out->type = get_le32(p);
    out->flags = get_le32(p + 4);
    out->fence_id = get_le64(p + 8);
    /* ctx_id, ring_idx, padding : lus mais hors modèle 2D */
}

static void dec_rect(const uint8_t *p, toile_rect *r)
{
    r->x = get_le32(p);
    r->y = get_le32(p + 4);
    r->w = get_le32(p + 8);
    r->h = get_le32(p + 12);
}

static void dec_xfer(const uint8_t *p, toile_cmd *out)
{
    dec_rect(p, &out->u.xfer.rect);
    out->u.xfer.offset = get_le64(p + 16);
    out->u.xfer.resource_id = get_le32(p + 24);
}

static void dec_backing(const uint8_t *p, size_t plen, toile_cmd *out)
{
    uint32_t n;

    if (plen < 8u)
        return;
    out->u.backing.resource_id = get_le32(p);
    n = get_le32(p + 4);
    if (n > TOILE_MAX_PAGES || plen != 8u + (size_t)n * 16u) {
        out->type = TOILE_ID_NONE; /* signal interne : longueur incohérente */
        return;
    }
    out->u.backing.nr_entries = n;
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t *e = p + 8u + (size_t)i * 16u;

        out->u.backing.pages[i].addr = get_le64(e);
        out->u.backing.pages[i].length = get_le32(e + 8);
    }
}

static void dec_cursor(const uint8_t *p, uint32_t type, toile_cmd *out)
{
    out->u.cursor.scanout_id = get_le32(p);
    out->u.cursor.x = get_le32(p + 4);
    out->u.cursor.y = get_le32(p + 8);
    if (type == TOILE_VG_CMD_UPDATE_CURSOR) {
        out->u.cursor.resource_id = get_le32(p + 16);
        out->u.cursor.hot_x = get_le32(p + 20);
        out->u.cursor.hot_y = get_le32(p + 24);
    } else {
        out->u.cursor.resource_id = 0u;
        out->u.cursor.hot_x = 0u;
        out->u.cursor.hot_y = 0u;
    }
}


/* Décode la charge utile selon out->type ; 0 = OK, TOILE_CMD_INVALID sinon. */
static int dec_payload(const uint8_t *p, size_t plen, toile_cmd *out)
{
    switch (out->type) {
    case TOILE_VG_CMD_GET_DISPLAY_INFO:
        break;
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        out->u.create_2d.resource_id = get_le32(p);
        out->u.create_2d.format = get_le32(p + 4);
        out->u.create_2d.width = get_le32(p + 8);
        out->u.create_2d.height = get_le32(p + 12);
        break;
    case TOILE_VG_CMD_RESOURCE_UNREF:
        out->u.unref.resource_id = get_le32(p);
        break;
    case TOILE_VG_CMD_SET_SCANOUT:
        dec_rect(p, &out->u.scanout.rect);
        out->u.scanout.scanout_id = get_le32(p + 16);
        out->u.scanout.resource_id = get_le32(p + 20);
        break;
    case TOILE_VG_CMD_RESOURCE_FLUSH:
        dec_rect(p, &out->u.flush.rect);
        out->u.flush.resource_id = get_le32(p + 16);
        break;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:
        dec_xfer(p, out);
        break;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        dec_backing(p, plen, out);
        if (out->type == TOILE_ID_NONE)
            return TOILE_CMD_INVALID; /* longueur incohérente ou n > max */
        break;
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
        out->u.backing.resource_id = get_le32(p);
        out->u.backing.nr_entries = 0u;
        break;
    case TOILE_VG_CMD_UPDATE_CURSOR:
    case TOILE_VG_CMD_MOVE_CURSOR:
        dec_cursor(p, out->type, out);
        break;
    default:
        return TOILE_CMD_INVALID; /* inaccessible : type filtré avant */
    }
    return 0;
}

int toile_vg_decode(const uint8_t *buf, size_t len, toile_cmd *out)
{
    uint32_t type;
    size_t need, plen;
    int rc;

    if (!buf || !out || len < TOILE_VG_HDR_LEN)
        return TOILE_CMD_INVALID;
    type = get_le32(buf);
    need = (type == TOILE_VG_CMD_RESOURCE_ATTACH_BACKING)
               ? 8u /* prérequis fixe ; les entrées sont vérifiées à part */
               : payload_len(type, 0u);
    if (need == (size_t)-1 || len < TOILE_VG_HDR_LEN + need)
        return TOILE_CMD_INVALID;
    plen = len - TOILE_VG_HDR_LEN;
    dec_hdr(buf, out);
    rc = dec_payload(buf + TOILE_VG_HDR_LEN, plen, out);
    if (rc != 0)
        return rc;
    /* Longueur fil exacte exigée (attach : 8 + nr_entries × 16). */
    need = payload_len(out->type, out->u.backing.nr_entries);
    if (out->type != TOILE_VG_CMD_RESOURCE_ATTACH_BACKING && plen != need)
        return TOILE_CMD_INVALID;
    return 0;
}
