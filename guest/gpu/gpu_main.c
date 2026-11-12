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
 *
 * Phase-1 evidence (essai « le pilote invité démarre dans l'invité ») :
 * every key step is announced on the serial console under a stable
 * "TOILE: " prefix, so the probe script can grep one line:
 *   TOILE: virtio-gpu trouve (1af4/1050) sur Bxxx:Dxx:Fxx
 *   TOILE: /dev/gpu0 pret
 *   TOILE: ECHEC <raison>          (on any failure, never a silent exit)
 */
#include "gpu_priv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <sys/neutrino.h>
#include <unistd.h>

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
    o->mire = false;
    o->info_only = false;
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
    fflush(stdout);
}

/* Fatal-signal handler: one clear line with the faulting address on the
 * console before dying, so a hard fault is never a silent exit. */
static void gpu_on_fatal(int sig, siginfo_t *si, void *ctx)
{
    char b[128];
    void *addr = (si != NULL && si->si_code > 0) ? si->si_addr : NULL;
    int n = snprintf(b, sizeof(b),
                     "TOILE: signal fatal %d addr=%p (SIGSEGV=%d)\n",
                     sig, addr, SIGSEGV);
    if (n > 0)
        (void)write(2, b, (size_t)n);
    (void)ctx;
    signal(sig, SIG_DFL);
    raise(sig);
}

static void gpu_install_fatal_handlers(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = gpu_on_fatal;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
}

/* Bring-up sequence: transport init -> framebuffer -> scanout; then either
 * print the info and exit (--info), or attach the resmgr and serve. */
static int gpu_bring_up(gpu_ctx *g, const gpu_opts *o)
{
    int rc = gpu_pci_init(g, o->allow_msix);
    if (rc != TOILE_OK) {
        gpu_announce("ECHEC init transport (%s)", gpu_rc_reason(rc));
        return EXIT_FAILURE;
    }
    rc = gpu_fb_alloc(g, g->width, g->height, g->format);
    if (rc == TOILE_OK)
        rc = gpu_scanout_create(g);
    if (rc != TOILE_OK) {
        gpu_announce("ECHEC scanout (%s)", gpu_rc_reason(rc));
        return EXIT_FAILURE;
    }
    if (o->mire) {
        gpu_paint_mire(&g->fb, g->width, g->height, g->format);
        (void)gpu_present(g, (toile_rect){0u, 0u, 0u, 0u});
        gpu_log(g, "mire painted and presented");
    }
    gpu_print_info(g);
    if (o->info_only)
        return EXIT_SUCCESS;
    if (gpu_resmgr_attach(g) != 0) {
        gpu_announce("ECHEC resmgr /dev/gpu0");
        return EXIT_FAILURE;
    }
    gpu_announce("/dev/gpu0 pret");
    gpu_resmgr_loop(); /* does not return */
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    /* The context embeds the portable core model (~35 KB of resource
     * tables).  QNX processes launched from a startup script inherit a
     * small main-thread stack: keep the context out of the stack (bss). */
    static gpu_ctx g;
    gpu_opts o;
    memset(&g, 0, sizeof(g));
    gpu_install_fatal_handlers();
    if (gpu_parse_opts(argc, argv, &g, &o) != 0)
        return EXIT_FAILURE;

    /* Device drivers map BARs and DMA rings: enable I/O privilege up front
     * so that mmap_device_memory()/MAP_PHYS succeed later. */
    if (ThreadCtl(_NTO_TCTL_IO, NULL) == -1) {
        gpu_announce("ECHEC ThreadCtl(_NTO_TCTL_IO)");
        return EXIT_FAILURE;
    }

    if (gpu_pci_report(&g) != 0)
        return EXIT_FAILURE;
    return gpu_bring_up(&g, &o);
}
