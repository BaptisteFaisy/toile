/* SPDX-License-Identifier: MIT */
/*
 * Toile — tests des scanouts et des invariants du modèle (core/src/scanout.c).
 * C11 pur : aucun malloc, fichier, horloge ou socket ; les états fautifs sont
 * construits par manipulation directe de toile_model.
 */
#include <stdio.h>
#include <string.h>

#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/verdict.h"

static int failures;

#define CHECK(cond)                                                          \
    do {                                                                      \
        if (!(cond)) { failures++;                                            \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); }           \
    } while (0)

/* Insère une ressource valide dans le premier emplacement libre. */
static void add_resource(toile_model *m, uint32_t id, uint32_t w, uint32_t h)
{
    for (uint32_t i = 0; i < TOILE_MAX_RESOURCES; i++) {
        if (m->resources[i].in_use == 0) {
            toile_resource *r = &m->resources[i];
            r->id = id; r->fmt = TOILE_FMT_B8G8R8A8_UNORM;
            r->width = w; r->height = h; r->in_use = 1;
            return;
        }
    }
}

/* Vérifie le code et la cause figée d'un contre-exemple (terminaison NUL). */
static void check_cause(const toile_model *m, const char *want)
{
    char cause[TOILE_CAUSE_MAX];
    const char *end;
    int rc;

    memset(cause, 0xAA, sizeof cause); /* pas de terminaison préalable */
    rc = toile_model_check_invariants(m, cause, sizeof cause);
    end = memchr(cause, '\0', sizeof cause);
    if (rc != TOILE_MODEL_COUNTEREXAMPLE || end == NULL
        || strcmp(cause, want) != 0) {
        failures++;
        printf("FAIL invariant rc=%d cause=\"%s\" attendu \"%s\"\n", rc,
               end != NULL ? cause : "<non termine>", want);
    }
}

/* Modèle cohérent de départ : ressource 1 affichée sur le scanout 0. */
static void fresh_model(toile_model *m)
{
    memset(m, 0, sizeof *m);
    add_resource(m, 1, 64, 64);
    CHECK(toile_scanout_set(m, 0, 1, 0, 0, 64, 64) == TOILE_OK);
}

/* set_scanout : affectation, déplacement, désactivation, refus. */
static void test_set_scanout(void)
{
    toile_model m;
    const toile_scanout *s;

    memset(&m, 0, sizeof m);
    add_resource(&m, 1, 640, 480);
    add_resource(&m, 2, 800, 600);
    CHECK(toile_scanout_find(&m, 0) == NULL);
    CHECK(toile_scanout_find(&m, TOILE_MAX_SCANOUTS) == NULL);
    CHECK(toile_scanout_count(&m) == 0);
    CHECK(toile_scanout_set(&m, 0, 1, 0, 0, 640, 480) == TOILE_OK);
    CHECK(m.cmds_applied == 1 && m.cmds_rejected == 0);
    s = toile_scanout_find(&m, 0);
    CHECK(s != NULL && s->id == 0 && s->in_use == 1 && s->resource_id == 1);
    CHECK(s != NULL && s->rect.x == 0 && s->rect.y == 0 && s->rect.w == 640 && s->rect.h == 480);
    CHECK(toile_resource_has_scanout(&m, 1) == 1);
    CHECK(toile_resource_has_scanout(&m, 2) == 0);
    CHECK(toile_resource_has_scanout(&m, 0) == 0);
    CHECK(toile_scanout_set(&m, 0, 2, 10, 20, 800, 600) == TOILE_OK);
    s = toile_scanout_find(&m, 0);
    CHECK(s != NULL && s->resource_id == 2 && s->rect.x == 10 && s->rect.y == 20 && s->rect.w == 800);
    CHECK(m.cmds_applied == 2);
    CHECK(toile_scanout_set(&m, 0, 0, 0, 0, 0, 0) == TOILE_OK);
    CHECK(toile_scanout_find(&m, 0) == NULL && toile_scanout_count(&m) == 0 && m.cmds_applied == 3);
    CHECK(toile_scanout_set(&m, 1, 42, 0, 0, 64, 64) == TOILE_SCANOUT_INVALID);
    CHECK(toile_scanout_find(&m, 1) == NULL && m.cmds_rejected == 1);
    CHECK(toile_scanout_set(&m, TOILE_MAX_SCANOUTS, 1, 0, 0, 64, 64) == TOILE_CMD_INVALID);
    CHECK(toile_scanout_set(&m, 4096, 1, 0, 0, 64, 64) == TOILE_CMD_INVALID);
    CHECK(m.cmds_rejected == 3 && m.cmds_applied == 3);
    CHECK(toile_scanout_set(&m, 2, 1, 0, 0, 0, 64) == TOILE_CMD_INVALID);
    CHECK(toile_scanout_set(&m, 2, 1, 0, 0, 64, 0) == TOILE_CMD_INVALID);
    CHECK(toile_scanout_find(&m, 2) == NULL && m.cmds_rejected == 5);
}

/* Table pleine : jamais de débordement. */
static void test_table_full(void)
{
    toile_model m;

    memset(&m, 0, sizeof m);
    add_resource(&m, 1, 64, 64);
    for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        CHECK(toile_scanout_set(&m, i, 1, 0, 0, 64, 64) == TOILE_OK);
    }
    CHECK(toile_scanout_count(&m) == 16);
    /* 16+1 : le 17e scanout est hors bornes -> CMD_INVALID, aucun débordement. */
    CHECK(toile_scanout_set(&m, TOILE_MAX_SCANOUTS, 1, 0, 0, 64, 64) == TOILE_CMD_INVALID && m.cmds_rejected == 1);
    /* Un déplacement réutilise son emplacement : autorisé table pleine. */
    CHECK(toile_scanout_set(&m, 3, 1, 5, 6, 32, 32) == TOILE_OK);
    CHECK(toile_scanout_find(&m, 3) != NULL && toile_scanout_find(&m, 3)->rect.x == 5);
}

/* Invariants I1..I6 : un état fautif par cause. */
static void test_invariants(void)
{
    toile_model m;
    char cause[TOILE_CAUSE_MAX];

    fresh_model(&m);
    CHECK(toile_model_check_invariants(&m, cause, sizeof cause) == 0 && cause[0] == '\0');
    fresh_model(&m); m.scanouts[0].resource_id = 999; check_cause(&m, "invariant_i1");
    fresh_model(&m); m.resources[1].flush_count = 3; check_cause(&m, "invariant_i2");
    fresh_model(&m); m.resources[1].n_pages = 2; check_cause(&m, "invariant_i3");
    fresh_model(&m); m.resources[0].width = 0; check_cause(&m, "invariant_i4");
    fresh_model(&m); m.resources[0].fmt = (toile_fmt)999; check_cause(&m, "invariant_i4");
    fresh_model(&m); m.resources[0].n_pages = TOILE_MAX_PAGES + 1; check_cause(&m, "invariant_i5");
    fresh_model(&m); m.bytes_transferred = 6; check_cause(&m, "invariant_i6");
}

int main(void)
{
    test_set_scanout();
    test_table_full();
    test_invariants();
    if (failures != 0) {
        printf("test_scanout: %d echec(s)\n", failures);
        return 1;
    }
    printf("test_scanout: OK\n");
    return 0;
}
