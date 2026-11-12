/* SPDX-License-Identifier: MIT */
/* bench-model : débit du cœur portable (essai E5). Mesure le débit du MODÈLE
 * (cœur C, API publique gelée CONTRACT.md §10) sur une séquence virtio-gpu
 * 2D réaliste 1920x1080 : create_2d, attach_backing, set_scanout,
 * transfer_to_host_2d (surface pleine), flush. Pureté §2 : le cœur n'est
 * PAS modifié, reste sans horloge/allocation/fichier ; l'horloge vit ici.
 * Le modèle ne copie AUCUN pixel, il COMPTE les octets (invariant I6) : les
 * débits affichés sont des débits de COMPTABILISATION, pas des débits DMA. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "toile/cmdproc.h"
#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/vgproto.h"
/* Surface de référence E5 : 1920x1080, 4 octets par pixel. */
#define FRAME_W      1920u
#define FRAME_H      1080u
#define FRAME_BPP    4u
#define FRAME_BYTES  ((uint64_t)FRAME_W * (uint64_t)FRAME_H * (uint64_t)FRAME_BPP)
/* Backing synthétique : 32 entrées de 256 Kio = 8 388 608 octets >=
 * 8 294 400 (surface 1080p). 32 = plafond TOILE_MAX_PAGES du modèle (il ne
 * peut pas représenter un backing invité réel de 2025 pages de 4 Kio — voir
 * docs/TRIALS-E5.md, limites de modélisation). */
#define BACKING_N   32u
#define BACKING_LEN 262144u
#define RES_ID     1u
#define SCANOUT_ID 0u
#define FULL_SEQ   5u    /* create_2d, attach, scanout, xfer, flush */
#define LOOP_SEQ   2u    /* xfer, flush */
#define DEFAULT_ITERS 20000u
#define REPS 5u
/* Horloge monotone, en secondes (le cœur, lui, reste sans horloge). */
static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
/* Entrées de backing de la surface modélisée (adresses synthétiques). */
static void build_pages(toile_page *pages)
{
    for (unsigned i = 0; i < BACKING_N; i++) {
        pages[i].addr = 0x10000000ull + (uint64_t)i * BACKING_LEN;
        pages[i].length = BACKING_LEN;
    }
}
/* transfer_to_host_2d sur toute la surface. */
static void cmd_xfer(toile_cmd *c)
{
    memset(c, 0, sizeof *c);
    c->type = TOILE_VG_CMD_TRANSFER_TO_HOST_2D;
    c->u.xfer.resource_id = RES_ID;
    c->u.xfer.rect.w = FRAME_W;
    c->u.xfer.rect.h = FRAME_H;
}
/* resource_flush sur toute la surface. */
static void cmd_flush(toile_cmd *c)
{
    memset(c, 0, sizeof *c);
    c->type = TOILE_VG_CMD_RESOURCE_FLUSH;
    c->u.flush.resource_id = RES_ID;
    c->u.flush.rect.w = FRAME_W;
    c->u.flush.rect.h = FRAME_H;
}
/* Cycle de vie complet : create 2D, attach backing, set scanout, xfer,
 * flush. */
static void build_full_sequence(toile_cmd *cmds, const toile_page *pages)
{
    memset(cmds, 0, sizeof(toile_cmd) * FULL_SEQ);
    cmds[0].type = TOILE_VG_CMD_RESOURCE_CREATE_2D;
    cmds[0].u.create_2d.resource_id = RES_ID;
    cmds[0].u.create_2d.format = (uint32_t)TOILE_FMT_B8G8R8A8_UNORM;
    cmds[0].u.create_2d.width = FRAME_W;
    cmds[0].u.create_2d.height = FRAME_H;
    cmds[1].type = TOILE_VG_CMD_RESOURCE_ATTACH_BACKING;
    cmds[1].u.backing.resource_id = RES_ID;
    cmds[1].u.backing.nr_entries = BACKING_N;
    memcpy(cmds[1].u.backing.pages, pages, sizeof(toile_page) * BACKING_N);
    cmds[2].type = TOILE_VG_CMD_SET_SCANOUT;
    cmds[2].u.scanout.scanout_id = SCANOUT_ID;
    cmds[2].u.scanout.resource_id = RES_ID;
    cmds[2].u.scanout.rect.w = FRAME_W;
    cmds[2].u.scanout.rect.h = FRAME_H;
    cmd_xfer(&cmds[3]);
    cmd_flush(&cmds[4]);
}
/* --- résultats des scénarios ------------------------------------------- */
typedef struct {
    double elapsed;   /* secondes (meilleure répétition) */
    double init_sec;  /* secondes passées dans toile_model_init */
    uint64_t cmds;    /* commandes exécutées */
    uint64_t bytes;   /* octets comptés (invariant I6) */
    unsigned errors;  /* commandes refusées (doit rester 0) */
} bench_result;
/* Une répétition du cycle de vie complet : 5 commandes par image. */
static bench_result lifecycle_once(unsigned iters)
{
    bench_result r;
    toile_model model;
    toile_verdict verdicts[FULL_SEQ + 1];
    toile_cmd cmds[FULL_SEQ];
    toile_page pages[BACKING_N];
    size_t n_out = 0;
    double t0 = now_sec();
    build_pages(pages);
    build_full_sequence(cmds, pages);
    r.cmds = 0; r.bytes = 0; r.errors = 0; r.init_sec = 0.0;
    for (unsigned i = 0; i < iters; i++) {
        double a = now_sec();
        int rc = 0;
        toile_model_init(&model);
        r.init_sec += now_sec() - a;
        rc = toile_cmdproc_run(cmds, FULL_SEQ, &model, verdicts,
                               FULL_SEQ + 1, &n_out);
        if (rc != TOILE_OK)
            r.errors++;
        r.bytes += model.bytes_transferred;
        r.cmds += FULL_SEQ;
    }
    r.elapsed = now_sec() - t0;
    return r;
}
/* Une répétition du flux d'affichage : xfer + flush par image. */
static bench_result steady_once(unsigned iters)
{
    bench_result r;
    toile_model model;
    toile_verdict setup[FULL_SEQ + 1];
    toile_verdict verdicts[LOOP_SEQ + 1];
    toile_cmd cmds[FULL_SEQ];
    toile_cmd loop[LOOP_SEQ];
    toile_page pages[BACKING_N];
    size_t n_out = 0;
    double t0;
    build_pages(pages);
    build_full_sequence(cmds, pages);
    cmd_xfer(&loop[0]);
    cmd_flush(&loop[1]);
    toile_model_init(&model);
    r.errors = 0; r.init_sec = 0.0;
    if (toile_cmdproc_run(cmds, FULL_SEQ, &model, setup, FULL_SEQ + 1,
                           &n_out) != TOILE_OK)
        r.errors = 1;
    r.cmds = FULL_SEQ;
    t0 = now_sec();
    for (unsigned i = 0; i < iters; i++) {
        if (toile_cmdproc_run(loop, LOOP_SEQ, &model, verdicts,
                              LOOP_SEQ + 1, &n_out) != TOILE_OK)
            r.errors++;
        r.cmds += LOOP_SEQ;
    }
    r.elapsed = now_sec() - t0;
    r.bytes = model.bytes_transferred;
    return r;
}
/* Réinitialisation seule du modèle (pour décomposer le cycle de vie). */
static bench_result reset_once(unsigned iters)
{
    bench_result r;
    toile_model model;
    double t0 = now_sec();
    r.cmds = 0; r.bytes = 0; r.errors = 0;
    for (unsigned i = 0; i < iters; i++) {
        toile_model_init(&model);
        r.bytes += (uint64_t)(model.cmds_applied == 0u);
    }
    r.elapsed = now_sec() - t0;
    r.init_sec = r.elapsed;
    return r;
}
/* Référence hôte : copie mémoire de la taille d'une image (hors virtio). */
static bench_result memcpy_once(unsigned iters)
{
    bench_result r;
    unsigned char *src = (unsigned char *)malloc(FRAME_BYTES);
    unsigned char *dst = (unsigned char *)malloc(FRAME_BYTES);
    volatile unsigned char sink = 0;
    double t0;
    if (src == NULL || dst == NULL) {
        free(src);
        free(dst);
        r.elapsed = 0.0; r.cmds = 0; r.bytes = 0; r.errors = 1;
        r.init_sec = 0.0;
        return r;
    }
    memset(src, 0x5a, FRAME_BYTES);
    r.cmds = 0; r.bytes = 0; r.errors = 0; r.init_sec = 0.0;
    t0 = now_sec();
    for (unsigned i = 0; i < iters; i++) {
        memcpy(dst, src, FRAME_BYTES);
        sink ^= dst[0];
        r.bytes += FRAME_BYTES;
    }
    r.elapsed = now_sec() - t0;
    (void)sink;
    free(src);
    free(dst);
    return r;
}
/* Meilleure répétition sur REPS passes (bruit système minimisé). */
static bench_result best_of(bench_result (*run)(unsigned), unsigned iters)
{
    bench_result best = run(iters);
    for (unsigned rep = 1; rep < REPS; rep++) {
        bench_result cur = run(iters);
        if (cur.elapsed < best.elapsed)
            best = cur;
    }
    return best;
}
/* Empreinte d'état du modèle après une séquence complète (P3) : la même
 * séquence doit toujours donner la même empreinte. */
static uint64_t steady_hash(void)
{
    toile_model model;
    toile_verdict verdicts[FULL_SEQ + 1];
    toile_cmd cmds[FULL_SEQ];
    toile_page pages[BACKING_N];
    size_t n_out = 0;
    build_pages(pages);
    build_full_sequence(cmds, pages);
    toile_model_init(&model);
    (void)toile_cmdproc_run(cmds, FULL_SEQ, &model, verdicts, FULL_SEQ + 1,
                             &n_out);
    return toile_model_state_hash(&model);
}
/* Affichage d'un bloc de résultats. */
static void report(const char *title, bench_result r, unsigned iters)
{
    double sec = r.elapsed > 0.0 ? r.elapsed : 1e-9;
    printf("  %s\n", title);
    printf("    itérations    : %u\n", iters);
    printf("    durée totale  : %.6f s\n", r.elapsed);
    printf("    commandes     : %llu\n", (unsigned long long)r.cmds);
    printf("    commandes/s   : %.0f\n", (double)r.cmds / sec);
    printf("    Mo/s pixels   : %.2f\n", (double)r.bytes / sec / 1.0e6);
    printf("    Mpx/s         : %.2f\n", (double)(r.bytes / 4u) / sec / 1.0e6);
    printf("    erreurs       : %u\n", r.errors);
}
/* Une passe complète du banc. */
static void run_all(unsigned iters)
{
    bench_result life, steady, reset, copy;
    double sec_s, sec_c;
    printf("=== banc de débit du cœur portable (essai E5) ===\n");
    printf("itérations par scénario : %u  (meilleure de %u passes)\n",
           iters, REPS);
    printf("surface                 : %ux%u, %u octets/pixel\n",
           FRAME_W, FRAME_H, FRAME_BPP);
    printf("octets par image        : %llu (%.2f Mo)\n",
           (unsigned long long)FRAME_BYTES, (double)FRAME_BYTES / 1.0e6);
    printf("backing modèle          : %u entrées de %u octets\n\n",
           BACKING_N, BACKING_LEN);
    life = best_of(lifecycle_once, iters);
    report("[A] cycle de vie complet (create+attach+scanout+xfer+flush)",
           life, iters);
    printf("    besoin E5 (20 img/s, 1080p) : 165.89 Mo/s\n");
    printf("    marge du modèle [A]         : %.1fx\n\n",
           (life.bytes / 1.0e6 / ((life.elapsed - life.init_sec) > 0.0 ?
            life.elapsed - life.init_sec : 1e-9)) / 165.89);
    steady = best_of(steady_once, iters);
    report("[B] boucle d'affichage (xfer surface pleine + flush)",
           steady, iters);
    sec_s = steady.elapsed > 0.0 ? steady.elapsed : 1e-9;
    printf("    images/s modélisées        : %.0f\n", (double)iters / sec_s);
    printf("    marge du modèle [B] sur 20 img/s : %.1fx\n\n",
           ((double)iters / sec_s) / 20.0);
    reset = best_of(reset_once, iters);
    report("[C] réinitialisation du modèle seule", reset, iters);
    printf("    part du reset dans [A]     : %.2f%%\n\n",
           life.elapsed > 0.0 ? 100.0 * reset.elapsed / life.elapsed : 0.0);
    copy = best_of(memcpy_once, iters);
    report("[D] référence hôte : memcpy de 8.29 Mo (hors virtio-gpu)",
           copy, iters);
    sec_c = copy.elapsed > 0.0 ? copy.elapsed : 1e-9;
    printf("    Mo/s memcpy hôte           : %.2f\n",
           (double)copy.bytes / sec_c / 1.0e6);
    printf("\nempreinte d'état du modèle (P3) : %016llx\n",
           (unsigned long long)steady_hash());
    printf("NOTE : le modèle ne copie aucun pixel ; il ne fait que\n");
    printf("      COMPTER les octets (invariant I6). Les débits ci-dessus\n");
    printf("      sont des débits de COMPTABILISATION, pas des débits DMA.\n");
}
int main(int argc, char **argv)
{
    unsigned iters = DEFAULT_ITERS;
    if (argc > 1) {
        long v = strtol(argv[1], NULL, 10);
        if (v > 0 && v <= 100000000L)
            iters = (unsigned)v;
    }
    run_all(iters);
    return 0;
}