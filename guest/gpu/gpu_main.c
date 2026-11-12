/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver entry point (dev.gpu / "toile-gpu").
 *
 * Usage:
 *   toile-gpu [-v] [--no-msix] [--mire] [--info]
 *     -v        verbose logging on stderr
 *     --no-msix force legacy INTx interrupt wiring
 *     --mire    paint the static test pattern and present it at start
 *               (S7 demo path: visible output without Screen)
 *     --info    print the display information and exit (smoke test)
 *
 * Normal run: PCI enumeration -> transport init -> queues ->
 * GET_DISPLAY_INFO -> framebuffer -> resource/scanout -> serve
 * /dev/gpu0 (resmgr) forever.
 */
#include "gpu_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gpu_usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [-v] [--no-msix] [--mire] [--info]\n", argv0);
}

typedef struct gpu_opts {
    bool allow_msix;
    bool mire;
    bool info_only;
} gpu_opts;

static int gpu_parse_opts(int argc, char **argv, gpu_ctx *g, gpu_opts *o)
{
    o->allow_msix = true;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0)
            g->verbose = 1;
        else if (strcmp(argv[i], "--no-msix") == 0)
            o->allow_msix = false;
        else if (strcmp(argv[i], "--mire") == 0)
            o->mire = true;
        else if (strcmp(argv[i], "--info") == 0)
            o->info_only = true;
        else {
            gpu_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

static void gpu_print_info(const gpu_ctx *g)
{
    toile_gpu_info_t info;
    memset(&info, 0, sizeof(info));
    gpu_fill_info(g, &info);
    printf("toile-gpu: %ux%u fmt=%u scanouts=%u stride=%u res=%u fb=%llu\n",
           (unsigned)info.width, (unsigned)info.height,
           (unsigned)info.format, (unsigned)info.num_scanouts,
           (unsigned)info.stride, (unsigned)info.resource_id,
           (unsigned long long)info.fb_bytes);
}

int main(int argc, char **argv)
{
    gpu_ctx g;
    gpu_opts o;
    memset(&g, 0, sizeof(g));
    if (gpu_parse_opts(argc, argv, &g, &o) != 0)
        return EXIT_FAILURE;
    int rc = gpu_pci_init(&g, o.allow_msix);
    if (rc != TOILE_OK) {
        fprintf(stderr, "toile-gpu: device init failed (%d)\n", rc);
        return EXIT_FAILURE;
    }
    rc = gpu_fb_alloc(&g, g.width, g.height, g.format);
    if (rc == TOILE_OK)
        rc = gpu_scanout_create(&g);
    if (rc != TOILE_OK) {
        fprintf(stderr, "toile-gpu: scanout setup failed (%d)\n", rc);
        gpu_pci_fini(&g);
        return EXIT_FAILURE;
    }
    if (o.mire) {
        gpu_paint_mire(&g.fb, g.width, g.height, g.format);
        (void)gpu_present(&g, (toile_rect){0u, 0u, 0u, 0u});
        gpu_log(&g, "mire painted and presented");
    }
    gpu_print_info(&g);
    if (o.info_only) {
        gpu_pci_fini(&g);
        return EXIT_SUCCESS;
    }
    if (gpu_resmgr_attach(&g) != 0) {
        gpu_pci_fini(&g);
        return EXIT_FAILURE;
    }
    gpu_resmgr_loop(); /* does not return */
    return EXIT_SUCCESS;
}
