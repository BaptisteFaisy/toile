/* SPDX-License-Identifier: MIT */
/*
 * Toile — S7 demo: one framebuffer, one head, static test pattern.
 *
 * Two paths, both real:
 *
 *   --screen  (default) Screen CLIENT path: a window is created in the
 *             running Screen server and the test pattern is painted into
 *             the window buffer, then posted.  Every symbol used here was
 *             read in the installed SDP:
 *               screen_create_context()          screen/screen.h:6360
 *               screen_create_window()           screen/screen.h:9782
 *               screen_set_window_property_iv()  screen/screen.h:10543
 *               screen_create_window_buffers()   screen/screen.h:9852
 *               screen_get_window_property_pv()  screen/screen.h:10167
 *               screen_get_buffer_property_pv()  screen/screen.h:6172
 *               screen_get_buffer_property_iv()  screen/screen.h:6124
 *               screen_post_window()             screen/screen.h:10359
 *               screen_destroy_window()          screen/screen.h:9947
 *               screen_destroy_context()         screen/screen.h:6378
 *               SCREEN_FORMAT_RGBX8888 = 9       screen/screen.h:3958
 *
 *   --fb      direct framebuffer path: the Toile GPU driver exposes the
 *             virtio-gpu framebuffer through /dev/gpu0 (devctl); the
 *             pattern is painted directly into it and pushed to the host.
 *             This is the documented fallback of the cahier des charges
 *             §12 (E2) when Screen cannot host a third-party driver.
 *
 * IMPORTANT : this file is NOT a QNX display driver.  The QNX 8.0
 * display-driver interface could not be verified on this machine (no
 * display-driver DDK in the installed SDP).
 * // E2: à confirmer — le contrat d'écriture d'un display driver tiers
 * // E2: à confirmer — (points d'entrée, ABI du module, découverte par le
 * // E2: à confirmer — serveur drm-*) n'est PAS documenté dans le SDP
 * // E2: à confirmer — installé : le squelette guest/display/toile_dispskel.c
 * // E2: à confirmer — liste les points ouverts, et le mode de repli livré
 * // E2: à confirmer — est le chemin /dev/gpu0 (mode --fb ci-dessous).
 */
#include <screen/screen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gpu_api.h"
#include "toile/vgproto.h" /* toile_fmt (CONTRACT.md §3) */
#include "toile_dispskel.h"
#include "toile_gpu_client.h"
#include "toile_pattern.h"

#define DEMO_DEFAULT_W 1280
#define DEMO_DEFAULT_H 800

static void usage(const char *argv0)
{
    fprintf(stderr,
            "usage: %s [--screen|--fb] [--width N] [--height N] [--e2]\n",
            argv0);
}

/* --- chemin /dev/gpu0 (sans Screen) -------------------------------------- */
static int run_fb_mire(int width, int height)
{
    /* Le chemin /dev/gpu0 utilise la géométrie négociée par le pilote
     * (GET_DISPLAY_INFO) ; width/height ne servent qu'au chemin Screen. */
    (void)width;
    (void)height;
    int fd = toile_gpu_open(TOILE_GPU_DEVPATH);
    if (fd < 0) {
        fprintf(stderr, "toile-display: %s indisponible\n", TOILE_GPU_DEVPATH);
        return 1;
    }
    toile_gpu_info_t info;
    if (toile_gpu_info(fd, &info) != 0) {
        fprintf(stderr, "toile-display: DCMD_TOILE_GPU_INFO a échoué\n");
        close(fd);
        return 1;
    }
    printf("toile-display: gpu %ux%u format=%u scanouts=%u stride=%u\n",
           (unsigned)info.width, (unsigned)info.height,
           (unsigned)info.format, (unsigned)info.num_scanouts,
           (unsigned)info.stride);
    if (toile_gpu_mire(fd) != 0) {
        fprintf(stderr, "toile-display: DCMD_TOILE_GPU_MIRE a échoué\n");
        close(fd);
        return 1;
    }
    printf("toile-display: mire peinte et présentée\n");
    close(fd);
    return 0;
}

/* --- chemin Screen (client) ----------------------------------------------- */
/* Crée la fenêtre, récupère son buffer, peint la mire et publie. */
static int screen_paint_mire(screen_context_t ctx, int width, int height)
{
    screen_window_t win = NULL;
    if (screen_create_window(&win, ctx) != 0 || win == NULL) {
        fprintf(stderr, "toile-display: screen_create_window a échoué\n");
        return 1;
    }
    int size[2] = {width, height};
    int pos[2] = {0, 0};
    int fmt = SCREEN_FORMAT_RGBX8888;
    screen_set_window_property_iv(win, SCREEN_PROPERTY_SIZE, size);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_POSITION, pos);
    screen_set_window_property_iv(win, SCREEN_PROPERTY_FORMAT, &fmt);
    if (screen_create_window_buffers(win, 1) != 0) {
        fprintf(stderr, "toile-display: screen_create_window_buffers a échoué\n");
        screen_destroy_window(win);
        return 1;
    }
    screen_buffer_t bufs[1] = {NULL};
    if (screen_get_window_property_pv(win, SCREEN_PROPERTY_BUFFERS,
                                      (void **)bufs) != 0 || bufs[0] == NULL) {
        fprintf(stderr, "toile-display: SCREEN_PROPERTY_BUFFERS indisponible\n");
        screen_destroy_window(win);
        return 1;
    }
    void *pixels = NULL;
    int stride = 0;
    screen_get_buffer_property_pv(bufs[0], SCREEN_PROPERTY_POINTER, &pixels);
    screen_get_buffer_property_iv(bufs[0], SCREEN_PROPERTY_STRIDE, &stride);
    if (pixels == NULL || stride <= 0) {
        fprintf(stderr, "toile-display: buffer sans pointeur exploitable\n");
        screen_destroy_window(win);
        return 1;
    }
    /* SCREEN_FORMAT_RGBX8888 : octets R,G,B,X -> famille RGBA de
     * CONTRACT.md §3 (TOILE_FMT_R8G8B8X8_UNORM = 134). */
    toile_pattern_paint(pixels, (uint32_t)stride, (uint32_t)width,
                        (uint32_t)height, TOILE_FMT_R8G8B8X8_UNORM);
    screen_post_window(win, bufs[0], 1, NULL, 0);
    return 0;
}

static int run_screen_mire(int width, int height)
{
    screen_context_t ctx = NULL;
    if (screen_create_context(&ctx, 0) != 0 || ctx == NULL) {
        fprintf(stderr, "toile-display: screen_create_context a échoué\n");
        return 1;
    }
    int rc = screen_paint_mire(ctx, width, height);
    if (rc != 0) {
        screen_destroy_context(ctx);
        return 1;
    }
    printf("toile-display: mire peinte dans une fenêtre Screen %dx%d\n",
           width, height);
    fflush(stdout);
    /* Keep the window alive so the server keeps showing the pattern. */
    for (;;)
        sleep(60);
}

int main(int argc, char **argv)
{
    int width = DEMO_DEFAULT_W;
    int height = DEMO_DEFAULT_H;
    int use_fb = 0;
    int show_e2 = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screen") == 0)
            use_fb = 0;
        else if (strcmp(argv[i], "--fb") == 0)
            use_fb = 1;
        else if (strcmp(argv[i], "--e2") == 0)
            show_e2 = 1;
        else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc)
            width = atoi(argv[++i]);
        else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc)
            height = atoi(argv[++i]);
        else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }
    if (show_e2) {
        toile_dispskel_report();
        return EXIT_SUCCESS;
    }
    int rc = use_fb ? run_fb_mire(width, height)
                     : run_screen_mire(width, height);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
