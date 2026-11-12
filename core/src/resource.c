/* SPDX-License-Identifier: MIT */
/*
 * Toile — resources, backing, flush and transfers (CONTRACT.md §3, §5, §6).
 *
 * Every mutator returns TOILE_OK (0) or a positive verdict code. On failure
 * the model is left untouched except for cmds_rejected++; on success
 * cmds_applied++ (TOILE_TRANSFER_OK counts as success).
 *
 * Frozen §6 causes are not carried by the frozen resource.h signatures; they
 * are derivable by the command processor from the returned code plus the
 * command fields, and are documented at each rejection site:
 *   create        -> TOILE_CMD_INVALID    (bad_format | bad_geometry |
 *                                          resource_table_full)
 *   create (dup)  -> TOILE_RESOURCE_EXISTS
 *   attach        -> TOILE_CMD_INVALID    (backing_entry_count)
 *   unref (shown) -> TOILE_SCANOUT_INVALID (resource_referenced_by_scanout)
 *   missing id    -> TOILE_RESOURCE_MISSING
 */
#include <stddef.h>
#include <string.h>

#include "toile/resource.h"
#include "toile/scanout.h"
#include "toile/vgproto.h"
#include "toile/verdict.h"

/* Bump the rejected counter and forward the failure code. */
static int reject(toile_model *m, int code)
{
    m->cmds_rejected++;
    return code;
}

/* Bump the applied counter and forward the success code. */
static int accept(toile_model *m, int code)
{
    m->cmds_applied++;
    return code;
}

/* Mutable lookup; NULL when the resource is not active. */
static toile_resource *find_mut(toile_model *m, uint32_t id)
{
    size_t i;

    if (m == NULL)
        return NULL;
    for (i = 0; i < TOILE_MAX_RESOURCES; i++)
        if (m->resources[i].in_use && m->resources[i].id == id)
            return &m->resources[i];
    return NULL;
}

/* First free table slot; NULL when the table is full. */
static toile_resource *free_slot(toile_model *m)
{
    size_t i;

    for (i = 0; i < TOILE_MAX_RESOURCES; i++)
        if (!m->resources[i].in_use)
            return &m->resources[i];
    return NULL;
}

/* CONTRACT.md §3 expansion rule: a rect whose width AND height are both 0
 * means "the whole resource". Idempotent, identical in C and Python. */
static toile_rect full_rect(const toile_resource *r, toile_rect rect)
{
    if (rect.w == 0 && rect.h == 0) {
        rect.x = 0;
        rect.y = 0;
        rect.w = r->width;
        rect.h = r->height;
    }
    return rect;
}

void toile_model_init(toile_model *m)
{
    if (m != NULL)
        memset(m, 0, sizeof *m);
}

const toile_resource *toile_resource_find(const toile_model *m, uint32_t id)
{
    return find_mut((toile_model *)(uintptr_t)m, id);
}

size_t toile_resource_count(const toile_model *m)
{
    size_t i, n = 0;

    if (m == NULL)
        return 0;
    for (i = 0; i < TOILE_MAX_RESOURCES; i++)
        n += m->resources[i].in_use ? 1u : 0u;
    return n;
}

int toile_resource_create(toile_model *m, uint32_t id, toile_fmt fmt,
                          uint32_t w, uint32_t h)
{
    toile_resource *r;

    if (m == NULL)
        return TOILE_CMD_INVALID;
    if (find_mut(m, id) != NULL)
        return reject(m, TOILE_RESOURCE_EXISTS);
    if (toile_fmt_bpp(fmt) == 0)          /* cause: bad_format */
        return reject(m, TOILE_CMD_INVALID);
    if (w == 0 || h == 0)                 /* cause: bad_geometry */
        return reject(m, TOILE_CMD_INVALID);
    r = free_slot(m);
    if (r == NULL)                        /* cause: resource_table_full */
        return reject(m, TOILE_CMD_INVALID);
    memset(r, 0, sizeof *r);
    r->id = id;
    r->fmt = fmt;
    r->width = w;
    r->height = h;
    r->in_use = 1;
    return accept(m, TOILE_OK);
}

int toile_resource_unref(toile_model *m, uint32_t id)
{
    toile_resource *r = find_mut(m, id);

    if (m == NULL)
        return TOILE_CMD_INVALID;
    if (r == NULL)
        return reject(m, TOILE_RESOURCE_MISSING);
    /* Cause: resource_referenced_by_scanout (DECISIONS.md D11, invariant I1). */
    if (toile_resource_has_scanout(m, id))
        return reject(m, TOILE_SCANOUT_INVALID);
    memset(r, 0, sizeof *r);
    return accept(m, TOILE_OK);
}

int toile_resource_attach_backing(toile_model *m, uint32_t id,
                                  const toile_page *pages, size_t n)
{
    toile_resource *r = find_mut(m, id);
    size_t i;

    if (m == NULL)
        return TOILE_CMD_INVALID;
    if (r == NULL)
        return reject(m, TOILE_RESOURCE_MISSING);
    /* Cause: backing_entry_count (0 or more than TOILE_MAX_PAGES entries). */
    if (n == 0 || n > TOILE_MAX_PAGES || pages == NULL)
        return reject(m, TOILE_CMD_INVALID);
    for (i = 0; i < n; i++)
        r->pages[i] = pages[i];           /* attachment order preserved */
    r->n_pages = (uint32_t)n;
    return accept(m, TOILE_OK);
}

int toile_resource_detach_backing(toile_model *m, uint32_t id)
{
    toile_resource *r = find_mut(m, id);

    if (m == NULL)
        return TOILE_CMD_INVALID;
    if (r == NULL)
        return reject(m, TOILE_RESOURCE_MISSING);
    r->n_pages = 0;                       /* no-op when none was attached */
    return accept(m, TOILE_OK);
}

int toile_resource_has_backing(const toile_model *m, uint32_t id)
{
    const toile_resource *r = toile_resource_find(m, id);

    return (r != NULL && r->n_pages > 0) ? 1 : 0;
}

int toile_resource_flush(toile_model *m, uint32_t id, toile_rect rect)
{
    toile_resource *r = find_mut(m, id);

    if (m == NULL)
        return TOILE_CMD_INVALID;
    if (r == NULL)
        return reject(m, TOILE_RESOURCE_MISSING);
    /* Invariant I2: a flush only succeeds on a scanned-out resource. */
    if (!toile_resource_has_scanout(m, id))
        return reject(m, TOILE_FLUSH_NO_SCANOUT);
    (void)full_rect(r, rect);             /* rect is host-side display only */
    r->flush_count++;
    m->flushes++;
    return accept(m, TOILE_OK);
}

/* Shared guest<->host transfer checks; returns the resource or NULL and sets
 * *err to the failure verdict. */
static toile_resource *xfer_begin(toile_model *m, uint32_t id, int *err)
{
    toile_resource *r = find_mut(m, id);

    *err = TOILE_OK;
    if (m == NULL) {
        *err = TOILE_CMD_INVALID;
        return NULL;
    }
    if (r == NULL) {
        *err = reject(m, TOILE_RESOURCE_MISSING);
        return NULL;
    }
    if (r->n_pages == 0) {                /* invariant I3 */
        *err = reject(m, TOILE_BACKING_MISSING);
        return NULL;
    }
    return r;
}

int toile_resource_transfer_to_host(toile_model *m, uint32_t id, toile_rect rect,
                                    uint64_t offset)
{
    toile_resource *r;
    int err;

    (void)offset;                         /* the model copies no real bytes */
    r = xfer_begin(m, id, &err);
    if (r == NULL)
        return err;
    rect = full_rect(r, rect);
    /* Invariant I6: bytes_transferred += area x bpp of successful transfers. */
    m->bytes_transferred +=
        (uint64_t)rect.w * (uint64_t)rect.h * toile_fmt_bpp(r->fmt);
    return accept(m, TOILE_TRANSFER_OK);
}

int toile_resource_transfer_from_host(toile_model *m, uint32_t id,
                                      toile_rect rect, uint64_t offset)
{
    toile_resource *r;
    int err;

    (void)offset;
    r = xfer_begin(m, id, &err);
    if (r == NULL)
        return err;
    (void)full_rect(r, rect);             /* host->guest does not feed I6 */
    return accept(m, TOILE_OK);
}
