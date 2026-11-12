/* SPDX-License-Identifier: MIT */
/* Toile — tests for cmdproc.c + cmdproc_parse.c: standalone main(), no framework. */
#include <stdio.h>
#include <string.h>

#include "toile/cmdproc.h"
#include "toile/resource.h"
#include "toile/scanout.h"

static int g_failures;

#define CHECK(c) do { if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); g_failures++; } } while (0)
#define CHECK_EQ(a, b) do { long long x = (long long)(a), y = (long long)(b); if (x != y) { printf("FAIL %d: %s != %s\n", __LINE__, #a, #b); g_failures++; } } while (0)
#define VCHECK(i, c, t) do { CHECK_EQ(v[i].code, c); CHECK_EQ(v[i].cmd_type, t); } while (0)

/* Boot scene: create, attach_backing, set_scanout, transfer_to_host, flush. */
static size_t build_boot(toile_cmd *cmds, uint32_t res, uint32_t w, uint32_t h)
{
    memset(cmds, 0, 5u * sizeof *cmds);
    cmds[0].type = TOILE_VG_CMD_RESOURCE_CREATE_2D;
    cmds[0].u.create_2d.resource_id = res; cmds[0].u.create_2d.format = TOILE_FMT_B8G8R8A8_UNORM;
    cmds[0].u.create_2d.width = w; cmds[0].u.create_2d.height = h;
    cmds[1].type = TOILE_VG_CMD_RESOURCE_ATTACH_BACKING;
    cmds[1].u.backing.resource_id = res; cmds[1].u.backing.nr_entries = 1u;
    cmds[1].u.backing.pages[0].addr = 0x1000u; cmds[1].u.backing.pages[0].length = 4096u;
    cmds[2].type = TOILE_VG_CMD_SET_SCANOUT;
    cmds[2].u.scanout.resource_id = res; cmds[2].u.scanout.rect = (toile_rect){ 0u, 0u, w, h };
    cmds[3].type = TOILE_VG_CMD_TRANSFER_TO_HOST_2D; cmds[3].u.xfer.resource_id = res;
    cmds[4].type = TOILE_VG_CMD_RESOURCE_FLUSH; cmds[4].u.flush.resource_id = res;
    return 5u;
}

/* Boot sequence: exact number, order and content of the verdicts. */
static void test_boot_sequence(void)
{
    toile_cmd cmds[5];
    toile_verdict v[8];
    toile_model m;
    size_t n = 0;

    build_boot(cmds, 1u, 1280u, 800u);
    toile_model_init(&m);
    CHECK_EQ(toile_cmdproc_run(cmds, 5u, &m, v, 8u, &n), TOILE_OK);
    CHECK_EQ(n, 6u);
    VCHECK(0, TOILE_OK, TOILE_VG_CMD_RESOURCE_CREATE_2D);
    VCHECK(1, TOILE_OK, TOILE_VG_CMD_RESOURCE_ATTACH_BACKING);
    VCHECK(2, TOILE_OK, TOILE_VG_CMD_SET_SCANOUT);
    VCHECK(3, TOILE_TRANSFER_OK, TOILE_VG_CMD_TRANSFER_TO_HOST_2D);
    VCHECK(4, TOILE_OK, TOILE_VG_CMD_RESOURCE_FLUSH);
    CHECK_EQ(v[5].code, TOILE_MODEL_CONSISTENT); CHECK_EQ(v[5].cmd_index, TOILE_ID_NONE);
    CHECK_EQ(v[0].resource_id, 1u); CHECK_EQ(v[2].scanout_id, 0u); CHECK_EQ(v[3].resource_id, 1u);
    CHECK_EQ(v[3].rect.w, 1280u); CHECK_EQ(v[3].rect.h, 800u); CHECK_EQ(m.cmds_applied, 5u);
    CHECK_EQ(m.cmds_rejected, 0u); CHECK_EQ(m.flushes, 1u);
    CHECK_EQ(m.bytes_transferred, 1280u * 800u * 4u);
}

/* Three P2 rejections and the fail-fast counter-example. */
static void test_rejections(void)
{
    static const struct { uint32_t type, res; const char *cause; int code; } rej[] = {
        { 0x0999u, 0u, "unknown_command_type", TOILE_CMD_INVALID },
        { TOILE_VG_CMD_RESOURCE_FLUSH, 42u, "", TOILE_RESOURCE_MISSING },
        { TOILE_VG_CMD_MOVE_CURSOR, 0u, "cursor_not_visible", TOILE_CMD_INVALID },
    };
    toile_cmd cmds[2];
    toile_verdict v[8];
    toile_model m, before;
    size_t n = 0;

    for (size_t i = 0; i < sizeof rej / sizeof rej[0]; i++) {
        memset(cmds, 0, sizeof cmds); cmds[0].type = rej[i].type;
        cmds[0].u.flush.resource_id = rej[i].res; /* first word of the union */
        toile_model_init(&m); before = m;
        CHECK_EQ(toile_cmdproc_run(cmds, 1u, &m, v, 8u, &n), rej[i].code);
        CHECK_EQ(v[0].code, rej[i].code);
        if (rej[i].cause[0])
            CHECK_EQ(strcmp(v[0].cause, rej[i].cause), 0);
        {   /* the model must not change, except the rejection counter */
            toile_model snap = before, now = m; /* unchanged but the counter */
            snap.cmds_rejected = 0; now.cmds_rejected = 0;
            CHECK_EQ(memcmp(&snap, &now, sizeof snap), 0);
        }
    }
    memset(cmds, 0, sizeof cmds);
    cmds[0].type = TOILE_VG_CMD_GET_DISPLAY_INFO; cmds[1].type = TOILE_VG_CMD_GET_DISPLAY_INFO;
    toile_model_init(&m);
    m.scanouts[0].id = 0u; m.scanouts[0].resource_id = 99u; m.scanouts[0].in_use = 1u;
    CHECK_EQ(toile_cmdproc_run(cmds, 2u, &m, v, 8u, &n), TOILE_MODEL_COUNTEREXAMPLE);
    CHECK_EQ(n, 2u); CHECK_EQ(v[0].code, TOILE_OK);
    CHECK_EQ(v[1].code, TOILE_MODEL_COUNTEREXAMPLE); CHECK_EQ(strcmp(v[1].cause, "invariant_i1"), 0);
    CHECK_EQ(v[1].cmd_index, 0u);
}

/* Output capacity, invalid arguments and determinism (P3). */
static void test_limits(void)
{
    toile_cmd cmds[5];
    toile_verdict v[8], v2[8];
    toile_model m, m2;
    size_t n = 0, n2 = 0;

    build_boot(cmds, 1u, 64u, 64u); toile_model_init(&m);
    CHECK_EQ(toile_cmdproc_run(cmds, 5u, &m, v, 3u, &n), TOILE_OK);
    CHECK_EQ(n, 3u); CHECK_EQ(v[0].code, TOILE_OK); CHECK_EQ(v[2].code, TOILE_OK);
    n = 7u; CHECK_EQ(toile_cmdproc_run(NULL, 5u, &m, v, 8u, &n), TOILE_CMD_INVALID); CHECK_EQ(n, 0u);
    CHECK_EQ(toile_cmdproc_run(cmds, 0u, &m, v, 8u, &n), TOILE_CMD_INVALID); CHECK_EQ(n, 0u);
    toile_model_init(&m); toile_model_init(&m2);
    CHECK_EQ(toile_cmdproc_run(cmds, 5u, &m, v, 8u, &n), TOILE_OK); CHECK_EQ(toile_cmdproc_run(cmds, 5u, &m2, v2, 8u, &n2), TOILE_OK);
    CHECK_EQ(n, n2); CHECK_EQ(memcmp(v, v2, sizeof v), 0); CHECK_EQ(toile_model_state_hash(&m), toile_model_state_hash(&m2));
}

/* Parser: one command name per type, aliases, rect, pages, unknown key. */
static void test_parser(void)
{
    static const struct { const char *line; uint32_t type; } cs[] = { /* §3 types */
        { "get_display_info", 0x0100u }, { "resource_unref id=1", 0x0102u },
        { "resource_flush id=1", 0x0104u }, { "transfer_to_host_2d id=1", 0x0105u },
        { "resource_detach_backing id=1", 0x0107u }, { "transfer_from_host id=1", 0x0206u },
        { "update_cursor scanout=0 x=1 y=2", 0x0300u }, { "move_cursor scanout=0 x=1 y=2", 0x0301u },
        { "resource_create_2d id=1 format=B8G8R8A8_UNORM width=8 height=4", 0x0101u },
        { "set_scanout scanout=0 id=1 x=0 y=0 width=8 height=4", 0x0103u },
        { "resource_attach_backing id=1 pages=0x1000:4096", 0x0106u },
    };
    toile_cmd c; char err[64];

    for (size_t i = 0; i < sizeof cs / sizeof cs[0]; i++) {
        CHECK_EQ(toile_cmd_parse_line(cs[i].line, 1u, &c, err, sizeof err), 0);
        CHECK_EQ(c.type, cs[i].type);
    }
    CHECK_EQ(toile_cmd_parse_line("resource_create_2d resource=7 format=B8G8R8A8_UNORM w=64 h=32", 2u, &c, err, sizeof err), 0);
    CHECK_EQ(c.u.create_2d.resource_id, 7u); CHECK_EQ(c.u.create_2d.width, 64u); CHECK_EQ(c.u.create_2d.height, 32u);
    CHECK_EQ(toile_cmd_parse_line("resource_flush id=1", 3u, &c, err, sizeof err), 0);
    CHECK_EQ(c.u.flush.rect.w, 0u); CHECK_EQ(c.u.flush.rect.h, 0u);
    CHECK_EQ(toile_cmd_parse_line("resource_flush id=1 x=1 y=2 width=3 height=4", 4u, &c, err, sizeof err), 0);
    CHECK_EQ(c.u.flush.rect.x, 1u); CHECK_EQ(c.u.flush.rect.w, 3u); CHECK_EQ(c.u.flush.rect.h, 4u);
    CHECK_EQ(toile_cmd_parse_line("resource_attach_backing id=1 pages=0x1000:4096,0x2000:8192", 5u, &c, err, sizeof err), 0);
    CHECK_EQ(c.u.backing.nr_entries, 2u); CHECK_EQ(c.u.backing.pages[1].addr, 0x2000u); CHECK_EQ(c.u.backing.pages[1].length, 8192u);
    CHECK_EQ(toile_cmd_parse_line("resource_flush id=1 bogus=2", 6u, &c, err, sizeof err), TOILE_CMD_INVALID);
    CHECK_EQ(strcmp(err, "6:unknown_key"), 0);
}

int main(void)
{
    test_boot_sequence();
    test_rejections();
    test_limits();
    test_parser();
    if (g_failures) { printf("test_cmdproc: %d echec(s)\n", g_failures); return 1; }
    printf("test_cmdproc: OK\n"); return 0;
}
