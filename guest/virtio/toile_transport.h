/* SPDX-License-Identifier: MIT */
/*
 * Toile — thin virtio transport layer for the guest GPU driver.
 *
 * This is an ADAPTATION of the Passerelle virtio transport (Apache-2.0,
 * same author — see the repository NOTICE and guest/virtio/README.md).
 * Toile does not re-implement virtqueues: every ring, negotiation and
 * interrupt operation below delegates to the pgw_vio_* / pgw_vq_* API of
 * Passerelle (virtio/include/pgw/virtio.h).  What this layer adds:
 *
 *   - virtio-gpu identity (device type 16, modern PCI DID 0x1050);
 *   - named queues controlq (0) / cursorq (1) per OASIS virtio 1.x §5.7;
 *   - a fixed request pool with DMA-capable command/response buffers
 *     (allocated through the Passerelle dev->vq_alloc hook: real
 *     physically contiguous non-cached memory on QNX, identity-mapped
 *     heap on PC/mock);
 *   - a synchronous submit helper and an async push/kick/poll trio;
 *   - a mock-backed open so the layer is testable on a PC (no QNX).
 *
 * The Passerelle PCI/MMIO discovery entry points whitelist 9P/virtiofs
 * only; the companion file toile-virtio-gpu.patch extends them to the
 * GPU device type and is applied to a build-directory copy of the
 * Passerelle sources (never to the shared tree).  See TRIALS E3.
 */
#ifndef TOILE_TRANSPORT_H
#define TOILE_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* virtio-gpu device identity (OASIS virtio 1.x §5.7, PCI modern base). */
#define TOILE_VIRTIO_ID_GPU      16u
#define TOILE_VIRTIO_GPU_PCI_DID 0x1050u /* 0x1040 + 16 */

/* virtio-gpu queue indices (OASIS virtio 1.x §5.7.2). */
#define TOILE_VQ_CONTROLQ 0u
#define TOILE_VQ_CURSORQ  1u
#define TOILE_VQ_COUNT    2u

/* Per-queue request pool: max in-flight commands before poll reaps. */
#define TOILE_TR_POOL       8u
/* Device-writable response capacity per request (display-info response
 * is 24 + 16 * 24 = 408 bytes; everything else fits in the 24-byte
 * control header). */
#define TOILE_TR_RESP_CAP   512u

/* Result codes (transport level; not the core verdict enum). */
typedef enum toile_tr_err {
    TOILE_TR_OK    = 0,
    TOILE_TR_EINVAL = 1,  /* bad argument */
    TOILE_TR_ENODEV = 2,  /* no virtio-gpu device found / not open */
    TOILE_TR_ENOMEM = 3,  /* allocation failure or pool exhausted */
    TOILE_TR_EIO    = 4,  /* transport-level failure */
    TOILE_TR_EAGAIN = 5,  /* nothing completed yet */
    TOILE_TR_ETIME  = 6   /* synchronous wait timed out */
} toile_tr_err;

typedef struct toile_transport toile_transport;

/* --- lifecycle -------------------------------------------------------- */

/* QNX only: probe the PCI bus through Passerelle libpci glue and open the
 * first virtio device of the given type (16 = virtio-gpu, 18 = virtio-input;
 * the ids must be allowed by the Passerelle whitelist, extended by
 * toile-virtio-gpu.patch).  Returns NULL when absent (logged). */
toile_transport *toile_tr_open_pci(uint16_t virtio_device_id,
                                    bool allow_msix);

/* Any host: open a mock transport with an in-memory virtio-gpu device so
 * the layer can be exercised on a PC (mirrors Passerelle VIO-10). */
toile_transport *toile_tr_open_mock(uint16_t num_queues);

/* Negotiate features and move the device to DRIVER_OK.  Pass the OR of
 * the feature bits the driver needs; transport-level VERSION_1 is added
 * automatically.  0 on success. */
int toile_tr_init(toile_transport *tr, uint64_t driver_features);

void toile_tr_close(toile_transport *tr);

/* --- introspection ---------------------------------------------------- */

uint16_t toile_tr_queue_count(const toile_transport *tr);
bool     toile_tr_msix_active(const toile_transport *tr);
uint64_t toile_tr_features(const toile_transport *tr);

/* Read the virtio-gpu device configuration space (struct virtio_gpu_config:
 * events_read, eventsClear, num_scanouts, num_capsets). */
int toile_tr_read_config(toile_transport *tr, uint32_t offset,
                         void *out, size_t len);

/* DMA-capable memory (framebuffer, backing pages): physically contiguous
 * non-cached memory on QNX with its physical address, identity-mapped heap
 * on PC/mock.  Delegates to the Passerelle dev->vq_alloc hook. */
void *toile_tr_dma_alloc(toile_transport *tr, size_t bytes, size_t align,
                         uint64_t *out_pa);
void toile_tr_dma_free(toile_transport *tr, void *ptr, size_t bytes);

/* --- queues ------------------------------------------------------------ */

/* Create the virtqueue `qidx` with `ring_size` descriptors (0 = default
 * 64, capped by the device).  0 on success. */
int toile_tr_queue_open(toile_transport *tr, uint16_t qidx, uint16_t ring_size);
void toile_tr_queue_close(toile_transport *tr, uint16_t qidx);

/* --- requests ----------------------------------------------------------- */

/* Asynchronous: copy `cmd` into a pooled DMA buffer, push the descriptor
 * pair (command readable, response writable) with `cookie` for later
 * identification.  `cmd == NULL` pushes a single device-writable buffer
 * (virtio-input event queue style).  0 on success, TOILE_TR_ENOMEM when
 * the pool is full. */
int toile_tr_push(toile_transport *tr, uint16_t qidx,
                  const void *cmd, size_t cmd_len,
                  void *resp, size_t resp_cap, void *cookie);

/* Notify the device (honours notification suppression). */
int toile_tr_kick(toile_transport *tr, uint16_t qidx);

/* Reap one completed request: *cookie receives the value passed to push,
 * *resp_len the bytes the device wrote (already copied into the resp
 * buffer given to push).  TOILE_TR_EAGAIN when nothing completed. */
int toile_tr_poll(toile_transport *tr, uint16_t qidx,
                  void **cookie, size_t *resp_len);

/* Synchronous convenience: push + kick + wait + poll, until the response
 * arrives or `timeout_ms` elapses (negative = wait forever). */
int toile_tr_submit(toile_transport *tr, uint16_t qidx,
                    const void *cmd, size_t cmd_len,
                    void *resp, size_t resp_cap, size_t *resp_len,
                    int32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_TRANSPORT_H */
