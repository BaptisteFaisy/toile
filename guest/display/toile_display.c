/* SPDX-License-Identifier: MIT */
/*
 * Toile — S7 demo: one framebuffer, one head, static test pattern.
 *
 * Three paths:
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
 *             §12 (decision D18) when Screen cannot host a third-party
 *             driver.
 *
 *   --wfd     WFD path (E2): a third-party display driver for Screen in
 *             QNX 8.0 is an OpenWF Display (WFD) driver, not an
 *             « io-display » one.  This path opens a WFD session and
 *             binds the Toile pipeline.  E2: it cannot run on this
 *             machine — wfd-server, libWFDplatform and libwfdcfg.so are
 *             absent from the installed SDP, so wfdEnumerateDevices()
 *             returns 0 devices.  The failure is reported, not hidden.
 *
 * IMPORTANT : this file is NOT a QNX display driver.  It is a demo
 * client.  The display-driver contract itself lives in
 * guest/display/toile_dispskel.c, against the real WFD/Wfdcfg API.
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
            "usage: %s [--screen|--fb|--wfd] [--width N] [--height N] [--e2]\n",
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

/* --- chemin WFD (OpenWF Display) ------------------------------------------ */
/* E2 : ouvre une session WFD et lie le pipeline Toile.
 * Sur cette machine, wfdEnumerateDevices() rend 0 appareil (ni
 * wfd-server, ni libWFDplatform dans le SDP) : l'echec est signale. */
static int run_wfd_mire(int width, int height)
{
    toile_dispskel_t s;
    int rc;

    if (toile_dispskel_open(&s) != 0) {
        fprintf(stderr,
                "toile-display: aucune session WFD (wfd-server absent du SDP)\n");
        fprintf(stderr, "toile-display: E2 — wfd-server, libWFDplatform et "
                        "libwfdcfg.so manquent ; repli : --fb\n");
        return 1;
    }
    printf("toile-display: session WFD ouverte (device %d, %d port(s))\n",
           (int)s.device_id, (int)s.port_count);
    rc = toile_dispskel_bind_pipeline(&s, 0, (WFDint)width, (WFDint)height);
    if (rc != 0)
        fprintf(stderr, "toile-display: bind du pipeline WFD a échoué\n");
    else
        printf("toile-display: pipeline WFD lié (commit effectué)\n");
    toile_dispskel_close(&s);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    int width = DEMO_DEFAULT_W;
    int height = DEMO_DEFAULT_H;
    int mode = 0; /* 0 = screen, 1 = fb, 2 = wfd */
    int show_e2 = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screen") == 0)
            mode = 0;
        else if (strcmp(argv[i], "--fb") == 0)
            mode = 1;
        else if (strcmp(argv[i], "--wfd") == 0)
            mode = 2;
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
    if (mode == 1)
        return run_fb_mire(width, height) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    if (mode == 2)
        return run_wfd_mire(width, height) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    return run_screen_mire(width, height) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
