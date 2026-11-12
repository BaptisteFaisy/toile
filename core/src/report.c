/* SPDX-License-Identifier: MIT */
/*
 * Toile — rapport texte et aiguillage des formats (CONTRACT.md §7.1).
 *
 * L'écrivain borné et l'état présenté viennent de report_priv.h : les trois
 * formats partagent exactement les mêmes règles de présence des champs.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "report_priv.h"
#include "toile/verdict.h"

/* Ajoute un fragment au payload, en gardant la séparation d'une espace. */
static void tr_payload_add(char *out, size_t cap, size_t *used, int first,
                           const char *text)
{
    size_t len;

    if (text == NULL || text[0] == '\0')
        return;
    len = strlen(text);
    if (*used + len + (first ? 0u : 1u) + 1u > cap)
        return;
    if (!first)
        out[(*used)++] = ' ';
    memcpy(out + *used, text, len);
    *used += len;
    out[*used] = '\0';
}

/* Payload d'un verdict : champs applicables, ordre figé (§7.1). */
static void tr_payload(char *out, size_t cap, const toile_verdict *v,
                       const tr_state *st)
{
    char part[80];
    size_t used = 0;
    int first = 1;

    out[0] = '\0';
    if (v->cmd_type != TOILE_ID_NONE) {
        snprintf(part, sizeof part, "cmd=0x%04" PRIx32, v->cmd_type);
        tr_payload_add(out, cap, &used, first, part);
        first = 0;
    }
    if (v->resource_id != TOILE_ID_NONE) {
        snprintf(part, sizeof part, "resource=%" PRIu32, v->resource_id);
        tr_payload_add(out, cap, &used, first, part);
        first = 0;
    }
    if (v->scanout_id != TOILE_ID_NONE) {
        snprintf(part, sizeof part, "scanout=%" PRIu32, v->scanout_id);
        tr_payload_add(out, cap, &used, first, part);
        first = 0;
    }
    if (v->code == TOILE_TRANSFER_OK) {
        snprintf(part, sizeof part, "rect=%" PRIu32 ",%" PRIu32 ",%" PRIu32
                 ",%" PRIu32, v->rect.x, v->rect.y, v->rect.w, v->rect.h);
        tr_payload_add(out, cap, &used, first, part);
        first = 0;
    }
    if (v->cause[0] != '\0') {
        snprintf(part, sizeof part, "cause=%s", v->cause);
        tr_payload_add(out, cap, &used, first, part);
        first = 0;
    }
    if (v->code == TOILE_MODEL_COUNTEREXAMPLE) {
        snprintf(part, sizeof part, "state=%zu,%d", st->resources,
                 st->scanouts);
        tr_payload_add(out, cap, &used, first, part);
    }
}

/* Une ligne de verdict : "%04u %-26s %-7s" puis le payload s'il existe.
 * Sans payload, les espaces de remplissage de fin de ligne sont supprimés
 * (même règle que le jumeau Python, CONTRACT.md §7.1). */
static void tr_text_line(tr_writer *w, size_t index, const toile_verdict *v,
                         const tr_state *st)
{
    char payload[TR_PAYLOAD_MAX];
    char head[64];
    size_t len;

    tr_payload(payload, sizeof payload, v, st);
    len = (size_t)snprintf(head, sizeof head, "%04zu %-26s %-7s", index,
                           toile_verdict_name(v->code),
                           toile_verdict_severity(v->code));
    while (len > 0 && (head[len - 1] == ' ' || head[len - 1] == '\t'))
        head[--len] = '\0';
    tr_puts(w, head);
    if (payload[0] != '\0') {
        tr_putc(w, ' ');
        tr_puts(w, payload);
    }
    tr_putc(w, '\n');
}

int toile_report_text(const toile_model *m, const toile_verdict *v, size_t n,
                      const char *source, char *buf, size_t cap,
                      size_t *written)
{
    tr_writer w;
    tr_state st;
    size_t i;

    (void)source; /* le rapport texte ne cite pas la fixture */
    if (m == NULL || buf == NULL || cap == 0 || (v == NULL && n > 0))
        return TOILE_CMD_INVALID;

    tr_init(&w, buf, cap);
    tr_state_fill(m, &st);
    tr_puts(&w, "toile v1 text\n");
    for (i = 0; i < n; i++)
        tr_text_line(&w, i, &v[i], &st);
    tr_printf(&w,
              "# state resources=%zu scanouts=%d hash=%016" PRIx64
              " applied=%" PRIu64 " rejected=%" PRIu64 " flushes=%" PRIu64
              " bytes=%" PRIu64 " cursor=%" PRIu32 ",%" PRIu32 ",%" PRIu32
              ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "\n",
              st.resources, st.scanouts, st.hash, st.applied, st.rejected,
              st.flushes, st.bytes, st.cur_resource, st.cur_scanout, st.cur_x,
              st.cur_y, st.cur_hot_x, st.cur_hot_y);
    return tr_finish(&w, written);
}

int toile_report(const char *format, const toile_model *m,
                 const toile_verdict *v, size_t n, const char *source,
                 char *buf, size_t cap, size_t *written)
{
    if (format == NULL)
        return TOILE_CMD_INVALID;
    if (strcmp(format, "text") == 0)
        return toile_report_text(m, v, n, source, buf, cap, written);
    if (strcmp(format, "json") == 0)
        return toile_report_json(m, v, n, source, buf, cap, written);
    if (strcmp(format, "md") == 0)
        return toile_report_md(m, v, n, source, buf, cap, written);
    return TOILE_CMD_INVALID;
}
