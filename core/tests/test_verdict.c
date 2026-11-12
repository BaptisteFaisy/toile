/* SPDX-License-Identifier: MIT */
/*
 * Toile — tests unitaires autonomes :
 *   - verdicts : nom, sévérité, is_error, hors bornes, toile_verdict_set
 *   - protocole fil : aller-retour encode/decode pour chaque type §3
 *   - rejets : type inconnu, tampon trop court, nr_entries > TOILE_MAX_PAGES
 * Sortie 0 si tout passe, 1 sinon (nom du test en échec affiché).
 */
#include <stdio.h>
#include <string.h>

#include "toile/verdict.h"
#include "toile/vgproto.h"

static int g_failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL [%s] %s:%d  %s\n", __func__, __FILE__, __LINE__,      \
                   #cond);                                                     \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)                                                         \
    do {                                                                       \
        long long va = (long long)(a);                                         \
        long long vb = (long long)(b);                                         \
        if (va != vb) {                                                        \
            printf("FAIL [%s] %s:%d  %s == %s (%lld != %lld)\n", __func__,     \
                   __FILE__, __LINE__, #a, #b, va, vb);                        \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_STR(a, b)                                                        \
    do {                                                                       \
        const char *sa = (a);                                                  \
        const char *sb = (b);                                                  \
        if (!sa || !sb || strcmp(sa, sb) != 0) {                               \
            printf("FAIL [%s] %s:%d  %s == %s (\"%s\" != \"%s\")\n",          \
                   __func__, __FILE__, __LINE__, #a, #b,                       \
                   sa ? sa : "(null)", sb ? sb : "(null)");                    \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

/* --- verdicts -------------------------------------------------------------- */

static void test_verdict_names(void)
{
    static const struct {
        toile_verdict_code code;
        const char *name;
        const char *sev;
    } expect[] = {
        { TOILE_OK,                   "TOILE_OK",                  "info" },
        { TOILE_CMD_INVALID,          "TOILE_CMD_INVALID",         "error" },
        { TOILE_RESOURCE_EXISTS,      "TOILE_RESOURCE_EXISTS",     "warning" },
        { TOILE_RESOURCE_MISSING,     "TOILE_RESOURCE_MISSING",    "error" },
        { TOILE_BACKING_MISSING,      "TOILE_BACKING_MISSING",     "error" },
        { TOILE_SCANOUT_INVALID,      "TOILE_SCANOUT_INVALID",     "error" },
        { TOILE_FLUSH_NO_SCANOUT,     "TOILE_FLUSH_NO_SCANOUT",    "warning" },
        { TOILE_TRANSFER_OK,          "TOILE_TRANSFER_OK",         "info" },
        { TOILE_MODEL_CONSISTENT,     "TOILE_MODEL_CONSISTENT",    "info" },
        { TOILE_MODEL_COUNTEREXAMPLE, "TOILE_MODEL_COUNTEREXAMPLE", "error" },
    };

    for (size_t i = 0; i < sizeof expect / sizeof expect[0]; i++) {
        CHECK_STR(toile_verdict_name(expect[i].code), expect[i].name);
        CHECK_STR(toile_verdict_severity(expect[i].code), expect[i].sev);
        CHECK_EQ(toile_verdict_is_error(expect[i].code),
                 strcmp(expect[i].sev, "error") == 0 ? 1 : 0);
    }
}

static void test_verdict_out_of_range(void)
{
    CHECK_STR(toile_verdict_name((toile_verdict_code)TOILE_VERDICT_COUNT),
              "TOILE_UNKNOWN");
    CHECK_STR(toile_verdict_name((toile_verdict_code)999u), "TOILE_UNKNOWN");
    CHECK_STR(toile_verdict_name((toile_verdict_code)0xFFFFFFFFu),
              "TOILE_UNKNOWN");
    CHECK_STR(toile_verdict_severity((toile_verdict_code)TOILE_VERDICT_COUNT),
              "error");
    CHECK_STR(toile_verdict_severity((toile_verdict_code)999u), "error");
    CHECK_EQ(toile_verdict_is_error((toile_verdict_code)999u), 1);
}

static void test_verdict_set(void)
{
    toile_verdict v;
    toile_rect r = { 3u, 4u, 5u, 6u };
    char longcause[200];
    size_t i;

    for (i = 0; i + 1 < sizeof longcause; i++)
        longcause[i] = 'c';
    longcause[sizeof longcause - 1] = '\0';

    memset(&v, 0xAA, sizeof v);
    toile_verdict_set(&v, TOILE_TRANSFER_OK, 7u,
                      TOILE_VG_CMD_TRANSFER_TO_HOST_2D, 2u, TOILE_ID_NONE, r,
                      NULL);
    CHECK_EQ(v.code, TOILE_TRANSFER_OK);
    CHECK_EQ(v.cmd_index, 7u);
    CHECK_EQ(v.cmd_type, TOILE_VG_CMD_TRANSFER_TO_HOST_2D);
    CHECK_EQ(v.resource_id, 2u);
    CHECK_EQ(v.scanout_id, TOILE_ID_NONE);
    CHECK_EQ(v.rect.x, 3u);
    CHECK_EQ(v.rect.y, 4u);
    CHECK_EQ(v.rect.w, 5u);
    CHECK_EQ(v.rect.h, 6u);
    CHECK_STR(v.cause, "");

    toile_verdict_set(&v, TOILE_CMD_INVALID, 0u, TOILE_VG_CMD_GET_DISPLAY_INFO,
                      TOILE_ID_NONE, TOILE_ID_NONE, r, "unknown_command_type");
    CHECK_EQ(v.code, TOILE_CMD_INVALID);
    CHECK_STR(v.cause, "unknown_command_type");
    CHECK_EQ(v.cmd_index, 0u);

    /* cause trop longue : troncature propre, terminaison NUL garantie */
    toile_verdict_set(&v, TOILE_MODEL_COUNTEREXAMPLE, 1u,
                      TOILE_VG_CMD_RESOURCE_UNREF, TOILE_ID_NONE,
                      TOILE_ID_NONE, r, longcause);
    CHECK_EQ(strlen(v.cause), TOILE_CAUSE_MAX - 1u);
    CHECK_EQ(v.cause[TOILE_CAUSE_MAX - 1u], '\0');
    for (i = 0; i < TOILE_CAUSE_MAX - 1u; i++)
        CHECK_EQ((unsigned char)v.cause[i], (unsigned char)'c');
}

/* --- formats et noms de commandes ------------------------------------------ */

static void test_formats(void)
{
    static const toile_fmt fmts[] = {
        TOILE_FMT_B8G8R8A8_UNORM, TOILE_FMT_B8G8R8X8_UNORM,
        TOILE_FMT_A8R8G8B8_UNORM, TOILE_FMT_X8R8G8B8_UNORM,
        TOILE_FMT_R8G8B8A8_UNORM, TOILE_FMT_X8B8G8R8_UNORM,
        TOILE_FMT_A8B8G8R8_UNORM, TOILE_FMT_R8G8B8X8_UNORM,
    };
    toile_fmt f = TOILE_FMT_INVALID;

    for (size_t i = 0; i < sizeof fmts / sizeof fmts[0]; i++) {
        CHECK_EQ(toile_fmt_bpp(fmts[i]), 4u); /* §3 : tous à 4 octets/pixel */
        CHECK(toile_fmt_name(fmts[i]) != NULL);
    }
    CHECK_EQ(toile_fmt_bpp(TOILE_FMT_INVALID), 0u);
    CHECK(toile_fmt_name(TOILE_FMT_INVALID) == NULL);
    CHECK(toile_fmt_name((toile_fmt)999) == NULL);

    CHECK_EQ(toile_fmt_parse("B8G8R8A8_UNORM", &f), 0);
    CHECK_EQ(f, TOILE_FMT_B8G8R8A8_UNORM);
    CHECK_EQ(toile_fmt_parse("R8G8B8X8_UNORM", &f), 0);
    CHECK_EQ(f, TOILE_FMT_R8G8B8X8_UNORM);
    CHECK_EQ(toile_fmt_parse("1", &f), 0);
    CHECK_EQ(f, TOILE_FMT_B8G8R8A8_UNORM);
    CHECK_EQ(toile_fmt_parse("0x43", &f), 0); /* 67 */
    CHECK_EQ(f, TOILE_FMT_R8G8B8A8_UNORM);
    CHECK_EQ(toile_fmt_parse("0X79", &f), 0); /* 121 */
    CHECK_EQ(f, TOILE_FMT_A8B8G8R8_UNORM);
    CHECK_EQ(toile_fmt_parse("134", &f), 0);
    CHECK_EQ(f, TOILE_FMT_R8G8B8X8_UNORM);

    CHECK_EQ(toile_fmt_parse("nope", &f), TOILE_CMD_INVALID);
    CHECK_EQ(toile_fmt_parse("0", &f), TOILE_CMD_INVALID);
    CHECK_EQ(toile_fmt_parse("", &f), TOILE_CMD_INVALID);
    CHECK_EQ(toile_fmt_parse("999", &f), TOILE_CMD_INVALID);
    CHECK_EQ(toile_fmt_parse("0x0", &f), TOILE_CMD_INVALID);
    CHECK_EQ(toile_fmt_parse(NULL, &f), TOILE_CMD_INVALID);
}

static void test_cmd_names(void)
{
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_GET_DISPLAY_INFO), "get_display_info");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_RESOURCE_CREATE_2D), "resource_create_2d");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_RESOURCE_UNREF), "resource_unref");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_SET_SCANOUT), "set_scanout");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_RESOURCE_FLUSH), "resource_flush");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_TRANSFER_TO_HOST_2D), "transfer_to_host_2d");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_RESOURCE_ATTACH_BACKING), "resource_attach_backing");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_RESOURCE_DETACH_BACKING), "resource_detach_backing");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_TRANSFER_FROM_HOST_3D), "transfer_from_host");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_UPDATE_CURSOR), "update_cursor");
    CHECK_STR(toile_vg_cmd_name(TOILE_VG_CMD_MOVE_CURSOR), "move_cursor");
    CHECK(toile_vg_cmd_name(0x0302u) == NULL); /* SET_CURSOR inexistant (§9) */
    CHECK(toile_vg_cmd_name(0x0999u) == NULL);
}

/* --- aller-retour encode/decode --------------------------------------------- */

static toile_cmd mk_cmd(uint32_t type)
{
    toile_cmd c;

    memset(&c, 0, sizeof c);
    c.type = type;
    c.flags = TOILE_VG_FLAG_FENCE;
    c.fence_id = 0x0102030405060708ull;
    return c;
}

static void fill_backing(toile_cmd *c, uint32_t n)
{
    c->u.backing.resource_id = 5u;
    c->u.backing.nr_entries = n;
    for (uint32_t i = 0; i < n && i < TOILE_MAX_PAGES; i++) {
        c->u.backing.pages[i].addr = 0x10000000ull + (uint64_t)i * 4096ull;
        c->u.backing.pages[i].length = 4096u;
    }
}

static int cmp_rect(toile_rect a, toile_rect b)
{
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

static void cmp_backing(const toile_cmd *a, const toile_cmd *b)
{
    CHECK_EQ(a->u.backing.resource_id, b->u.backing.resource_id);
    CHECK_EQ(a->u.backing.nr_entries, b->u.backing.nr_entries);
    for (uint32_t i = 0; i < a->u.backing.nr_entries && i < TOILE_MAX_PAGES; i++) {
        CHECK_EQ(a->u.backing.pages[i].addr, b->u.backing.pages[i].addr);
        CHECK_EQ(a->u.backing.pages[i].length, b->u.backing.pages[i].length);
    }
}

static void cmp_cursor(const toile_cmd *a, const toile_cmd *b, int full)
{
    CHECK_EQ(a->u.cursor.scanout_id, b->u.cursor.scanout_id);
    CHECK_EQ(a->u.cursor.x, b->u.cursor.x);
    CHECK_EQ(a->u.cursor.y, b->u.cursor.y);
    if (full) {
        CHECK_EQ(a->u.cursor.resource_id, b->u.cursor.resource_id);
        CHECK_EQ(a->u.cursor.hot_x, b->u.cursor.hot_x);
        CHECK_EQ(a->u.cursor.hot_y, b->u.cursor.hot_y);
    }
}

static void cmp_cmd(const toile_cmd *a, const toile_cmd *b)
{
    CHECK_EQ(a->type, b->type);
    CHECK_EQ(a->flags, b->flags);
    CHECK_EQ(a->fence_id, b->fence_id);
    switch (a->type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        CHECK_EQ(a->u.create_2d.resource_id, b->u.create_2d.resource_id);
        CHECK_EQ(a->u.create_2d.format, b->u.create_2d.format);
        CHECK_EQ(a->u.create_2d.width, b->u.create_2d.width);
        CHECK_EQ(a->u.create_2d.height, b->u.create_2d.height);
        break;
    case TOILE_VG_CMD_RESOURCE_UNREF:
        CHECK_EQ(a->u.unref.resource_id, b->u.unref.resource_id);
        break;
    case TOILE_VG_CMD_SET_SCANOUT:
        CHECK_EQ(a->u.scanout.resource_id, b->u.scanout.resource_id);
        CHECK_EQ(a->u.scanout.scanout_id, b->u.scanout.scanout_id);
        CHECK(cmp_rect(a->u.scanout.rect, b->u.scanout.rect));
        break;
    case TOILE_VG_CMD_RESOURCE_FLUSH:
        CHECK_EQ(a->u.flush.resource_id, b->u.flush.resource_id);
        CHECK(cmp_rect(a->u.flush.rect, b->u.flush.rect));
        break;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:
        CHECK_EQ(a->u.xfer.resource_id, b->u.xfer.resource_id);
        CHECK_EQ(a->u.xfer.offset, b->u.xfer.offset);
        CHECK(cmp_rect(a->u.xfer.rect, b->u.xfer.rect));
        break;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        cmp_backing(a, b);
        break;
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
        CHECK_EQ(a->u.backing.resource_id, b->u.backing.resource_id);
        break;
    case TOILE_VG_CMD_UPDATE_CURSOR:
        cmp_cursor(a, b, 1);
        break;
    case TOILE_VG_CMD_MOVE_CURSOR:
        cmp_cursor(a, b, 0);
        break;
    default:
        break;
    }
}

static void round_trip(const toile_cmd *c, size_t expect_len)
{
    uint8_t buf[TOILE_VG_MAX_CMD_LEN];
    size_t written = 0u;
    toile_cmd d;
    int rc;

    memset(&d, 0, sizeof d);
    rc = toile_vg_encode(c, buf, sizeof buf, &written);
    CHECK_EQ(rc, 0);
    CHECK_EQ(written, expect_len);
    rc = toile_vg_decode(buf, written, &d);
    CHECK_EQ(rc, 0);
    cmp_cmd(c, &d);
}

static void test_wire_commands(void)
{
    toile_cmd c;

    c = mk_cmd(TOILE_VG_CMD_GET_DISPLAY_INFO);
    round_trip(&c, 24u); /* en-tête seul */

    c = mk_cmd(TOILE_VG_CMD_RESOURCE_CREATE_2D);
    c.u.create_2d.resource_id = 3u;
    c.u.create_2d.format = TOILE_FMT_B8G8R8A8_UNORM;
    c.u.create_2d.width = 640u;
    c.u.create_2d.height = 480u;
    round_trip(&c, 40u);

    c = mk_cmd(TOILE_VG_CMD_RESOURCE_UNREF);
    c.u.unref.resource_id = 3u;
    round_trip(&c, 32u);

    c = mk_cmd(TOILE_VG_CMD_SET_SCANOUT);
    c.u.scanout.resource_id = 3u;
    c.u.scanout.scanout_id = 1u;
    c.u.scanout.rect = (toile_rect){ 0u, 0u, 640u, 480u };
    round_trip(&c, 48u);

    c = mk_cmd(TOILE_VG_CMD_RESOURCE_FLUSH);
    c.u.flush.resource_id = 3u;
    c.u.flush.rect = (toile_rect){ 10u, 20u, 30u, 40u };
    round_trip(&c, 48u);

    c = mk_cmd(TOILE_VG_CMD_TRANSFER_TO_HOST_2D);
    c.u.xfer.resource_id = 3u;
    c.u.xfer.rect = (toile_rect){ 0u, 0u, 640u, 480u };
    c.u.xfer.offset = 0xDEADBEEFCAFEBABEull;
    round_trip(&c, 56u);

    c = mk_cmd(TOILE_VG_CMD_TRANSFER_FROM_HOST_3D);
    c.u.xfer.resource_id = 4u;
    c.u.xfer.rect = (toile_rect){ 1u, 2u, 3u, 4u };
    c.u.xfer.offset = 4096u;
    round_trip(&c, 56u);

}

static void test_wire_backing_cursor(void)
{
    toile_cmd c;

    /* attach_backing : 0, 1 et 32 pages (longueur fil exacte) */
    c = mk_cmd(TOILE_VG_CMD_RESOURCE_ATTACH_BACKING);
    fill_backing(&c, 0u);
    round_trip(&c, 32u);
    fill_backing(&c, 1u);
    round_trip(&c, 48u);
    fill_backing(&c, TOILE_MAX_PAGES);
    round_trip(&c, 24u + 8u + TOILE_MAX_PAGES * 16u); /* = TOILE_VG_MAX_CMD_LEN */
    CHECK_EQ(24u + 8u + TOILE_MAX_PAGES * 16u, TOILE_VG_MAX_CMD_LEN);

    c = mk_cmd(TOILE_VG_CMD_RESOURCE_DETACH_BACKING);
    c.u.backing.resource_id = 5u;
    round_trip(&c, 32u);

    c = mk_cmd(TOILE_VG_CMD_UPDATE_CURSOR);
    c.u.cursor.scanout_id = 0u;
    c.u.cursor.x = 100u;
    c.u.cursor.y = 200u;
    c.u.cursor.resource_id = 7u;
    c.u.cursor.hot_x = 3u;
    c.u.cursor.hot_y = 4u;
    round_trip(&c, 56u);

    c = mk_cmd(TOILE_VG_CMD_MOVE_CURSOR);
    c.u.cursor.scanout_id = 0u;
    c.u.cursor.x = 321u;
    c.u.cursor.y = 654u;
    round_trip(&c, 40u);
}

/* Rejets de l'encodeur : type inconnu, tampon court, backing trop long. */
static void test_rejects_encode(void)
{
    uint8_t buf[TOILE_VG_MAX_CMD_LEN];
    uint8_t big[600];
    size_t written = 0u;
    toile_cmd c;

    /* encode : type inconnu (dont 0x0302, inexistant au standard §9) */
    c = mk_cmd(0x0999u);
    CHECK_EQ(toile_vg_encode(&c, buf, sizeof buf, &written), TOILE_CMD_INVALID);
    c = mk_cmd(0x0302u);
    CHECK_EQ(toile_vg_encode(&c, buf, sizeof buf, &written), TOILE_CMD_INVALID);

    /* encode : tampon trop petit */
    c = mk_cmd(TOILE_VG_CMD_RESOURCE_CREATE_2D);
    CHECK_EQ(toile_vg_encode(&c, buf, 8u, &written), TOILE_CMD_INVALID);
    CHECK_EQ(toile_vg_encode(&c, buf, TOILE_VG_HDR_LEN + 15u, &written),
             TOILE_CMD_INVALID);

    /* encode : nr_entries > TOILE_MAX_PAGES */
    c = mk_cmd(TOILE_VG_CMD_RESOURCE_ATTACH_BACKING);
    fill_backing(&c, 1u);
    c.u.backing.nr_entries = TOILE_MAX_PAGES + 1u;
    CHECK_EQ(toile_vg_encode(&c, big, sizeof big, &written), TOILE_CMD_INVALID);

}

/* Rejets du décodeur : type inconnu, tampon tronqué, nr_entries hors borne. */
static void test_rejects_decode(void)
{
    uint8_t buf[TOILE_VG_MAX_CMD_LEN];
    uint8_t big[600];
    size_t written = 0u;
    toile_cmd c;
    toile_cmd d;

    c = mk_cmd(TOILE_VG_CMD_GET_DISPLAY_INFO);
    CHECK_EQ(toile_vg_encode(&c, buf, sizeof buf, &written), 0);
    buf[0] = 0x02u;
    buf[1] = 0x03u; /* 0x0302 petit-boutien */
    CHECK_EQ(toile_vg_decode(buf, written, &d), TOILE_CMD_INVALID);

    /* decode : tampon trop court (en-tête tronqué, puis charge tronquée) */
    CHECK_EQ(toile_vg_decode(buf, TOILE_VG_HDR_LEN - 1u, &d), TOILE_CMD_INVALID);
    CHECK_EQ(toile_vg_decode(buf, 0u, &d), TOILE_CMD_INVALID);
    c = mk_cmd(TOILE_VG_CMD_TRANSFER_TO_HOST_2D);
    c.u.xfer.resource_id = 1u;
    c.u.xfer.rect = (toile_rect){ 0u, 0u, 10u, 20u };
    CHECK_EQ(toile_vg_encode(&c, buf, sizeof buf, &written), 0);
    CHECK_EQ(toile_vg_decode(buf, written - 1u, &d), TOILE_CMD_INVALID);

    /* decode : nr_entries = 33 sur le fil (> TOILE_MAX_PAGES) */
    memset(big, 0, sizeof big);
    big[0] = 0x06u;
    big[1] = 0x01u;  /* 0x0106 = resource_attach_backing */
    big[24] = 1u;    /* resource_id */
    big[28] = 33u;   /* nr_entries */
    CHECK_EQ(toile_vg_decode(big, 24u + 8u + 33u * 16u, &d), TOILE_CMD_INVALID);
}

int main(void)
{
    test_verdict_names();
    test_verdict_out_of_range();
    test_verdict_set();
    test_formats();
    test_cmd_names();
    test_wire_commands();
    test_wire_backing_cursor();
    test_rejects_encode();
    test_rejects_decode();

    if (g_failures != 0) {
        printf("ECHEC : %d assertion(s) en echec\n", g_failures);
        return 1;
    }
    printf("OK : verdicts + protocole fil (tous les types §3)\n");
    return 0;
}
