/* SPDX-License-Identifier: MIT */
/*
 * Toile — virtio-input minimal (clavier), relié à la vitrine.
 *
 * Le pilote lit les événements du périphérique virtio-input (file eventq) et
 * traduit les touches en état d'animation partagé, via le devctl
 * DCMD_TOILE_GPU_ANIM_SET de /dev/gpu0 :
 *
 *   1 / 2 / 3   palette 0 / 1 / 2          space   pause / reprise
 *   p           palette suivante           - / +   vitesse -1 / +1
 *
 * C'est le chemin d'entrée complet : clavier QEMU (sendkey par la vitrine) →
 * virtio-input → ce pilote → état partagé → les clients peintres.
 *
 * Mécanique (OASIS virtio 1.x, §5.8 « Input Device », structure standard
 * virtio_input_event : le16 type, le16 code, le32 value = 8 octets) :
 *   - file eventq (index 0) : le pilote y pousse des tampons VIDES que le
 *     périphérique remplit avec les événements ;
 *   - type 1 (EV_KEY), value 1 = enfoncé, 0 = relâché, 2 = répétition.
 */
#include "toile_gpu_client.h"
#include "toile_transport.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <sys/neutrino.h> /* ThreadCtl(_NTO_TCTL_IO) */

#define VIRTIO_INPUT_ID      18u
#define VIRTIO_INPUT_PCI_DID 0x1052u /* 0x1040 + 18 */
#define IN_EVENT_LEN 8u

#define IN_EV_KEY 0x01u

/* Codes Linux (publics) des touches utilisées par la vitrine. */
#define KEY_1 2u
#define KEY_2 3u
#define KEY_3 4u
#define KEY_P 25u
#define KEY_SPACE 57u
#define KEY_MINUS 12u
#define KEY_EQUAL 13u

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wait_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)(ms % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

/* /dev/gpu0 est publié par le pilote : on réessaie quelques secondes. */
static int gpu_open_wait(void)
{
    for (int i = 0; i < 60; i++) {
        int fd = toile_gpu_open(NULL);
        if (fd >= 0)
            return fd;
        wait_ms(250u);
    }
    return -1;
}

static void apply_key(int fd, uint16_t code)
{
    toile_anim_cfg_t cfg;
    if (toile_gpu_anim_get(fd, &cfg) != 0)
        return;
    switch (code) {
    case KEY_1:
    case KEY_2:
    case KEY_3:
        cfg.palette = (uint32_t)(code - KEY_1);
        break;
    case KEY_P:
        cfg.palette = (cfg.palette + 1u) % TOILE_ANIM_PALETTES;
        break;
    case KEY_SPACE:
        cfg.paused = cfg.paused ? 0u : 1u;
        break;
    case KEY_MINUS:
        cfg.speed = cfg.speed > 1u ? cfg.speed - 1u : 1u;
        break;
    case KEY_EQUAL:
        cfg.speed = cfg.speed < 8u ? cfg.speed + 1u : 8u;
        break;
    default:
        return;
    }
    if (toile_gpu_anim_set(fd, &cfg) == 0)
        printf("INPUT: touche %u -> palette=%u speed=%u paused=%u\n",
               (unsigned)code, (unsigned)cfg.palette, (unsigned)cfg.speed,
               (unsigned)cfg.paused);
    else
        printf("INPUT: ECHEC devctl ANIM_SET (touche %u)\n", (unsigned)code);
    fflush(stdout);
}

/* Copy the last polled event out of the transport pool. */
static int queue_one_event(toile_transport *tr, uint8_t *slot)
{
    memset(slot, 0, IN_EVENT_LEN);
    return toile_tr_push(tr, 0u, NULL, 0u, slot, IN_EVENT_LEN, NULL);
}

static void input_event_loop(toile_transport *tr, int fd)
{
    uint8_t slots[4][IN_EVENT_LEN];
    for (unsigned i = 0; i < 4u; i++)
        (void)queue_one_event(tr, slots[i]);
    (void)toile_tr_kick(tr, 0u);
    printf("INPUT: eventq ouvert, 4 tampons en attente\n");
    fflush(stdout);
    for (;;) {
        void *cookie = NULL;
        size_t len = 0;
        int rc = toile_tr_poll(tr, 0u, &cookie, &len);
        if (rc == TOILE_TR_EAGAIN) {
            (void)queue_one_event(tr, slots[0]);
            (void)toile_tr_kick(tr, 0u);
            continue;
        }
        if (rc != TOILE_TR_OK)
            break;
        const uint8_t *ev = (const uint8_t *)cookie;
        uint8_t fallback[IN_EVENT_LEN];
        if (ev == NULL || len < IN_EVENT_LEN) {
            memcpy(fallback, slots[0], IN_EVENT_LEN);
            ev = fallback;
        }
        uint16_t type = rd_le16(ev);
        uint16_t code = rd_le16(ev + 2);
        uint32_t value = rd_le32(ev + 4);
        if (type == IN_EV_KEY && value == 1u)
            apply_key(fd, code);
        /* Rendre le tampon au périphérique pour l'événement suivant. */
        (void)queue_one_event(tr, slots[0]);
        (void)toile_tr_kick(tr, 0u);
    }
}

int main(void)
{
    printf("INPUT: virtio-input (cle 1/2/3, p, space, -/+)\n");
    /* Le transport mappe les BAR du peripherique : privilege I/O requis,
     * exactement comme dans le pilote (guest/gpu/gpu_main.c). */
    if (ThreadCtl(_NTO_TCTL_IO, NULL) == -1) {
        printf("INPUT: ECHEC ThreadCtl(_NTO_TCTL_IO)\n");
        return 1;
    }
    int fd = gpu_open_wait();
    if (fd < 0) {
        printf("INPUT: ECHEC /dev/gpu0 absent\n");
        return 1;
    }
    printf("INPUT: /dev/gpu0 ouvert (fd=%d), recherche virtio-input...\n", fd);
    fflush(stdout);
    toile_transport *tr = toile_tr_open_pci(VIRTIO_INPUT_ID, true);
    printf("INPUT: open_pci -> %p\n", (void *)tr);
    fflush(stdout);
    if (tr == NULL) {
        printf("INPUT: aucun peripherique virtio-input sur le bus PCI "
               "(ajouter -device virtio-keyboard-pci cote QEMU)\n");
        return 1;
    }
    if (toile_tr_init(tr, 0u) != TOILE_TR_OK ||
        toile_tr_queue_open(tr, 0u, 0u) != TOILE_TR_OK ||
        toile_tr_ready(tr) != TOILE_TR_OK) {
        printf("INPUT: ECHEC initialisation du peripherique\n");
        toile_tr_close(tr);
        return 1;
    }
    input_event_loop(tr, fd);
    toile_tr_close(tr);
    return 0;
}