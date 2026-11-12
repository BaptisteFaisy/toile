/* SPDX-License-Identifier: MIT */
/*
 * Toile — CLI (CONTRACT.md §11).
 *
 *   toile-run run    <fixture.tcl> [--format text|json|md] [--expect <golden>]
 *   toile-run wire   <fixture.hex> [--format text|json|md] [--expect <golden>]
 *   toile-run golden <fixture.tcl> [--format text|json|md]
 *   toile-run selftest
 *
 * Codes de sortie : 0 aucune erreur, 1 verdict d'erreur, 2 usage ou fixture
 * invalide, 3 divergence avec --expect.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cli_internal.h"

static void cli_usage(FILE *f)
{
    fputs("usage:\n"
          "  toile-run run    <fixture.tcl> [--format text|json|md] [--expect <golden>]\n"
          "  toile-run wire   <fixture.hex> [--format text|json|md] [--expect <golden>]\n"
          "  toile-run golden <fixture.tcl> [--format text|json|md]\n"
          "  toile-run selftest\n"
          "\n"
          "codes de sortie : 0 ok, 1 verdict d'erreur, 2 usage/fixture invalide,\n"
          "                  3 divergence avec --expect\n", f);
}

/* Crée un répertoire (EXISTANT tolerated). */
static int cli_mkdir_p(const char *dir)
{
    if (mkdir(dir, 0777) == 0 || errno == EEXIST)
        return 0;
    return -1;
}

/* Vérifie que le format demandé existe. */
static int cli_format_ok(const char *fmt)
{
    return strcmp(fmt, "text") == 0 || strcmp(fmt, "json") == 0 ||
           strcmp(fmt, "md") == 0;
}

/* run : fixture DSL. */
int cli_cmd_run(const cli_opts *o)
{
    size_t len = 0;
    char *text = cli_read_file(o->fixture, &len);
    toile_cmd *cmds = NULL;
    size_t n = 0;
    int rc;

    if (text == NULL) {
        fprintf(stderr, "toile: fixture illisible: %s\n", o->fixture);
        return CLI_EXIT_USAGE;
    }
    if (cli_parse_dsl(text, len, o->fixture, &cmds, &n) != 0) {
        free(text);
        return CLI_EXIT_USAGE;
    }
    free(text);
    rc = cli_execute(cmds, n, o, NULL);
    free(cmds);
    return rc;
}

/* wire : fixture fil (paires hexadécimales). */
int cli_cmd_wire(const cli_opts *o)
{
    size_t len = 0;
    char *text = cli_read_file(o->fixture, &len);
    toile_cmd *cmds = NULL;
    size_t n = 0;
    int rc;

    if (text == NULL) {
        fprintf(stderr, "toile: fixture illisible: %s\n", o->fixture);
        return CLI_EXIT_USAGE;
    }
    if (cli_parse_wire(text, len, o->fixture, &cmds, &n) != 0) {
        free(text);
        return CLI_EXIT_USAGE;
    }
    free(text);
    rc = cli_execute(cmds, n, o, NULL);
    free(cmds);
    return rc;
}

/* golden : rejoue la fixture et écrit fixtures/golden/<nom-sans-ext>.<fmt>. */
int cli_cmd_golden(const cli_opts *o)
{
    char path[512];
    const char *base, *dot;
    size_t blen, len = 0;
    char *text;
    toile_cmd *cmds = NULL;
    size_t n = 0;
    int rc;

    if (cli_mkdir_p("fixtures") != 0 || cli_mkdir_p("fixtures/golden") != 0) {
        fprintf(stderr, "toile: creation de fixtures/golden impossible\n");
        return CLI_EXIT_USAGE;
    }
    base = strrchr(o->fixture, '/');
    base = (base != NULL) ? base + 1 : o->fixture;
    dot = strrchr(base, '.');
    blen = (dot != NULL) ? (size_t)(dot - base) : strlen(base);
    if (blen == 0) {
        fprintf(stderr, "toile: nom de fixture vide\n");
        return CLI_EXIT_USAGE;
    }
    (void)snprintf(path, sizeof path, "fixtures/golden/%.*s.%s", (int)blen,
                   base, o->format);
    text = cli_read_file(o->fixture, &len);
    if (text == NULL) {
        fprintf(stderr, "toile: fixture illisible: %s\n", o->fixture);
        return CLI_EXIT_USAGE;
    }
    if (cli_parse_dsl(text, len, o->fixture, &cmds, &n) != 0) {
        free(text);
        return CLI_EXIT_USAGE;
    }
    free(text);
    rc = cli_execute(cmds, n, o, path);
    free(cmds);
    if (rc == CLI_EXIT_OK)
        fprintf(stderr, "toile: golden ecrit: %s\n", path);
    return rc;
}

/* Analyse des options : fixture positionnelle, --format, --expect. */
static int cli_parse_args(int argc, char **argv, cli_opts *o)
{
    int i;

    for (i = 2; i < argc; i++) {
        const char *a = argv[i];

        if (strcmp(a, "--format") == 0 && i + 1 < argc) {
            o->format = argv[++i];
        } else if (strcmp(a, "--expect") == 0 && i + 1 < argc) {
            o->expect = argv[++i];
        } else if (strncmp(a, "--", 2) == 0) {
            fprintf(stderr, "toile: option inconnue ou incomplete: %s\n", a);
            return -1;
        } else if (o->fixture == NULL) {
            o->fixture = a;
        } else {
            fprintf(stderr, "toile: argument en trop: %s\n", a);
            return -1;
        }
    }
    if (o->fixture == NULL) {
        fprintf(stderr, "toile: fixture manquante\n");
        return -1;
    }
    if (!cli_format_ok(o->format)) {
        fprintf(stderr, "toile: format inconnu: %s\n", o->format);
        return -1;
    }
    return 0;
}

/* Point d'entrée : dispatch des sous-commandes §11. */
int main(int argc, char **argv)
{
    cli_opts o;
    const char *cmd;

    memset(&o, 0, sizeof o);
    o.format = "text";
    if (argc < 2) {
        cli_usage(stderr);
        return CLI_EXIT_USAGE;
    }
    cmd = argv[1];
    if (strcmp(cmd, "selftest") == 0) {
        if (argc != 2) {
            fprintf(stderr, "toile: selftest ne prend aucun argument\n");
            cli_usage(stderr);
            return CLI_EXIT_USAGE;
        }
        return cli_cmd_selftest();
    }
    if (strcmp(cmd, "run") != 0 && strcmp(cmd, "wire") != 0 &&
        strcmp(cmd, "golden") != 0) {
        fprintf(stderr, "toile: sous-commande inconnue: %s\n", cmd);
        cli_usage(stderr);
        return CLI_EXIT_USAGE;
    }
    if (cli_parse_args(argc, argv, &o) != 0) {
        cli_usage(stderr);
        return CLI_EXIT_USAGE;
    }
    if (strcmp(cmd, "run") == 0)
        return cli_cmd_run(&o);
    if (strcmp(cmd, "wire") == 0)
        return cli_cmd_wire(&o);
    return cli_cmd_golden(&o);
}
