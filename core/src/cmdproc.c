/* SPDX-License-Identifier: MIT */
/*
 * Toile — command processor (CONTRACT.md sections 1, 5 and 6).
 *
 * For each command: validate, apply to the state, produce the verdict, then
 * re-check the invariants and stop at the first counter-example (fail fast).
 * The state functions own the applied/rejected counters; the processor only
 * counts the rejections it handles itself (unknown type, invisible cursor).
 */
#include "toile/cmdproc.h"
#include "toile/scanout.h"

typedef struct run_ctx {
    toile_model *m;
    toile_verdict *out;
    size_t cap;
    size_t n_out;    /* verdicts actually written (never above cap) */
    int first_error; /* first error code seen, TOILE_OK otherwise */
} run_ctx;

/* CONTRACT.md section 3 expansion rule: width == 0 AND height == 0 means
 * "the whole resource". Idempotent: the state functions apply it as well. */
static void develop_rect(const toile_model *m, uint32_t id, toile_rect *r)
{
    const toile_resource *res;

    if (r->w != 0u || r->h != 0u)
        return;
    res = toile_resource_find(m, id);
    if (!res)
        return;
    r->x = 0u;
    r->y = 0u;
    r->w = res->width;
    r->h = res->height;
}

/* Resource the command refers to, TOILE_ID_NONE when not applicable. */
static uint32_t cmd_resource_id(const toile_cmd *c)
{
    switch (c->type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D: return c->u.create_2d.resource_id;
    case TOILE_VG_CMD_RESOURCE_UNREF: return c->u.unref.resource_id;
    case TOILE_VG_CMD_SET_SCANOUT: return c->u.scanout.resource_id;
    case TOILE_VG_CMD_RESOURCE_FLUSH: return c->u.flush.resource_id;
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D: return c->u.xfer.resource_id;
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING: return c->u.backing.resource_id;
    case TOILE_VG_CMD_UPDATE_CURSOR:
        return c->u.cursor.resource_id ? c->u.cursor.resource_id : TOILE_ID_NONE;
    default: return TOILE_ID_NONE;
    }
}

/* Scanout involved in the command, TOILE_ID_NONE when not applicable. */
static uint32_t cmd_scanout_id(const toile_cmd *c, const toile_model *m)
{
    switch (c->type) {
    case TOILE_VG_CMD_SET_SCANOUT: return c->u.scanout.scanout_id;
    case TOILE_VG_CMD_UPDATE_CURSOR:
    case TOILE_VG_CMD_MOVE_CURSOR: return c->u.cursor.scanout_id;
    case TOILE_VG_CMD_RESOURCE_UNREF:
        for (uint32_t i = 0; i < TOILE_MAX_SCANOUTS; i++) {
            const toile_scanout *s = &m->scanouts[i];

            if (s->in_use && s->resource_id == c->u.unref.resource_id)
                return i;
        }
        return TOILE_ID_NONE;
    default: return TOILE_ID_NONE;
    }
}

/* Hardware cursor: position on a scanout, optional image resource. */
static int cursor_update(toile_model *m, const toile_cmd *c)
{
    m->cursor.scanout_id = c->u.cursor.scanout_id;
    m->cursor.x = c->u.cursor.x;
    m->cursor.y = c->u.cursor.y;
    if (c->u.cursor.resource_id != 0u) {
        m->cursor.resource_id = c->u.cursor.resource_id;
        m->cursor.hot_x = c->u.cursor.hot_x;
        m->cursor.hot_y = c->u.cursor.hot_y;
    }
    m->cursor.visible = 1u;
    m->cmds_applied++;
    return TOILE_OK;
}

/* Move an already visible cursor (visibility checked by the caller). */
static int cursor_move(toile_model *m, const toile_cmd *c)
{
    m->cursor.scanout_id = c->u.cursor.scanout_id;
    m->cursor.x = c->u.cursor.x;
    m->cursor.y = c->u.cursor.y;
    m->cmds_applied++;
    return TOILE_OK;
}

/* --- application ---------------------------------------------------------- */

static int apply_resource_cmd(toile_model *m, const toile_cmd *c, toile_rect *r)
{
    switch (c->type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        return toile_resource_create(m, c->u.create_2d.resource_id,
                                     (toile_fmt)c->u.create_2d.format,
                                     c->u.create_2d.width,
                                     c->u.create_2d.height);
    case TOILE_VG_CMD_RESOURCE_UNREF:
        return toile_resource_unref(m, c->u.unref.resource_id);
    case TOILE_VG_CMD_SET_SCANOUT:
        return toile_scanout_set(m, c->u.scanout.scanout_id,
                                 c->u.scanout.resource_id,
                                 c->u.scanout.rect.x, c->u.scanout.rect.y,
                                 c->u.scanout.rect.w, c->u.scanout.rect.h);
    case TOILE_VG_CMD_RESOURCE_FLUSH:
        develop_rect(m, c->u.flush.resource_id, r);
        return toile_resource_flush(m, c->u.flush.resource_id, *r);
    case TOILE_VG_CMD_TRANSFER_TO_HOST_2D:
        develop_rect(m, c->u.xfer.resource_id, r);
        return toile_resource_transfer_to_host(m, c->u.xfer.resource_id, *r,
                                               c->u.xfer.offset);
    case TOILE_VG_CMD_TRANSFER_FROM_HOST_3D:
        develop_rect(m, c->u.xfer.resource_id, r);
        return toile_resource_transfer_from_host(m, c->u.xfer.resource_id, *r,
                                                 c->u.xfer.offset);
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        return toile_resource_attach_backing(m, c->u.backing.resource_id,
                                             c->u.backing.pages,
                                             c->u.backing.nr_entries);
    case TOILE_VG_CMD_RESOURCE_DETACH_BACKING:
        return toile_resource_detach_backing(m, c->u.backing.resource_id);
    default:
        return TOILE_CMD_INVALID; /* unknown_command_type */
    }
}

static int apply_cmd(toile_model *m, const toile_cmd *c, toile_rect *r)
{
    switch (c->type) {
    case TOILE_VG_CMD_GET_DISPLAY_INFO:
        return TOILE_OK; /* no mutation */
    case TOILE_VG_CMD_UPDATE_CURSOR:
        return cursor_update(m, c);
    case TOILE_VG_CMD_MOVE_CURSOR:
        return cursor_move(m, c);
    default:
        return apply_resource_cmd(m, c, r);
    }
}

/* --- verdicts ------------------------------------------------------------- */

/* Fixed cause of a TOILE_CMD_INVALID verdict (CONTRACT.md section 6). */
static const char *cause_for(const toile_cmd *c, int code)
{
    if (code != TOILE_CMD_INVALID)
        return NULL;
    switch (c->type) {
    case TOILE_VG_CMD_RESOURCE_CREATE_2D:
        if (toile_fmt_bpp((toile_fmt)c->u.create_2d.format) == 0u)
            return "bad_format";
        if (c->u.create_2d.width == 0u || c->u.create_2d.height == 0u)
            return "bad_geometry";
        return "resource_table_full";
    case TOILE_VG_CMD_RESOURCE_ATTACH_BACKING:
        return "backing_entry_count";
    case TOILE_VG_CMD_SET_SCANOUT:
        if (c->u.scanout.scanout_id >= TOILE_MAX_SCANOUTS)
            return "scanout_id_out_of_range";
        if (c->u.scanout.rect.w == 0u || c->u.scanout.rect.h == 0u)
            return "bad_geometry";
        return "scanout_table_full";
    default:
        return NULL;
    }
}

/* Writes one verdict when there is room; always tracks the first error. */
static void emit(run_ctx *r, toile_verdict_code code, uint32_t idx,
                 uint32_t type, uint32_t res, uint32_t scan,
                 const toile_rect *rect, const char *cause)
{
    if (toile_verdict_is_error(code) && r->first_error == TOILE_OK)
        r->first_error = (int)code;
    if (r->n_out >= r->cap || !r->out)
        return;
    toile_verdict_set(&r->out[r->n_out], code, idx, type, res, scan,
                      rect ? *rect : (toile_rect){ 0u, 0u, 0u, 0u }, cause);
    r->n_out++;
}

/* --- sequence driver ------------------------------------------------------ */

/* True when the type belongs to the table of CONTRACT.md section 3. */
static int type_known(uint32_t type)
{
    return toile_cmd_name(type) != NULL;
}

/* Rejection decided before any state change: unknown type, invisible
 * cursor. Returns TOILE_OK when the command may be applied. */
static int precheck(const toile_model *m, const toile_cmd *c, const char **cause)
{
    if (!type_known(c->type)) {
        *cause = "unknown_command_type";
        return TOILE_CMD_INVALID;
    }
    if (c->type == TOILE_VG_CMD_MOVE_CURSOR && !m->cursor.visible) {
        *cause = "cursor_not_visible";
        return TOILE_CMD_INVALID;
    }
    return TOILE_OK;
}

int toile_cmdproc_run(const toile_cmd *cmds, size_t n, toile_model *model,
                      toile_verdict *out, size_t cap, size_t *n_out)
{
    run_ctx r;
    int complete = 1;

    if (n_out)
        *n_out = 0;
    if (!cmds || !model || !out || !n_out || n == 0)
        return TOILE_CMD_INVALID;
    r.m = model; r.out = out; r.cap = cap; r.n_out = 0; r.first_error = TOILE_OK;
    for (size_t i = 0; i < n; i++) {
        const toile_cmd *c = &cmds[i];
        toile_rect rect = { 0u, 0u, 0u, 0u };
        char cause[TOILE_CAUSE_MAX];
        const char *cause0 = NULL;
        uint32_t res, scan;
        int code, pre;

        if (r.n_out >= r.cap) {
            complete = 0; /* output full: stop, never overflow */
            break;
        }
        pre = precheck(model, c, &cause0);
        if (pre != TOILE_OK) {
            model->cmds_rejected++;
            emit(&r, TOILE_CMD_INVALID, (uint32_t)i, c->type, TOILE_ID_NONE,
                 cmd_scanout_id(c, model), NULL, cause0);
            continue;
        }
        res = cmd_resource_id(c);
        scan = cmd_scanout_id(c, model);
        code = apply_cmd(model, c, &rect);
        emit(&r, (toile_verdict_code)code, (uint32_t)i, c->type, res, scan,
             (code == TOILE_TRANSFER_OK) ? &rect : NULL, cause_for(c, code));
        if (toile_model_check_invariants(model, cause, sizeof cause) != 0) {
            emit(&r, TOILE_MODEL_COUNTEREXAMPLE, (uint32_t)i, c->type,
                 TOILE_ID_NONE, TOILE_ID_NONE, NULL, cause);
            *n_out = r.n_out;
            return r.first_error; /* fail fast: stop the sequence */
        }
    }
    if (complete && r.n_out < r.cap)
        emit(&r, TOILE_MODEL_CONSISTENT, TOILE_ID_NONE, TOILE_ID_NONE,
             TOILE_ID_NONE, TOILE_ID_NONE, NULL, NULL);
    *n_out = r.n_out;
    return r.first_error;
}
