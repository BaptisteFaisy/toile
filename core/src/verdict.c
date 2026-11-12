/* SPDX-License-Identifier: MIT */
/*
 * Toile — verdicts du modèle : implémentation de toile/verdict.h.
 *
 * Tableaux static const indexés par code (CONTRACT.md §6) ; garde contre
 * les codes hors bornes ("TOILE_UNKNOWN" / "error"). Aucune allocation,
 * aucune E/S.
 */
#include "toile/verdict.h"

/* Noms exacts de l'enum, dans l'ordre des codes 0..9. */
static const char *const verdict_names[TOILE_VERDICT_COUNT] = {
    "TOILE_OK",
    "TOILE_CMD_INVALID",
    "TOILE_RESOURCE_EXISTS",
    "TOILE_RESOURCE_MISSING",
    "TOILE_BACKING_MISSING",
    "TOILE_SCANOUT_INVALID",
    "TOILE_FLUSH_NO_SCANOUT",
    "TOILE_TRANSFER_OK",
    "TOILE_MODEL_CONSISTENT",
    "TOILE_MODEL_COUNTEREXAMPLE",
};

/* Sévérités figées §6, dans l'ordre des codes 0..9. */
static const char *const verdict_severities[TOILE_VERDICT_COUNT] = {
    "info",     /* TOILE_OK */
    "error",    /* TOILE_CMD_INVALID */
    "warning",  /* TOILE_RESOURCE_EXISTS */
    "error",    /* TOILE_RESOURCE_MISSING */
    "error",    /* TOILE_BACKING_MISSING */
    "error",    /* TOILE_SCANOUT_INVALID */
    "warning",  /* TOILE_FLUSH_NO_SCANOUT */
    "info",     /* TOILE_TRANSFER_OK */
    "info",     /* TOILE_MODEL_CONSISTENT */
    "error",    /* TOILE_MODEL_COUNTEREXAMPLE */
};

/* 1 si le code est dans [0, TOILE_VERDICT_COUNT[. */
static int code_in_range(toile_verdict_code code)
{
    return (unsigned)code < TOILE_VERDICT_COUNT;
}

const char *toile_verdict_name(toile_verdict_code code)
{
    return code_in_range(code) ? verdict_names[code] : "TOILE_UNKNOWN";
}

const char *toile_verdict_severity(toile_verdict_code code)
{
    return code_in_range(code) ? verdict_severities[code] : "error";
}

/* Comparaison de chaînes littérales (pas de <string.h> : zéro dépendance). */
static int str_const_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int toile_verdict_is_error(toile_verdict_code code)
{
    return str_const_eq(toile_verdict_severity(code), "error") ? 1 : 0;
}

/* Copie tronquée avec terminaison NUL garantie dans dst[TOILE_CAUSE_MAX]. */
static void cause_copy(char *dst, const char *src)
{
    size_t i = 0;

    if (src) {
        for (; i + 1 < TOILE_CAUSE_MAX && src[i]; i++)
            dst[i] = src[i];
    }
    dst[i] = '\0';
    for (i = i + 1; i < TOILE_CAUSE_MAX; i++)
        dst[i] = '\0';
}

void toile_verdict_set(toile_verdict *v, toile_verdict_code code,
                       uint32_t cmd_index, uint32_t cmd_type,
                       uint32_t resource_id, uint32_t scanout_id,
                       toile_rect rect, const char *cause)
{
    if (!v)
        return;
    v->code = code;
    v->cmd_index = cmd_index;
    v->cmd_type = cmd_type;
    v->resource_id = resource_id;
    v->scanout_id = scanout_id;
    v->rect = rect;
    cause_copy(v->cause, cause);
}
