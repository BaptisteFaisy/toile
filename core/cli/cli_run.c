/* SPDX-License-Identifier: MIT */
/*
 * Toile — exécution des fixtures CLI (CONTRACT.md §11).
 *
 * run   : fixture DSL .tcl (une commande par ligne)
 * wire  : fixture fil .hex (une commande par ligne de paires hexadécimales)
 * golden: rejoue une fixture et écrit fixtures/golden/<nom>.<fmt>
 *
 * Aucun état global : tout est alloué puis libéré, rien n'est écrit ailleurs
 * que sur stdout, dans le fichier golden et sur stderr.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cli_internal.h"

/* Declarations avancees : la garde de taille (§13) compte une fonction depuis
 * la derniere declaration precedente ; ces prototypes collent le decompte au
 * corps des fonctions au lieu de l'en-tete du fichier. */
static void cli_fail(const char *path, unsigned long line, const char *reason);
static char *cli_dup(const char *text, size_t len);
static char *cli_next_line(char **cursor);
static int cli_hex_val(int c);
static int cli_hex_line(const char *line, uint8_t *bytes, size_t cap, size_t *nb);

/* Lit un fichier entier en mémoire (terminateur '\0' ajouté). */
char *cli_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    size_t cap = 8192, n = 0, got;

    if (!f)
        return NULL;
    buf = malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (n + 1 >= cap) {
            char *b2;
            cap *= 2;
            b2 = realloc(buf, cap);
            if (!b2) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = b2;
        }
        got = fread(buf + n, 1, cap - n - 1, f);
        n += got;
        if (got == 0)
            break;
    }
    fclose(f);
    buf[n] = '\0';
    if (len)
        *len = n;
    return buf;
}

/* Message d'erreur de fixture : "<chemin>:<ligne>: <raison>" sur stderr.
 * Le parseur peut deja prefixer la raison par "<ligne>:" : on evite alors le
 * doublon en ne conservant que la raison. */
static void cli_fail(const char *path, unsigned long line, const char *reason)
{
    const char *r = (reason != NULL) ? reason : "";

    if (*r != '\0') {
        const char *p = r;
        while (*p >= '0' && *p <= '9')
            p++;
        if (p != r && *p == ':' && strtoul(r, NULL, 10) == line)
            r = p + 1;
    }
    fprintf(stderr, "%s:%lu: %s\n", path, line, r);
}

/* Copie modifiable d'un texte (les lignes seront découpées in place). */
static char *cli_dup(const char *text, size_t len)
{
    char *copy = malloc(len + 1);
    if (!copy)
        return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    return copy;
}

/* Valeur d'un chiffre hexadécimal, -1 si absent. */
static int cli_hex_val(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* Ligne suivante (le séparateur '\n' est neutralisé) ; NULL à la fin. */
static char *cli_next_line(char **cursor)
{
    char *start = *cursor;
    char *nl;

    if (start == NULL || *start == '\0')
        return NULL;
    nl = strchr(start, '\n');
    if (nl != NULL) {
        *nl = '\0';
        *cursor = nl + 1;
    } else {
        *cursor = start + strlen(start);
    }
    return start;
}

/* Fixture DSL (CONTRACT.md §4) : renvoie 0, ou -1 après message d'erreur. */
int cli_parse_dsl(const char *text, size_t len, const char *path,
                         toile_cmd **out_cmds, size_t *out_n)
{
    char *copy = cli_dup(text, len);
    toile_cmd *cmds;
    char *cursor, *line;
    unsigned long lineno = 0;
    size_t n = 0;

    if (copy == NULL)
        return -1;
    cmds = calloc(CLI_MAX_CMDS, sizeof *cmds);
    if (cmds == NULL) {
        free(copy);
        return -1;
    }
    cursor = copy;
    while ((line = cli_next_line(&cursor)) != NULL) {
        char err[TOILE_CAUSE_MAX];

        lineno++;
        if (n >= CLI_MAX_CMDS) {
            cli_fail(path, lineno, "trop de commandes");
            free(copy);
            free(cmds);
            return -1;
        }
        if (toile_cmd_parse_line(line, lineno, &cmds[n], err, sizeof err) != 0) {
            cli_fail(path, lineno, err);
            free(copy);
            free(cmds);
            return -1;
        }
        if (cmds[n].type == 0) /* ligne vide ou commentaire */
            continue;
        n++;
    }
    free(copy);
    *out_cmds = cmds;
    *out_n = n;
    return 0;
}

/* Une ligne de paires hexadécimales : 0 = OK, -1 = caractère ou longueur
 * invalide, ou nombre impair de chiffres. */
static int cli_hex_line(const char *line, uint8_t *bytes, size_t cap, size_t *nb)
{
    const char *p;
    int hi = -1;

    *nb = 0;
    for (p = line; *p != '\0'; p++) {
        int v;

        if (*p == '#')
            break;
        if (*p == ' ' || *p == '\t' || *p == '\r')
            continue;
        v = cli_hex_val((unsigned char)*p);
        if (v < 0)
            return -1;
        if (hi < 0) {
            hi = v;
            continue;
        }
        if (*nb >= cap)
            return -1;
        bytes[(*nb)++] = (uint8_t)((hi << 4) | v);
        hi = -1;
    }
    return (hi < 0) ? 0 : -1;
}

/* Traite une ligne de fixture fil : 0 = commandeaccepted, 1 = ligne vide ou
 * commentaire, -1 = erreur (message émis). */
static int cli_wire_line(const char *line, toile_cmd *cmd, unsigned long lineno,
                         const char *path)
{
    uint8_t bytes[TOILE_VG_MAX_CMD_LEN];
    size_t nb = 0;

    if (cli_hex_line(line, bytes, sizeof bytes, &nb) != 0) {
        cli_fail(path, lineno, "ligne hexadecimale invalide");
        return -1;
    }
    if (nb == 0)
        return 1;
    if (toile_vg_decode(bytes, nb, cmd) != 0) {
        cli_fail(path, lineno, "commande fil invalide");
        return -1;
    }
    return 0;
}

/* Fixture fil (CONTRACT.md §4) : paires hexadécimales, '#' et espaces ignorés. */
int cli_parse_wire(const char *text, size_t len, const char *path,
                   toile_cmd **out_cmds, size_t *out_n)
{
    char *copy = cli_dup(text, len);
    toile_cmd *cmds;
    char *cursor, *line;
    unsigned long lineno = 0;
    size_t n = 0;
    int rc = 0;

    if (copy == NULL)
        return -1;
    cmds = calloc(CLI_MAX_CMDS, sizeof *cmds);
    if (cmds == NULL) {
        free(copy);
        return -1;
    }
    cursor = copy;
    while ((line = cli_next_line(&cursor)) != NULL) {
        int st;

        lineno++;
        if (n >= CLI_MAX_CMDS) {
            cli_fail(path, lineno, "trop de commandes");
            rc = -1;
            break;
        }
        st = cli_wire_line(line, &cmds[n], lineno, path);
        if (st < 0) {
            rc = -1;
            break;
        }
        if (st == 0)
            n++;
    }
    free(copy);
    if (rc != 0) {
        free(cmds);
        return -1;
    }
    *out_cmds = cmds;
    *out_n = n;
    return 0;
}
