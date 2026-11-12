/* SPDX-License-Identifier: MIT */
/*
 * Toile — rapports de sortie (CONTRACT.md §7).
 *
 * Trois formats, figés octet pour octet et reproduits à l'identique par le
 * jumeau Python : text (défaut), json (une ligne), md (audit). L'appelant
 * fournit le tampon ; rien n'est écrit sur disque ici.
 */
#ifndef TOILE_REPORT_H
#define TOILE_REPORT_H

#include <stddef.h>

#include "toile/resource.h"
#include "toile/verdict.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Codes de retour des trois fonctions :
 *   0 = écrit intégralement ; *written = longueur produite (hors '\0')
 *   TOILE_CMD_INVALID = argument invalide (pointeur NULL, format inconnu)
 *   TOILE_OUT_OF_BUFFER = tampon trop court ; *written = longueur nécessaire
 */
#define TOILE_OUT_OF_BUFFER 100

int toile_report_text(const toile_model *m, const toile_verdict *v, size_t n,
                      const char *source, char *buf, size_t cap, size_t *written);

int toile_report_json(const toile_model *m, const toile_verdict *v, size_t n,
                      const char *source, char *buf, size_t cap, size_t *written);

int toile_report_md(const toile_model *m, const toile_verdict *v, size_t n,
                    const char *source, char *buf, size_t cap, size_t *written);

/* Aiguillage par nom de format ("text", "json", "md") ; TOILE_CMD_INVALID si
 * le nom est inconnu. */
int toile_report(const char *format, const toile_model *m,
                 const toile_verdict *v, size_t n, const char *source,
                 char *buf, size_t cap, size_t *written);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_REPORT_H */
