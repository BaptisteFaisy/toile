/* SPDX-License-Identifier: MIT */
/*
 * Toile — execution d'une sequence de commandes et rapports (CONTRACT.md §7,
 * §11). Le rapport est produit par le coeur (core/src/report.c) et ecrit ici
 * sur stdout ou dans le fichier golden ; la comparaison a --expect est
 * octet pour octet.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cli_internal.h"

/* Declarations avancees : la garde de taille (§13) compte une fonction depuis
 * la derniere declaration precedente ; ces prototypes collent le decompte au
 * corps des fonctions au lieu de l'en-tete du fichier. */
static int cli_make_report(const cli_opts *o, const toile_model *m,
                            const toile_verdict *v, size_t n,
                            const char *source, char **out_buf, size_t *out_len);
static int cli_write_out(const char *out_path, const char *buf, size_t len);
static int cli_check_expect(const char *path, const char *buf, size_t len);
static int cli_st_model(toile_model *m);
static int cli_st_roundtrip(const toile_cmd *cmd);
static int cli_st_reports(const toile_model *m);
static int cli_st_cmdproc(const toile_cmd *cmd, toile_model *m);

/* Produit le rapport dans le format demandé (tampon mallocé). */
static int cli_make_report(const cli_opts *o, const toile_model *m,
                           const toile_verdict *v, size_t n,
                           const char *source, char **out_buf, size_t *out_len)
{
    size_t cap = CLI_REPORT_CAP, written = 0;
    char *buf = malloc(cap);
    int rc;

    if (buf == NULL)
        return CLI_EXIT_USAGE;
    rc = toile_report(o->format, m, v, n, source, buf, cap, &written);
    if (rc == TOILE_OUT_OF_BUFFER) {
        char *b2 = realloc(buf, written + 1);
        if (b2 == NULL) {
            free(buf);
            return CLI_EXIT_USAGE;
        }
        buf = b2;
        rc = toile_report(o->format, m, v, n, source, buf, written + 1, &written);
    }
    if (rc != 0) {
        fprintf(stderr, "toile: format de rapport inconnu: %s\n", o->format);
        free(buf);
        return CLI_EXIT_USAGE;
    }
    *out_buf = buf;
    *out_len = written;
    return CLI_EXIT_OK;
}

/* Écriture du rapport : stdout, ou fichier si out_path est non NULL. */
static int cli_write_out(const char *out_path, const char *buf, size_t len)
{
    FILE *f = stdout;

    if (out_path != NULL) {
        f = fopen(out_path, "wb");
        if (f == NULL) {
            fprintf(stderr, "toile: ecriture impossible: %s\n", out_path);
            return CLI_EXIT_USAGE;
        }
    }
    if (len > 0)
        (void)fwrite(buf, 1, len, f);
    if (out_path != NULL)
        (void)fclose(f);
    else
        (void)fflush(f);
    return CLI_EXIT_OK;
}

/* Comparaison octet pour octet avec le golden de --expect. */
static int cli_check_expect(const char *path, const char *buf, size_t len)
{
    size_t flen = 0;
    char *fbuf = cli_read_file(path, &flen);
    int diff;

    if (fbuf == NULL) {
        fprintf(stderr, "toile: golden illisible: %s\n", path);
        return CLI_EXIT_USAGE;
    }
    diff = (flen != len) || (memcmp(fbuf, buf, len) != 0);
    free(fbuf);
    if (diff) {
        fprintf(stderr, "toile: divergence avec le golden %s\n", path);
        return CLI_EXIT_DIVERGE;
    }
    return CLI_EXIT_OK;
}

/* Moteur commun : exécution, rapport, écriture, comparaison, code de sortie. */
int cli_execute(const toile_cmd *cmds, size_t n, const cli_opts *o,
                const char *out_path)
{
    toile_model *model = calloc(1, sizeof *model);
    toile_verdict *verdicts = calloc(n + 2, sizeof *verdicts);
    char *report = NULL;
    size_t rlen = 0, n_out = 0;
    int code = CLI_EXIT_OK;

    if (model == NULL || verdicts == NULL) {
        free(model);
        free(verdicts);
        fprintf(stderr, "toile: memoire insuffisante\n");
        return CLI_EXIT_USAGE;
    }
    toile_model_init(model);
    if (n > 0)
        (void)toile_cmdproc_run(cmds, n, model, verdicts, n + 2, &n_out);
    code = cli_make_report(o, model, verdicts, n_out, o->fixture, &report, &rlen);
    if (code == CLI_EXIT_OK)
        code = cli_write_out(out_path, report, rlen);
    if (code == CLI_EXIT_OK && o->expect != NULL)
        code = cli_check_expect(o->expect, report, rlen);
    if (code == CLI_EXIT_OK &&
        toile_verdict_count_severity(verdicts, n_out, "error") > 0)
        code = CLI_EXIT_ERROR;
    free(report);
    free(verdicts);
    free(model);
    return code;
}
/* selftest : creation de ressource et hash d'etat non nul. */
static int cli_st_model(toile_model *m)
{
    if (toile_resource_create(m, 1, TOILE_FMT_B8G8R8A8_UNORM, 64, 32) !=
        TOILE_OK) {
        fprintf(stderr, "selftest: creation de ressource refusee\n");
        return 1;
    }
    if (toile_model_state_hash(m) == 0) {
        fprintf(stderr, "selftest: hash d'etat nul\n");
        return 1;
    }
    return 0;
}

/* selftest : aller-retour encode/decode d'une commande create_2d. */
static int cli_st_roundtrip(const toile_cmd *cmd)
{
    uint8_t buf[TOILE_VG_MAX_CMD_LEN];
    toile_cmd back;
    size_t written = 0;

    memset(&back, 0, sizeof back);
    if (toile_vg_encode(cmd, buf, sizeof buf, &written) != 0 || written == 0) {
        fprintf(stderr, "selftest: encode echoue\n");
        return 1;
    }
    if (toile_vg_decode(buf, written, &back) != 0) {
        fprintf(stderr, "selftest: decode echoue\n");
        return 1;
    }
    if (back.type != cmd->type ||
        back.u.create_2d.resource_id != cmd->u.create_2d.resource_id ||
        back.u.create_2d.width != cmd->u.create_2d.width ||
        back.u.create_2d.height != cmd->u.create_2d.height) {
        fprintf(stderr, "selftest: aller-retour encode/decode divergent\n");
        return 1;
    }
    return 0;
}

/* selftest : les trois formats de rapport sont non vides. */
static int cli_st_reports(const toile_model *m)
{
    const char *fmts[3] = { "text", "json", "md" };
    toile_verdict v[1];
    char rbuf[8192];
    size_t written = 0, i;

    memset(v, 0, sizeof v);
    for (i = 0; i < 3; i++) {
        written = 0;
        if (toile_report(fmts[i], m, v, 0, "selftest", rbuf, sizeof rbuf,
                          &written) != 0 || written == 0) {
            fprintf(stderr, "selftest: rapport %s vide\n", fmts[i]);
            return 1;
        }
    }
    return 0;
}

/* selftest : le processeur de commandes produit des verdicts. */
static int cli_st_cmdproc(const toile_cmd *cmd, toile_model *m)
{
    toile_verdict v[8];
    toile_cmd seq[2];
    size_t n_out = 0;

    memset(v, 0, sizeof v);
    memset(seq, 0, sizeof seq);
    seq[0].type = TOILE_VG_CMD_GET_DISPLAY_INFO;
    seq[1] = *cmd;
    (void)toile_cmdproc_run(seq, 2, m, v, 8, &n_out);
    if (n_out == 0) {
        fprintf(stderr, "selftest: aucun verdict produit\n");
        return 1;
    }
    return 0;
}

/* selftest : rapports non vides, hash non nul, aller-retour encode/decode. */
int cli_cmd_selftest(void)
{
    toile_model m;
    toile_cmd cmd;
    int failures = 0;

    memset(&cmd, 0, sizeof cmd);
    cmd.type = TOILE_VG_CMD_RESOURCE_CREATE_2D;
    cmd.u.create_2d.resource_id = 7;
    cmd.u.create_2d.format = TOILE_FMT_B8G8R8A8_UNORM;
    cmd.u.create_2d.width = 1280;
    cmd.u.create_2d.height = 800;
    toile_model_init(&m);
    failures += cli_st_model(&m);
    failures += cli_st_roundtrip(&cmd);
    failures += cli_st_reports(&m);
    failures += cli_st_cmdproc(&cmd, &m);
    if (failures != 0) {
        fprintf(stderr, "selftest: %d echec(s)\n", failures);
        return CLI_EXIT_ERROR;
    }
    printf("selftest: ok\n");
    return CLI_EXIT_OK;
}
