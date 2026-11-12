/* SPDX-License-Identifier: MIT */
/*
 * Toile — CLI (CONTRACT.md §11) : interfaces internes partagées entre
 * toile_cli.c et cli_run.c. Fichiers autorisés ici (fopen/mkdir).
 */
#ifndef TOILE_CLI_INTERNAL_H
#define TOILE_CLI_INTERNAL_H

#include <stddef.h>

#include "toile/cmdproc.h"
#include "toile/report.h"
#include "toile/resource.h"
#include "toile/vgproto.h"

/* Codes de sortie §11. */
#define CLI_EXIT_OK       0
#define CLI_EXIT_ERROR    1
#define CLI_EXIT_USAGE    2
#define CLI_EXIT_DIVERGE  3

/* Bornes du CLI (allocation dynamique au-delà des piles). */
#define CLI_MAX_CMDS      4096u
#define CLI_MAX_LINE      4096u

/* Tampon initial des rapports (redimensionné si le coeur demande plus). */
#define CLI_REPORT_CAP    (1u << 20)

typedef struct cli_opts {
    const char *fixture; /* chemin d'entrée */
    const char *format;  /* "text" (défaut), "json", "md" */
    const char *expect;  /* chemin golden ou NULL */
} cli_opts;

/* Lit un fichier entier ; retourne un tampon malloc terminé par '\0'
 * (*len reçoit la taille hors '\0'), NULL si illisible. */
char *cli_read_file(const char *path, size_t *len);

/* Moteur commun : exécute les commandes, produit le rapport, l'écrit sur
 * stdout (out_path NULL) ou dans out_path (sous-commande golden), compare
 * à --expect. Renvoie un code CLI_EXIT_*. */
int cli_execute(const toile_cmd *cmds, size_t n, const cli_opts *o,
                const char *out_path);

/* Analyse une fixture DSL / une fixture fil vers un tableau de commandes.
 * Renvoie 0, ou -1 après avoir écrit "<chemin>:<ligne>: <raison>" sur stderr. */
int cli_parse_dsl(const char *text, size_t len, const char *path,
                   toile_cmd **out_cmds, size_t *out_n);
int cli_parse_wire(const char *text, size_t len, const char *path,
                    toile_cmd **out_cmds, size_t *out_n);

int cli_cmd_run(const cli_opts *o);    /* fixture DSL .tcl */
int cli_cmd_wire(const cli_opts *o);   /* fixture fil .hex */
int cli_cmd_golden(const cli_opts *o); /* écrit fixtures/golden/<nom>.<fmt> */
int cli_cmd_selftest(void);            /* assertions internes */

#endif /* TOILE_CLI_INTERNAL_H */
