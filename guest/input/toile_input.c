/* SPDX-License-Identifier: MIT */
/*
 * Toile — virtio-input minimal (clavier + souris), niveau C (C5).
 *
 * STATUT : SQUELETTE FONCTIONNEL, NON LIVRÉ AU 15/12 (décision D7 du
 * cahier des charges : « virtio-input en niveau C, pas S »).  Ce fichier
 * montre la mécanique réelle et compile ; il ne fait pas encore
 * d'injection d'événements dans Screen.
 *
 * Mécanique (spécification OASIS virtio 1.x, §5.8 « Input Device »,
 * structure standard `virtio_input_event` : le16 type, le16 code,
 * le32 value, soit 8 octets) :
 *
 *   - file eventq (index 0) : le pilote y pousse des tampons VIDES que le
 *     périphérique remplit avec les événements ;
 *   - file statusq (index 1) : le pilote y pousse un tampon d'état.
 *
 * Le transport est celui de Toile (guest/virtio), lui-même construit sur
 * Passerelle ; le type de périphérique 18 (virtio-input) a été ajouté aux
 * listes blanches par guest/virtio/toile-virtio-gpu.patch.
 */
#include "toile_transport.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VIRTIO_INPUT_ID      18u
#define VIRTIO_INPUT_PCI_DID 0x1052u /* 0x1040 + 18 */

/* struct virtio_input_event (little-endian) */
#define IN_EVENT_LEN 8u

/* Codes d'événements Linux, utilisés par virtio-input (standard public). */
#define IN_EV_SYN 0x00u
#define IN_EV_KEY 0x01u
#define IN_EV_REL 0x02u
#define IN_EV_ABS 0x03u

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static const char *event_type_name(uint16_t type)
{
    switch (type) {
    case IN_EV_SYN: return "syn";
    case IN_EV_KEY: return "key";
    case IN_EV_REL: return "rel";
    case IN_EV_ABS: return "abs";
    default:        return "?";
    }
}

/* Copy the last polled event out of the transport pool.  The transport
 * copies the device-written bytes into the buffer given to
 * toile_tr_push(); the caller owns that buffer. */
static int queue_one_event(toile_transport *tr, uint8_t *slot)
{
    memset(slot, 0, IN_EVENT_LEN);
    return toile_tr_push(tr, 0u, NULL, 0u, slot, IN_EVENT_LEN, NULL);
}

/* Boucle d'événements : repose un tampon libre, attend, décode. */
static void input_event_loop(toile_transport *tr)
{
    uint8_t slots[4][IN_EVENT_LEN];
    for (unsigned i = 0; i < 4u; i++)
        (void)queue_one_event(tr, slots[i]);
    (void)toile_tr_kick(tr, 0u);
    printf("  eventq ouvert, 4 tampons en attente - decodage :\n");
    for (;;) {
        void *cookie = NULL;
        size_t len = 0;
        int rc = toile_tr_poll(tr, 0u, &cookie, &len);
        if (rc == TOILE_TR_EAGAIN) {
            /* Aucun evenement : on repose un tampon libre et on attend. */
            (void)queue_one_event(tr, slots[0]);
            (void)toile_tr_kick(tr, 0u);
            continue;
        }
        if (rc != TOILE_TR_OK)
            break;
        /* Le transport a recopie l'evenement dans le tampon du client ;
         * on le decode (8 octets, little-endian). */
        if (len < IN_EVENT_LEN)
            continue;
        for (unsigned i = 0; i < 4u; i++) {
            uint16_t type = rd_le16(slots[i]);
            uint16_t code = rd_le16(slots[i] + 2);
            uint32_t value = rd_le32(slots[i] + 4);
            if (type == 0u && code == 0u && value == 0u)
                continue;
            printf("  evenement: type=%s code=%u value=%u\n",
                   event_type_name(type), (unsigned)code, (unsigned)value);
        }
    }
}

int main(void)
{
    printf("toile-input: squelette virtio-input (non livre au 15/12)\n");
    printf("  peripherique attendu : virtio-input type %u (DID 0x%04x)\n",
           (unsigned)VIRTIO_INPUT_ID, (unsigned)VIRTIO_INPUT_PCI_DID);
    toile_transport *tr = toile_tr_open_pci(VIRTIO_INPUT_ID, true);
    if (tr == NULL) {
        printf("  aucun peripherique virtio-input trouve sur le bus PCI\n");
        return 1;
    }
    if (toile_tr_init(tr, 0u) != TOILE_TR_OK) {
        printf("  negociation des caracteristiques impossible\n");
        toile_tr_close(tr);
        return 1;
    }
    if (toile_tr_queue_open(tr, 0u, 0u) != TOILE_TR_OK) {
        printf("  ouverture de eventq impossible\n");
        toile_tr_close(tr);
        return 1;
    }
    input_event_loop(tr);
    toile_tr_close(tr);
    return 0;
}
