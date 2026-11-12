/* SPDX-License-Identifier: MIT */
/*
 * Toile — tests for core/src/resource.c + resource_hash.c : standalone main(),
 * homegrown assertions, no framework, no allocation. Scanout entries are
 * filled directly in the model, so only toile_resource_has_scanout() is used.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/vgproto.h"
#include "toile/verdict.h"

/* Alignment helper exported by resource_hash.c (outside the frozen API). */
size_t toile_model_state_serialize(const toile_model *m, char *buf, size_t cap);

static int g_failures;
static const toile_page PG = { 0x1000u, 4096u };

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL line %d: %s\n", __LINE__, #cond);               \
            g_failures++;                                                \
        }                                                                \
    } while (0)
#define RECT(x, y, w, h) ((toile_rect){ (x), (y), (w), (h) })
static void put_scanout(toile_model *m, uint32_t sid, uint32_t rid,
                        toile_rect rect)
{
    m->scanouts[sid].id = sid;
    m->scanouts[sid].resource_id = rid;
    m->scanouts[sid].rect = rect;
    m->scanouts[sid].in_use = 1;
}
/* Deterministic scene used by the P3 hash checks. */
static void build_scene(toile_model *m, uint32_t height)
{
    toile_model_init(m);
    (void)toile_resource_create(m, 1, TOILE_FMT_B8G8R8A8_UNORM, 4u, height);
    (void)toile_resource_attach_backing(m, 1, &PG, 1u);
    put_scanout(m, 0u, 1u, RECT(0u, 0u, 4u, height));
    (void)toile_resource_flush(m, 1, RECT(0u, 0u, 4u, height));
}

/* Creation, refusals and backing (checks 1..10). */
static void test_creation_and_backing(toile_model *m)
{
    toile_model_init(m);
    /* 1. fresh model */
    CHECK(toile_resource_count(m) == 0 && toile_resource_find(m, 1) == NULL
          && toile_resource_has_backing(m, 1) == 0);
    /* 2. missing resource */
    CHECK(toile_resource_unref(m, 9) == TOILE_RESOURCE_MISSING
          && toile_resource_flush(m, 9, RECT(0, 0, 0, 0))
                 == TOILE_RESOURCE_MISSING);
    /* 3. create + count */
    CHECK(toile_resource_create(m, 1, TOILE_FMT_B8G8R8A8_UNORM, 4u, 2u)
              == TOILE_OK
          && toile_resource_count(m) == 1);
    /* 4. duplicate -> EXISTS, table unchanged */
    CHECK(toile_resource_create(m, 1, TOILE_FMT_B8G8R8A8_UNORM, 4u, 2u)
              == TOILE_RESOURCE_EXISTS
          && toile_resource_count(m) == 1);
    /* 5+6. bad format / bad geometry -> refused */
    CHECK(toile_resource_create(m, 2, (toile_fmt)999, 4u, 2u) == TOILE_CMD_INVALID
          && toile_resource_create(m, 2, TOILE_FMT_B8G8R8A8_UNORM, 0u, 2u)
                 == TOILE_CMD_INVALID);
    /* 7. attach 1 page */
    CHECK(toile_resource_attach_backing(m, 1, &PG, 1u) == TOILE_OK
          && toile_resource_has_backing(m, 1) == 1);
    /* 8. attach 0 pages refused, backing untouched (P2) */
    CHECK(toile_resource_attach_backing(m, 1, &PG, 0u) == TOILE_CMD_INVALID
          && toile_resource_has_backing(m, 1) == 1);
    /* 9. transfer_to_host: exact bytes */
    CHECK(toile_resource_transfer_to_host(m, 1, RECT(0, 0, 4, 2), 0)
              == TOILE_TRANSFER_OK
          && m->bytes_transferred == 4u * 2u * 4u);
    /* 10. transfer_from_host: OK, bytes unchanged */
    CHECK(toile_resource_transfer_from_host(m, 1, RECT(0, 0, 4, 2), 0)
              == TOILE_OK
          && m->bytes_transferred == 32u);
}

/* Display path, destruction and counters (checks 11..18). */
static void test_display_and_counters(toile_model *m)
{
    /* 11. flush without scanout: warning, nothing else moves */
    CHECK(toile_resource_flush(m, 1, RECT(0, 0, 4, 2))
              == TOILE_FLUSH_NO_SCANOUT
          && m->flushes == 0 && m->cmds_applied == 4u);
    /* 12. with a scanout: flush succeeds and counts */
    put_scanout(m, 0u, 1u, RECT(0, 0, 4, 2));
    CHECK(toile_resource_flush(m, 1, RECT(0, 0, 4, 2)) == TOILE_OK
          && m->flushes == 1 && m->resources[0].flush_count == 1);
    /* 13. unref refused while scanned out (resource stays) */
    CHECK(toile_resource_unref(m, 1) == TOILE_SCANOUT_INVALID
          && toile_resource_find(m, 1) != NULL);
    /* 14. scanout deactivated -> unref succeeds */
    m->scanouts[0].in_use = 0;
    CHECK(toile_resource_unref(m, 1) == TOILE_OK
          && toile_resource_count(m) == 0);
    /* 15. transfer without backing */
    CHECK(toile_resource_create(m, 3, TOILE_FMT_R8G8B8A8_UNORM, 2u, 2u)
              == TOILE_OK
          && toile_resource_transfer_to_host(m, 3, RECT(0, 0, 2, 2), 0)
                 == TOILE_BACKING_MISSING);
    /* 16. detach clears the backing */
    CHECK(toile_resource_attach_backing(m, 3, &PG, 1u) == TOILE_OK
          && toile_resource_detach_backing(m, 3) == TOILE_OK
          && toile_resource_has_backing(m, 3) == 0);
    /* 17. exact counters (P2: refusals only touch cmds_rejected) */
    CHECK(m->cmds_applied == 9u && m->cmds_rejected == 9u
          && m->flushes == 1u && m->bytes_transferred == 32u);
    /* 18. canonical serialization prefix and length */
    {
        char buf[512];
        size_t n = toile_model_state_serialize(m, buf, sizeof buf);

        CHECK(n > 15 && strncmp(buf, "toile-state-v1\n", 15) == 0
              && n == strlen(buf) && n + 1 <= sizeof buf);
    }
}

/* Determinism of the state hash (P3, checks 19..20). */
static void test_hash_determinism(void)
{
    toile_model p, q, r;

    build_scene(&p, 2u);
    build_scene(&q, 2u);
    build_scene(&r, 3u);
    CHECK(toile_model_state_hash(&p) == toile_model_state_hash(&q));
    CHECK(toile_model_state_hash(&p) != toile_model_state_hash(&r));
}

int main(void)
{
    toile_model m;
    test_creation_and_backing(&m);
    test_display_and_counters(&m);
    test_hash_determinism();
    if (g_failures == 0)
        printf("test_resource: ALL OK\n");
    else
        printf("test_resource: %d FAILURE(S)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
