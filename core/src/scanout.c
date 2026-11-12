/* SPDX-License-Identifier: MIT */
/*
 * Toile — scanouts (têtes virtuelles) : implémentation de toile/scanout.h.
 *
 * C11 pur : aucune allocation, aucune E/S, aucune horloge. En cas d'échec,
 * l'état reste inchangé hors cmds_rejected (CONTRACT.md §3, §6).
 *
 * Ordre des gardes de toile_scanout_set (figé par le lot) :
 *   1. scanout >= TOILE_MAX_SCANOUTS  -> TOILE_CMD_INVALID
 *      (cause processeur : scanout_id_out_of_range)
 *   2. resource_id == 0               -> désactive le scanout, TOILE_OK
 *   3. ressource absente              -> TOILE_SCANOUT_INVALID
 *   4. w == 0 || h == 0               -> TOILE_CMD_INVALID (bad_geometry)
 *   5. table pleine (16 actifs) et nouveau scanout -> TOILE_CMD_INVALID
 *      (scanout_table_full) ; déplacer un scanout déjà actif reste possible.
 */
#include "toile/scanout.h"

#include <string.h>

#include "toile/verdict.h" /* codes TOILE_* */

/* Enregistre un refus : cmds_rejected++, état inchangé par ailleurs. */
static int scanout_reject(toile_model *m, int code)
{
    m->cmds_rejected++;
    return code;
}

/* Efface un emplacement de scanout (désactivation). */
static void scanout_clear(toile_scanout *s)
{
    s->id = 0;
    s->resource_id = 0;
    s->in_use = 0;
    s->rect.x = 0;
    s->rect.y = 0;
    s->rect.w = 0;
    s->rect.h = 0;
}

int toile_scanout_set(toile_model *m, uint32_t scanout, uint32_t resource_id,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    toile_scanout *s;

    if (m == NULL) {
        return TOILE_CMD_INVALID;
    }
    if (scanout >= TOILE_MAX_SCANOUTS) {
        return scanout_reject(m, TOILE_CMD_INVALID);
    }
    s = &m->scanouts[scanout];
    if (resource_id == 0) { /* comportement du standard : id=0 désactive */
        scanout_clear(s);
        m->cmds_applied++;
        return TOILE_OK;
    }
    if (toile_resource_find(m, resource_id) == NULL) {
        return scanout_reject(m, TOILE_SCANOUT_INVALID);
    }
    if (w == 0 || h == 0) {
        return scanout_reject(m, TOILE_CMD_INVALID);
    }
    if (s->in_use == 0 &&
        toile_scanout_count(m) >= (int)TOILE_MAX_SCANOUTS) {
        return scanout_reject(m, TOILE_CMD_INVALID);
    }
    s->id = scanout;
    s->resource_id = resource_id;
    s->in_use = 1;
    s->rect.x = x;
    s->rect.y = y;
    s->rect.w = w;
    s->rect.h = h;
    m->cmds_applied++;
    return TOILE_OK;
}

const toile_scanout *toile_scanout_find(const toile_model *m, uint32_t scanout)
{
    if (m == NULL || scanout >= TOILE_MAX_SCANOUTS) {
        return NULL;
    }
    return m->scanouts[scanout].in_use ? &m->scanouts[scanout] : NULL;
}

int toile_scanout_count(const toile_model *m)
{
    int n = 0;

    if (m == NULL) {
        return 0;
    }
    for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        n += m->scanouts[i].in_use ? 1 : 0;
    }
    return n;
}

int toile_resource_has_scanout(const toile_model *m, uint32_t resource_id)
{
    if (m == NULL || resource_id == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        const toile_scanout *s = &m->scanouts[i];

        if (s->in_use && s->resource_id == resource_id) {
            return 1;
        }
    }
    return 0;
}

/* --- invariants du modèle (CONTRACT.md §5, I1..I6) ----------------------- */
/*
 * I2, I3 et I6 sont des propriétés comportementales ; elles sont reformulées
 * en prédicats d'état SONDES (aucun faux positif sur une séquence légitime) :
 *   I1  tout scanout actif référence une ressource existante.
 *   I2  flush_count > 0 implique in_use : un flush réussi ne laisse pas de
 *       trace sur un emplacement libre (unref remet le compteur à zéro).
 *   I3  n_pages > 0 implique in_use : un backing (exigé par les transferts)
 *       ne survit pas à la destruction de sa ressource.
 *   I4  aucune ressource active de largeur/hauteur nulle ou de format
 *       inconnu (toile_fmt_bpp == 0).
 *   I5  tables dans leurs limites §3 : in_use vaut 0 ou 1 et
 *       n_pages <= TOILE_MAX_PAGES (les tailles de tableaux sont figées).
 *   I6  bytes_transferred est une somme d'aires x bpp ; tous les formats §3
 *       font 4 octets/pixel, donc cette somme est un multiple de 4.
 * Vérification dans l'ordre I1..I6 : attribution de cause déterministe.
 */

/* I1 — scanout actif => ressource existante. */
static const char *chk_i1(const toile_model *m)
{
    for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        const toile_scanout *s = &m->scanouts[i];

        if (s->in_use && toile_resource_find(m, s->resource_id) == NULL) {
            return "invariant_i1";
        }
    }
    return NULL;
}

/* I2 — trace de flush uniquement sur une ressource vivante. */
static const char *chk_i2(const toile_model *m)
{
    for (uint32_t i = 0; i < TOILE_MAX_RESOURCES; i++) {
        const toile_resource *r = &m->resources[i];

        if (!r->in_use && r->flush_count > 0) {
            return "invariant_i2";
        }
    }
    return NULL;
}

/* I3 — backing uniquement sur une ressource vivante. */
static const char *chk_i3(const toile_model *m)
{
    for (uint32_t i = 0; i < TOILE_MAX_RESOURCES; i++) {
        const toile_resource *r = &m->resources[i];

        if (!r->in_use && r->n_pages > 0) {
            return "invariant_i3";
        }
    }
    return NULL;
}

/* I4 — géométrie et format valides sur toute ressource active. */
static const char *chk_i4(const toile_model *m)
{
    for (uint32_t i = 0; i < TOILE_MAX_RESOURCES; i++) {
        const toile_resource *r = &m->resources[i];

        if (r->in_use &&
            (r->width == 0 || r->height == 0 ||
             toile_fmt_bpp(r->fmt) == 0)) {
            return "invariant_i4";
        }
    }
    return NULL;
}

/* I5 — drapeaux et bornes §3 respectés sur chaque emplacement. */
static const char *chk_i5(const toile_model *m)
{
    for (uint32_t i = 0; i < TOILE_MAX_RESOURCES; i++) {
        const toile_resource *r = &m->resources[i];

        if (r->in_use > 1 || r->n_pages > TOILE_MAX_PAGES) {
            return "invariant_i5";
        }
    }
    for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
        if (m->scanouts[i].in_use > 1) {
            return "invariant_i5";
        }
    }
    return NULL;
}

/* I6 — bytes_transferred multiple de 4 (bpp constant §3). */
static const char *chk_i6(const toile_model *m)
{
    return (m->bytes_transferred % 4u) ? "invariant_i6" : NULL;
}

/* Copie `src` dans `dst` avec terminaison NUL garantie, sans allocation. */
static void cause_copy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);

    if (dst == NULL || cap == 0) {
        return;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int toile_model_check_invariants(const toile_model *m, char *cause, size_t cap)
{
    static const char *(*const checks[])(const toile_model *) = {
        chk_i1, chk_i2, chk_i3, chk_i4, chk_i5, chk_i6,
    };

    if (m == NULL) {
        return TOILE_MODEL_COUNTEREXAMPLE;
    }
    for (size_t i = 0; i < sizeof checks / sizeof checks[0]; i++) {
        const char *hit = checks[i](m);

        if (hit != NULL) {
            cause_copy(cause, cap, hit);
            return TOILE_MODEL_COUNTEREXAMPLE;
        }
    }
    cause_copy(cause, cap, "");
    return 0;
}
