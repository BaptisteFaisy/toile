/* SPDX-License-Identifier: MIT */
/*
 * Toile — verdicts du modèle (CONTRACT.md §6).
 *
 * Un verdict est la trace d'une décision : jamais d'application partielle
 * silencieuse. Les codes et les sévérités sont gelés ; le jumeau Python
 * (host/toile/verdict.py) reproduit exactement les mêmes chaînes.
 */
#ifndef TOILE_VERDICT_H
#define TOILE_VERDICT_H

#include <stddef.h>
#include <stdint.h>

#include "toile/vgproto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum toile_verdict_code {
    TOILE_OK                    = 0,
    TOILE_CMD_INVALID           = 1,
    TOILE_RESOURCE_EXISTS       = 2,
    TOILE_RESOURCE_MISSING      = 3,
    TOILE_BACKING_MISSING       = 4,
    TOILE_SCANOUT_INVALID       = 5,
    TOILE_FLUSH_NO_SCANOUT      = 6,
    TOILE_TRANSFER_OK           = 7,
    TOILE_MODEL_CONSISTENT      = 8,
    TOILE_MODEL_COUNTEREXAMPLE  = 9
} toile_verdict_code;

#define TOILE_VERDICT_COUNT 10u

typedef struct toile_verdict {
    toile_verdict_code code;
    uint32_t cmd_index;   /* rang dans la séquence ; TOILE_ID_NONE si sans objet */
    uint32_t cmd_type;    /* TOILE_VG_CMD_* ; TOILE_ID_NONE si sans objet */
    uint32_t resource_id; /* TOILE_ID_NONE si sans objet */
    uint32_t scanout_id;  /* TOILE_ID_NONE si sans objet */
    toile_rect rect;      /* significative pour TOILE_TRANSFER_OK */
    char cause[TOILE_CAUSE_MAX]; /* cause figée §6, vide sinon */
} toile_verdict;

/* Nom exact de l'enum ("TOILE_OK") ; "TOILE_UNKNOWN" si hors bornes. */
const char *toile_verdict_name(toile_verdict_code code);

/* Sévérité : "info", "warning" ou "error". */
const char *toile_verdict_severity(toile_verdict_code code);

/* 1 si la sévérité est "error", 0 sinon. */
int toile_verdict_is_error(toile_verdict_code code);

/* Remplit un verdict ; les champs non fournis passent à TOILE_ID_NONE. */
void toile_verdict_set(toile_verdict *v, toile_verdict_code code,
                       uint32_t cmd_index, uint32_t cmd_type,
                       uint32_t resource_id, uint32_t scanout_id,
                       toile_rect rect, const char *cause);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_VERDICT_H */
