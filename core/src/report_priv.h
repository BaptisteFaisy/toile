/* SPDX-License-Identifier: MIT */
/*
 * Toile — aide interne aux trois rapports (CONTRACT.md §7).
 *
 * Écrivain borné : aucune allocation, aucune E/S. Le tampon et sa capacité
 * viennent de l'appelant ; en cas de débordement, la fonction renvoie
 * TOILE_OUT_OF_BUFFER et *written reçoit la longueur nécessaire.
 *
 * Ce fichier est inclus par report.c, report_json.c et report_md.c : il
 * n'existe qu'une seule implémentation de l'écrivain et des règles de
 * présence des champs.
 */
#ifndef TOILE_REPORT_PRIV_H
#define TOILE_REPORT_PRIV_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "toile/report.h"
#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/verdict.h"

/* Taille du tampon de formatage interne (lignes courtes). */
#define TR_TMP 1024u
/* Taille maximale d'un payload de verdict (cause ≤ 96 caractères). */
#define TR_PAYLOAD_MAX 512u
/* Cellule vide d'un tableau markdown : tiret cadratin U+2013. */
#define TR_DASH "\xE2\x80\x93"

typedef struct {
    char *buf;   /* tampon de l'appelant */
    size_t cap;  /* capacité totale, '\0' inclus */
    size_t len;  /* octets produits (ou nécessaires si débordement) */
    int overflow;
} tr_writer;

static inline void tr_init(tr_writer *w, char *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->len = 0;
    w->overflow = 0;
}

/* Copie n octets ; si la place manque, le contenu est abandonné mais la
 * longueur nécessaire continue d'être comptée (débordement signalé). */
static inline void tr_write(tr_writer *w, const char *s, size_t n)
{
    size_t limit = (w->cap > 0) ? (w->cap - 1) : 0;

    if (w->buf != NULL && !w->overflow) {
        if (w->len + n > limit)
            w->overflow = 1;
        else
            memcpy(w->buf + w->len, s, n);
    }
    w->len += n;
}

static inline void tr_puts(tr_writer *w, const char *s)
{
    tr_write(w, s, strlen(s));
}

static inline void tr_putc(tr_writer *w, char c)
{
    tr_write(w, &c, 1);
}

static inline void tr_printf(tr_writer *w, const char *fmt, ...)
{
    char tmp[TR_TMP];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n >= sizeof tmp) {
        /* Troncature interne : longueur exacte conservée, contenu perdu. */
        w->overflow = 1;
        w->len += (size_t)n;
        return;
    }
    tr_write(w, tmp, (size_t)n);
}

/* Clôture : *written = longueur produite ; NUL terminé si la place suffit. */
static inline int tr_finish(tr_writer *w, size_t *written)
{
    if (written != NULL)
        *written = w->len;
    if (w->overflow)
        return TOILE_OUT_OF_BUFFER;
    if (w->buf != NULL && w->len < w->cap)
        w->buf[w->len] = '\0';
    return 0;
}

/* --- état du modèle tel que les rapports le présentent (CONTRACT.md §7) --- */

/* Curseur présenté dans l'état : même ordre que le JSON et que la ligne
 * « # state » du rapport texte. */
typedef struct {
    size_t resources;
    int scanouts;
    uint64_t hash;
    uint64_t applied;
    uint64_t rejected;
    uint64_t flushes;
    uint64_t bytes;
    uint32_t cur_resource;
    uint32_t cur_scanout;
    uint32_t cur_x;
    uint32_t cur_y;
    uint32_t cur_hot_x;
    uint32_t cur_hot_y;
    int cur_visible;
} tr_state;

static inline void tr_state_fill(const toile_model *m, tr_state *st)
{
    st->resources = toile_resource_count(m);
    st->scanouts = toile_scanout_count(m);
    st->hash = toile_model_state_hash(m);
    st->applied = m->cmds_applied;
    st->rejected = m->cmds_rejected;
    st->flushes = m->flushes;
    st->bytes = m->bytes_transferred;
    st->cur_resource = m->cursor.resource_id;
    st->cur_scanout = m->cursor.scanout_id;
    st->cur_x = m->cursor.x;
    st->cur_y = m->cursor.y;
    st->cur_hot_x = m->cursor.hot_x;
    st->cur_hot_y = m->cursor.hot_y;
    st->cur_visible = (m->cursor.visible != 0) ? 1 : 0;
}

#endif /* TOILE_REPORT_PRIV_H */
