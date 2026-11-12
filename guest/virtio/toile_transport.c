/* SPDX-License-Identifier: MIT */
/*
 * Toile — thin virtio transport layer, implementation.
 *
 * Derived-work notice: this file is an adaptation layer on top of the
 * Passerelle virtio transport (SPDX Apache-2.0, same author,
 * github.com/BaptisteFaisy/passerelle).  All virtqueue, negotiation,
 * interrupt and DMA-memory mechanics come from Passerelle
 * (pgw_vio_ and pgw_vq_ families); nothing is re-implemented here.  See the
 * repository NOTICE and guest/virtio/README.md.
 */
#include "toile_transport.h"

#include "pgw/virtio.h"
#include "virtio_priv.h" /* struct pgw_vio_dev: ops, vq_alloc/vq_free */

#include "toile/vgproto.h" /* TOILE_VG_MAX_CMD_LEN */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TR_CMD_CAP   ((size_t)TOILE_VG_MAX_CMD_LEN)
#define TR_SLAB_ALIGN 4096u
#define TR_DEFAULT_RING 64u

/* --- pooled request slots ------------------------------------------------ */

typedef struct tr_slot {
    bool busy;
    void *cookie;
    void *user_resp;
    size_t user_cap;
    uint8_t *cmd_dma;
    uint64_t cmd_pa;
    uint8_t *resp_dma;
    uint64_t resp_pa;
} tr_slot;

typedef struct tr_queue {
    pgw_virtq *vq;
    uint8_t *slab;
    uint64_t slab_pa;
    size_t slab_bytes;
    tr_slot slots[TOILE_TR_POOL];
} tr_queue;

struct toile_transport {
    pgw_vio_dev *dev;
    tr_queue q[TOILE_VQ_COUNT];
};

static size_t tr_slot_bytes(void)
{
    return TR_CMD_CAP + TOILE_TR_RESP_CAP;
}

static toile_tr_err tr_map(pgw_err e)
{
    switch (e) {
    case PGW_OK:        return TOILE_TR_OK;
    case PGW_EAGAIN:    return TOILE_TR_EAGAIN;
    case PGW_ENOMEM:    return TOILE_TR_ENOMEM;
    case PGW_EINVAL:    return TOILE_TR_EINVAL;
    case PGW_ETIMEDOUT: return TOILE_TR_ETIME;
    case PGW_ENXIO:     return TOILE_TR_ENODEV;
    case PGW_ENOENT:    return TOILE_TR_ENODEV;
    default:            return TOILE_TR_EIO;
    }
}

static int64_t tr_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* --- little-endian field helpers (mock device only) ---------------------- */

static uint32_t tr_rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void tr_wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void tr_wr_le64(uint8_t *p, uint64_t v)
{
    tr_wr_le32(p, (uint32_t)v);
    tr_wr_le32(p + 4, (uint32_t)(v >> 32));
}

/* Minimal in-memory virtio-gpu device: acknowledges every command with
 * RESP_OK_NODATA (or RESP_OK_DISPLAY_INFO for GET_DISPLAY_INFO), echoing
 * the fence id.  Enough to exercise the transport on a PC. */
static uint32_t tr_mock_handler(const pgw_vio_sg *in_sg, size_t n_in,
                                const pgw_vio_sg *out_sg, size_t n_out,
                                void *user)
{
    (void)user;
    if (n_in == 0 || n_out == 0 || in_sg[0].len < TOILE_VG_HDR_LEN ||
        out_sg[0].len < TOILE_VG_HDR_LEN)
        return 0;
    const uint8_t *req = (const uint8_t *)in_sg[0].addr;
    uint8_t *resp = (uint8_t *)out_sg[0].addr;
    uint32_t type = tr_rd_le32(req);
    uint64_t fence = 0;
    memcpy(&fence, req + 8, sizeof(fence)); /* host order both sides */
    memset(resp, 0, TOILE_VG_HDR_LEN);
    tr_wr_le32(resp, type == TOILE_VG_CMD_GET_DISPLAY_INFO
                           ? TOILE_VG_RESP_OK_DISPLAY_INFO
                           : TOILE_VG_RESP_OK_NODATA);
    tr_wr_le32(resp + 4, 0); /* flags */
    tr_wr_le64(resp + 8, fence);
    return TOILE_VG_HDR_LEN;
}

/* --- open / close --------------------------------------------------------- */

static toile_transport *tr_new(pgw_vio_dev *dev)
{
    toile_transport *tr = calloc(1u, sizeof(*tr));
    if (tr == NULL) {
        pgw_vio_destroy(dev);
        return NULL;
    }
    tr->dev = dev;
    return tr;
}

toile_transport *toile_tr_open_pci(uint16_t virtio_device_id, bool allow_msix)
{
    pgw_vio_dev *dev = pgw_vio_pci_open(virtio_device_id, NULL, allow_msix);
    if (dev == NULL)
        return NULL;
    return tr_new(dev);
}

toile_transport *toile_tr_open_mock(uint16_t num_queues)
{
    pgw_vio_dev *dev = pgw_vio_mock_open(TOILE_VIRTIO_ID_GPU, NULL,
                                         num_queues, PGW_VIO_F_VERSION_1,
                                         tr_mock_handler, NULL);
    if (dev == NULL)
        return NULL;
    return tr_new(dev);
}

void toile_tr_close(toile_transport *tr)
{
    if (tr == NULL)
        return;
    for (uint16_t i = 0; i < TOILE_VQ_COUNT; i++)
        toile_tr_queue_close(tr, i);
    pgw_vio_destroy(tr->dev);
    free(tr);
}

/* --- init / introspection ------------------------------------------------- */

/* Virtio 1.2 §3.1 status sequence, driven HERE and not through
 * pgw_vio_init_device().  That helper starts with pgw_vio_reset(), and on
 * QNX the reset hook the backend installs (qnx_pci_teardown, wired as
 * dev->irq_release) DETACHES the PCI device and unmaps its BARs: the
 * device is dead before the first feature word can be read.  Driving the
 * status byte directly keeps the device attached for the whole session;
 * the detach happens once, in pgw_vio_destroy(). */
static int tr_status(pgw_vio_dev *dev, uint8_t want, uint8_t bit)
{
    dev->ops->set_status(dev, want);
    if ((dev->ops->get_status(dev) & bit) == 0u)
        return TOILE_TR_EIO;
    return TOILE_TR_OK;
}

int toile_tr_init(toile_transport *tr, uint64_t driver_features)
{
    if (tr == NULL)
        return TOILE_TR_EINVAL;
    pgw_vio_dev *dev = tr->dev;
    uint64_t want = driver_features | PGW_VIO_F_VERSION_1;

    int rc = tr_status(dev, PGW_VIO_STATUS_ACK, PGW_VIO_STATUS_ACK);
    if (rc != TOILE_TR_OK)
        return rc;
    rc = tr_status(dev, (uint8_t)(PGW_VIO_STATUS_ACK | PGW_VIO_STATUS_DRIVER),
                   PGW_VIO_STATUS_DRIVER);
    if (rc != TOILE_TR_OK)
        return rc;

    uint64_t host = dev->ops->get_features(dev);
    if ((want & ~host) != 0u) {
        dev->ops->set_status(dev, (uint8_t)(PGW_VIO_STATUS_ACK |
                                            PGW_VIO_STATUS_DRIVER |
                                            PGW_VIO_STATUS_FEATURES |
                                            PGW_VIO_STATUS_FAILED));
        return TOILE_TR_EIO;
    }
    uint64_t negotiated = want & host;
    dev->ops->set_features(dev, negotiated);
    rc = tr_status(dev, (uint8_t)(PGW_VIO_STATUS_ACK | PGW_VIO_STATUS_DRIVER |
                                  PGW_VIO_STATUS_FEATURES),
                   PGW_VIO_STATUS_FEATURES);
    if (rc != TOILE_TR_OK)
        return rc;
    pgw_vio_info *info = pgw_vio_info_mut(dev);
    if (info != NULL)
        info->negotiated_features = negotiated;
    return TOILE_TR_OK;
}

int toile_tr_ready(toile_transport *tr)
{
    if (tr == NULL)
        return TOILE_TR_EINVAL;
    pgw_vio_dev *dev = tr->dev;
    return tr_status(dev, (uint8_t)(PGW_VIO_STATUS_ACK | PGW_VIO_STATUS_DRIVER |
                                     PGW_VIO_STATUS_FEATURES |
                                     PGW_VIO_STATUS_DRIVER_OK),
                      PGW_VIO_STATUS_DRIVER_OK);
}

uint16_t toile_tr_queue_count(const toile_transport *tr)
{
    return tr == NULL ? 0u : pgw_vio_get_info(tr->dev)->num_queues;
}

bool toile_tr_msix_active(const toile_transport *tr)
{
    return tr != NULL && pgw_vio_get_info(tr->dev)->msix_active;
}

uint64_t toile_tr_features(const toile_transport *tr)
{
    return tr == NULL ? 0u : pgw_vio_get_info(tr->dev)->negotiated_features;
}

int toile_tr_read_config(toile_transport *tr, uint32_t offset,
                         void *out, size_t len)
{
    if (tr == NULL || out == NULL)
        return TOILE_TR_EINVAL;
    /* No public wrapper in pgw/virtio.h: go through the bus ops, the same
     * path the Passerelle tag readers use internally (dev.c). */
    return tr_map(tr->dev->ops->read_config(tr->dev, offset, out, len));
}

void *toile_tr_dma_alloc(toile_transport *tr, size_t bytes, size_t align,
                         uint64_t *out_pa)
{
    if (tr == NULL)
        return NULL;
    return tr->dev->vq_alloc(tr->dev, bytes, align, out_pa);
}

void toile_tr_dma_free(toile_transport *tr, void *ptr, size_t bytes)
{
    if (tr != NULL)
        tr->dev->vq_free(tr->dev, ptr, bytes);
}

/* --- queues ---------------------------------------------------------------- */

static void tr_queue_init_slots(tr_queue *q)
{
    size_t slot = tr_slot_bytes();
    for (uint32_t i = 0; i < TOILE_TR_POOL; i++) {
        tr_slot *s = &q->slots[i];
        s->busy = false;
        s->cmd_dma = q->slab + (size_t)i * slot;
        s->cmd_pa = q->slab_pa + (uint64_t)i * (uint64_t)slot;
        s->resp_dma = s->cmd_dma + TR_CMD_CAP;
        s->resp_pa = s->cmd_pa + (uint64_t)TR_CMD_CAP;
    }
}

int toile_tr_queue_open(toile_transport *tr, uint16_t qidx, uint16_t ring_size)
{
    if (tr == NULL || qidx >= TOILE_VQ_COUNT)
        return TOILE_TR_EINVAL;
    tr_queue *q = &tr->q[qidx];
    if (q->vq != NULL)
        return TOILE_TR_OK; /* idempotent */
    q->vq = pgw_vq_create(tr->dev, qidx,
                          ring_size != 0u ? ring_size : TR_DEFAULT_RING,
                          false, false);
    if (q->vq == NULL)
        return TOILE_TR_EIO;
    q->slab_bytes = tr_slot_bytes() * TOILE_TR_POOL;
    q->slab = tr->dev->vq_alloc(tr->dev, q->slab_bytes, TR_SLAB_ALIGN,
                                &q->slab_pa);
    if (q->slab == NULL) {
        pgw_vq_destroy(q->vq);
        q->vq = NULL;
        return TOILE_TR_ENOMEM;
    }
    tr_queue_init_slots(q);
    return TOILE_TR_OK;
}

void toile_tr_queue_close(toile_transport *tr, uint16_t qidx)
{
    if (tr == NULL || qidx >= TOILE_VQ_COUNT)
        return;
    tr_queue *q = &tr->q[qidx];
    if (q->slab != NULL) {
        tr->dev->vq_free(tr->dev, q->slab, q->slab_bytes);
        q->slab = NULL;
    }
    if (q->vq != NULL) {
        pgw_vq_destroy(q->vq);
        q->vq = NULL;
    }
}

/* --- requests ---------------------------------------------------------------- */

static tr_slot *tr_slot_acquire(tr_queue *q)
{
    for (uint32_t i = 0; i < TOILE_TR_POOL; i++)
        if (!q->slots[i].busy)
            return &q->slots[i];
    return NULL;
}

int toile_tr_push(toile_transport *tr, uint16_t qidx,
                  const void *cmd, size_t cmd_len,
                  void *resp, size_t resp_cap, void *cookie)
{
    if (tr == NULL || qidx >= TOILE_VQ_COUNT ||
        (cmd == NULL && cmd_len != 0) ||
        (cmd != NULL && (cmd_len == 0 || cmd_len > TR_CMD_CAP)))
        return TOILE_TR_EINVAL;
    tr_queue *q = &tr->q[qidx];
    if (q->vq == NULL)
        return TOILE_TR_ENODEV;
    tr_slot *s = tr_slot_acquire(q);
    if (s == NULL)
        return TOILE_TR_ENOMEM;
    pgw_vio_sg sg[2];
    size_t n_sg = 0;
    if (cmd != NULL) {
        memcpy(s->cmd_dma, cmd, cmd_len);
        sg[0].addr = s->cmd_dma;
        sg[0].phys = s->cmd_pa;
        sg[0].len = (uint32_t)cmd_len;
        sg[0].write = false;
        n_sg = 1;
    }
    sg[n_sg].addr = s->resp_dma;
    sg[n_sg].phys = s->resp_pa;
    sg[n_sg].len = resp_cap > TOILE_TR_RESP_CAP ? TOILE_TR_RESP_CAP
                                                : (uint32_t)resp_cap;
    sg[n_sg].write = true;
    n_sg++;
    s->cookie = cookie;
    s->user_resp = resp;
    s->user_cap = resp_cap;
    s->busy = true;
    pgw_err e = pgw_vq_push(q->vq, sg, n_sg, s);
    if (e != PGW_OK) {
        s->busy = false;
        return tr_map(e);
    }
    return TOILE_TR_OK;
}

int toile_tr_kick(toile_transport *tr, uint16_t qidx)
{
    if (tr == NULL || qidx >= TOILE_VQ_COUNT || tr->q[qidx].vq == NULL)
        return TOILE_TR_EINVAL;
    pgw_vq_kick(tr->q[qidx].vq);
    return TOILE_TR_OK;
}

int toile_tr_poll(toile_transport *tr, uint16_t qidx,
                  void **cookie, size_t *resp_len)
{
    if (tr == NULL || qidx >= TOILE_VQ_COUNT || tr->q[qidx].vq == NULL)
        return TOILE_TR_EINVAL;
    void *out_cookie = NULL;
    uint32_t written = 0;
    pgw_err e = pgw_vq_pop(tr->q[qidx].vq, &out_cookie, &written);
    if (e == PGW_EAGAIN)
        return TOILE_TR_EAGAIN;
    if (e != PGW_OK)
        return tr_map(e);
    tr_slot *s = (tr_slot *)out_cookie;
    if (s->user_resp != NULL && s->user_cap != 0) {
        size_t n = written < s->user_cap ? written : s->user_cap;
        memcpy(s->user_resp, s->resp_dma, n);
    }
    if (cookie != NULL)
        *cookie = s->cookie;
    if (resp_len != NULL)
        *resp_len = written;
    s->busy = false;
    return TOILE_TR_OK;
}

static int tr_wait_once(toile_transport *tr, int64_t deadline_ms)
{
    int64_t left = deadline_ms - tr_now_ms();
    if (left < 0)
        left = 0;
    int32_t slice = left > 1000 ? 1000 : (int32_t)left;
    pgw_err e = tr->dev->ops->wait_irq(tr->dev, slice);
    return e == PGW_ETIMEDOUT ? TOILE_TR_ETIME : TOILE_TR_OK;
}

int toile_tr_submit(toile_transport *tr, uint16_t qidx,
                    const void *cmd, size_t cmd_len,
                    void *resp, size_t resp_cap, size_t *resp_len,
                    int32_t timeout_ms)
{
    int rc = toile_tr_push(tr, qidx, cmd, cmd_len, resp, resp_cap, NULL);
    if (rc != TOILE_TR_OK)
        return rc;
    rc = toile_tr_kick(tr, qidx);
    if (rc != TOILE_TR_OK)
        return rc;
    int64_t deadline = tr_now_ms() + (timeout_ms < 0 ? 3600000 : timeout_ms);
    for (;;) {
        void *cookie = NULL;
        size_t len = 0;
        rc = toile_tr_poll(tr, qidx, &cookie, &len);
        if (rc == TOILE_TR_OK) {
            if (resp_len != NULL)
                *resp_len = len;
            return TOILE_TR_OK;
        }
        if (rc != TOILE_TR_EAGAIN)
            return rc;
        if (tr_now_ms() >= deadline)
            return TOILE_TR_ETIME; /* slot stays in flight; poll may reap */
        (void)tr_wait_once(tr, deadline);
    }
}
