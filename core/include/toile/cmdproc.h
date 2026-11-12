/* SPDX-License-Identifier: MIT */
/*
 * Toile — processeur de commandes (CONTRACT.md §1, §5, §10).
 *
 * parser → valider → appliquer → effet hôte → acquittement, sans retour en
 * arrière. Le processeur vérifie les invariants après chaque commande et
 * s'arrête au premier contre-exemple (fail fast).
 */
#ifndef TOILE_CMDPROC_H
#define TOILE_CMDPROC_H

#include <stddef.h>
#include <stdint.h>

#include "toile/resource.h"
#include "toile/vgproto.h"
#include "toile/verdict.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Applique n commandes. `out` reçoit au plus `cap` verdicts, *n_out le nombre
 * écrit. Renvoie TOILE_OK si aucun verdict n'est une erreur, sinon le premier
 * code d'erreur rencontré. Pointeurs NULL ou n == 0 : TOILE_CMD_INVALID.
 * Un modèle cohérent en fin de séquence ajoute TOILE_MODEL_CONSISTENT. */
int toile_cmdproc_run(const toile_cmd *cmds, size_t n, toile_model *model,
                      toile_verdict *out, size_t cap, size_t *n_out);

/* Analyse une ligne de DSL fixture (CONTRACT.md §4). 0 = OK ; sinon renvoie
 * TOILE_CMD_INVALID et écrit la raison dans `err` ("clé inconnue 'x'"…).
 * Les lignes vides et les commentaires renvoient 0 avec out->type == 0. */
int toile_cmd_parse_line(const char *line, unsigned long line_no,
                         toile_cmd *out, char *err, size_t err_cap);

/* Nom DSL d'un type ("resource_create_2d") ; NULL si inconnu. Identique à
 * toile_vg_cmd_name, conservé ici pour la lisibilité des appels du CLI. */
const char *toile_cmd_name(uint32_t type);

/* Nombre de verdicts d'une sévérité donnée (utilisé par les rapports). */
size_t toile_verdict_count_severity(const toile_verdict *v, size_t n,
                                    const char *severity);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_CMDPROC_H */
