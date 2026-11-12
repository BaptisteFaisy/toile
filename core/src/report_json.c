/* SPDX-License-Identifier: MIT */
/*
 * Toile — rapport JSON (CONTRACT.md §7.2).
 *
 * Une seule ligne plus '\n', aucune espace hors des chaînes, ordre des clés
 * figé, clés non applicables absentes (jamais null). Identique octet pour
 * octet à host/toile/report.py : le test différentiel les compare.
 */
#include <inttypes.h>

#include "report_priv.h"
#include "toile/verdict.h"

static void tr_json_state(tr_writer *w, const tr_state *st)
{
    tr_printf(w,
              "{\"resources\":%zu,\"scanouts\":%d,\"hash\":\"%016" PRIx64
              "\",\"applied\":%" PRIu64 ",\"rejected\":%" PRIu64
              ",\"flushes\":%" PRIu64 ",\"bytes\":%" PRIu64
              ",\"cursor\":{\"resource\":%" PRIu32 ",\"scanout\":%" PRIu32
              ",\"x\":%" PRIu32 ",\"y\":%" PRIu32 ",\"hot_x\":%" PRIu32
              ",\"hot_y\":%" PRIu32 ",\"visible\":%d}}",
              st->resources, st->scanouts, st->hash, st->applied, st->rejected,
              st->flushes, st->bytes, st->cur_resource, st->cur_scanout,
              st->cur_x, st->cur_y, st->cur_hot_x, st->cur_hot_y,
              st->cur_visible ? 1 : 0);
}

static void tr_json_verdict(tr_writer *w, size_t index, const toile_verdict *v)
{
    tr_printf(w, "{\"i\":%zu,\"code\":\"%s\",\"sev\":\"%s\"", index,
              toile_verdict_name(v->code), toile_verdict_severity(v->code));
    if (v->cmd_type != TOILE_ID_NONE)
        tr_printf(w, ",\"cmd\":\"0x%04" PRIx32 "\"", v->cmd_type);
    if (v->resource_id != TOILE_ID_NONE)
        tr_printf(w, ",\"resource\":%" PRIu32, v->resource_id);
    if (v->scanout_id != TOILE_ID_NONE)
        tr_printf(w, ",\"scanout\":%" PRIu32, v->scanout_id);
    if (v->code == TOILE_TRANSFER_OK)
        tr_printf(w, ",\"rect\":\"%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32
                  "\"", v->rect.x, v->rect.y, v->rect.w, v->rect.h);
    if (v->cause[0] != '\0')
        tr_printf(w, ",\"cause\":\"%s\"", v->cause);
    tr_putc(w, '}');
}

int toile_report_json(const toile_model *m, const toile_verdict *v, size_t n,
                      const char *source, char *buf, size_t cap,
                      size_t *written)
{
    tr_writer w;
    tr_state st;
    size_t i;

    (void)source; /* le JSON ne cite pas la fixture */
    if (m == NULL || buf == NULL || cap == 0 || (v == NULL && n > 0))
        return TOILE_CMD_INVALID;

    tr_init(&w, buf, cap);
    tr_state_fill(m, &st);
    tr_puts(&w, "{\"toile\":\"1\",\"state\":");
    tr_json_state(&w, &st);
    tr_puts(&w, ",\"verdicts\":[");
    for (i = 0; i < n; i++) {
        if (i > 0)
            tr_putc(&w, ',');
        tr_json_verdict(&w, i, &v[i]);
    }
    tr_puts(&w, "]}\n");
    return tr_finish(&w, written);
}
