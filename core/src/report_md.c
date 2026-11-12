/* SPDX-License-Identifier: MIT */
/*
 * Toile — rapport d'audit Markdown (CONTRACT.md §7.3).
 *
 * Cellule vide = tiret cadratin U+2013 (TR_DASH), aucune ligne vide après le
 * tableau, '\n' final. Identique octet pour octet à host/toile/report.py.
 */
#include <inttypes.h>

#include "report_priv.h"
#include "toile/verdict.h"

/* Écrit une cellule : vide ⇒ tiret cadratin. */
static void tr_cell(tr_writer *w, const char *text, int present)
{
    if (present && text != NULL && text[0] != '\0')
        tr_puts(w, text);
    else
        tr_puts(w, TR_DASH);
}

static void tr_md_row(tr_writer *w, size_t index, const toile_verdict *v)
{
    char cmd[16];
    char id[16];
    char scan[16];
    char rect[64];

    if (v->cmd_type != TOILE_ID_NONE)
        snprintf(cmd, sizeof cmd, "0x%04" PRIx32, v->cmd_type);
    else
        cmd[0] = '\0';
    if (v->resource_id != TOILE_ID_NONE)
        snprintf(id, sizeof id, "%" PRIu32, v->resource_id);
    else
        id[0] = '\0';
    if (v->scanout_id != TOILE_ID_NONE)
        snprintf(scan, sizeof scan, "%" PRIu32, v->scanout_id);
    else
        scan[0] = '\0';
    if (v->code == TOILE_TRANSFER_OK)
        snprintf(rect, sizeof rect, "%" PRIu32 ",%" PRIu32 ",%" PRIu32
                 ",%" PRIu32, v->rect.x, v->rect.y, v->rect.w, v->rect.h);
    else
        rect[0] = '\0';

    tr_puts(w, "| ");
    tr_printf(w, "%04zu", index);
    tr_puts(w, " | ");
    tr_cell(w, toile_verdict_name(v->code), 1);
    tr_puts(w, " | ");
    tr_cell(w, toile_verdict_severity(v->code), 1);
    tr_puts(w, " | ");
    tr_cell(w, cmd, 1);
    tr_puts(w, " | ");
    tr_cell(w, id, 1);
    tr_puts(w, " | ");
    tr_cell(w, scan, 1);
    tr_puts(w, " | ");
    tr_cell(w, rect, 1);
    tr_puts(w, " | ");
    tr_cell(w, v->cause, 1);
    tr_puts(w, " |\n");
}

/* Compte les verdicts par sévérité : erreurs, avertissements, info. */
static void tr_md_counts(const toile_verdict *v, size_t n, size_t *err,
                         size_t *warn, size_t *info)
{
    size_t i;

    *err = 0;
    *warn = 0;
    *info = 0;
    for (i = 0; i < n; i++) {
        if (toile_verdict_is_error(v[i].code) != 0)
            (*err)++;
        else if (v[i].code == TOILE_RESOURCE_EXISTS ||
                 v[i].code == TOILE_FLUSH_NO_SCANOUT)
            (*warn)++;
        else
            (*info)++;
    }
}

int toile_report_md(const toile_model *m, const toile_verdict *v, size_t n,
                    const char *source, char *buf, size_t cap, size_t *written)
{
    tr_writer w;
    tr_state st;
    size_t err, warn, info, i;

    if (m == NULL || buf == NULL || cap == 0 || (v == NULL && n > 0))
        return TOILE_CMD_INVALID;

    tr_init(&w, buf, cap);
    tr_state_fill(m, &st);
    tr_md_counts(v, n, &err, &warn, &info);
    tr_puts(&w, "# Rapport d'audit Toile\n\n");
    tr_printf(&w, "- fixture : `%s`\n", (source != NULL) ? source : "-");
    tr_printf(&w, "- verdicts : %zu (erreurs %zu, avertissements %zu, "
                  "info %zu)\n", n, err, warn, info);
    tr_printf(&w, "- état : %zu ressources, %d scanouts, hash `%016" PRIx64
                  "`\n\n", st.resources, st.scanouts, st.hash);
    tr_puts(&w, "| # | code | sévérité | cmd | ressource | scanout | rect | "
                "cause |\n");
    tr_puts(&w, "|---|---|---|---|---|---|---|---|\n");
    for (i = 0; i < n; i++)
        tr_md_row(&w, i, &v[i]);
    return tr_finish(&w, written);
}
