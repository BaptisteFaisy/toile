/* SPDX-License-Identifier: MIT */
/*
 * Toile — plasma client for /dev/gpu0 (the live vitrine's moving picture).
 *
 * Same path as any application: open the node, ask for the framebuffer
 * descriptor, map it, paint, present.  Nothing is painted by the driver, so
 * what the emulator shows is what a guest application produced.
 */
#include "toile_anim.h"
#include "toile_gpu_client.h"

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#define ANIM_TARGET_FPS 30u
#define ANIM_OPEN_TRIES 40
#define ANIM_OPEN_DELAY_NS 250000000L
#define ANIM_REPORT_MS 5000u

static uint32_t now_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0u;
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u +
                      (uint64_t)ts.tv_nsec / 1000000u);
}

static void wait_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

/* The driver may still be publishing the node: retry briefly. */
static int anim_open(void)
{
    for (int i = 0; i < ANIM_OPEN_TRIES; i++) {
        int fd = toile_gpu_open(NULL);
        if (fd >= 0)
            return fd;
        wait_ms((uint32_t)(ANIM_OPEN_DELAY_NS / 1000000L));
    }
    return -1;
}

int main(void)
{
    int fd = anim_open();
    if (fd < 0) {
        printf("ANIM: ECHEC open /dev/gpu0\n");
        return 1;
    }
    printf("ANIM: /dev/gpu0 ouvert (fd=%d)\n", fd);
    toile_gpu_info_t info;
    toile_gpu_fb_t fb;
    if (toile_gpu_info(fd, &info) != 0 || toile_gpu_fb(fd, &fb) != 0) {
        printf("ANIM: ECHEC devctl INFO/FB\n");
        return 1;
    }
    void *px = toile_gpu_mmap(fd, &fb, NULL);
    if (px == NULL) {
        printf("ANIM: ECHEC mmap paddr=0x%llx bytes=%llu\n",
               (unsigned long long)fb.paddr, (unsigned long long)fb.bytes);
        return 1;
    }
    printf("ANIM: framebuffer mappe %ux%u stride=%u fmt=%u, plasma a %u im/s\n",
           (unsigned)fb.width, (unsigned)fb.height, (unsigned)fb.stride,
           (unsigned)fb.format, (unsigned)ANIM_TARGET_FPS);
    fflush(stdout);

    uint32_t t0 = now_ms();
    uint32_t frames = 0u;
    uint32_t last = t0;
    uint32_t present_err = 0u;
    for (;;) {
        uint32_t t = now_ms() - t0;
        toile_anim_frame(px, fb.stride, fb.width, fb.height, fb.format, t,
                         frames);
        if (toile_gpu_present(fd, NULL) != 0)
            present_err++;
        frames++;
        uint32_t now = now_ms();
        if (now - last >= ANIM_REPORT_MS) {
            printf("ANIM: %u images, %.1f im/s, present_ko=%u, t=%u ms\n",
                   frames, (double)frames * 1000.0 / (double)(now - t0),
                   present_err, now - t0);
            fflush(stdout);
            last = now;
        }
        uint32_t due = t0 + (frames * 1000u) / ANIM_TARGET_FPS;
        if (due > now)
            wait_ms(due - now);
    }
}