/* SPDX-License-Identifier: MIT */
/*
 * Toile — guest GPU driver: command pipeline.
 *
 * Every command goes through the same path (CONTRACT.md §1, decision D1):
 *
 *   validate + apply to the portable core model (toile_cmdproc_run)
 *     -> encode to wire bytes (toile_vg_encode)
 *       -> submit through the Passerelle-based transport (toile_tr_submit)
 *         -> check the device response header.
 *
 * A command the model rejects is NEVER sent to the device (fail fast,
 * no partial application).  A device error response is reported upward;
 * model/device divergence detection is the C8 capture-replay trial.
 */
#include "gpu_priv.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Wire response header: type (le32) at offset 0, 24 bytes total. */
uint32_t gpu_resp_type(const uint8_t *resp, size_t len)
{
    if (resp == NULL || len < 4u)
        return 0u;
    return (uint32_t)resp[0] | ((uint32_t)resp[1] << 8) |
           ((uint32_t)resp[2] << 16) | ((uint32_t)resp[3] << 24);
}

/* Cursor commands (0x03xx) travel on cursorq, everything else controlq
 * (OASIS virtio 1.x §5.7.2). */
int gpu_ctrl_queue_of(uint32_t cmd_type)
{
    if ((cmd_type & 0xFF00u) == 0x0300u)
        return (int)TOILE_VQ_CURSORQ;
    return (int)TOILE_VQ_CONTROLQ;
}

void gpu_log(const gpu_ctx *g, const char *fmt, ...)
{
    (void)g; /* reserved for per-context log routing */
    va_list ap;
    va_start(ap, fmt);
    fputs("toile-gpu: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static int gpu_model_apply(gpu_ctx *g, const toile_cmd *cmd)
{
    toile_verdict v[4];
    size_t nv = 0;
    int rc = toile_cmdproc_run(cmd, 1u, &g->model, v, 4u, &nv);
    if (rc != TOILE_OK) {
        const char *cause = nv > 0u ? v[0].cause : "";
        gpu_log(g, "model rejected cmd 0x%04x: %s (%s)",
                (unsigned)cmd->type, toile_verdict_name((toile_verdict_code)rc),
                cause);
    }
    return rc;
}

static int gpu_wire_submit(gpu_ctx *g, const toile_cmd *cmd,
                           uint8_t *resp, size_t resp_cap, size_t *resp_len)
{
    uint8_t wire[TOILE_VG_MAX_CMD_LEN];
    uint8_t local[TOILE_TR_RESP_CAP];
    size_t wire_len = 0;
    int rc = toile_vg_encode(cmd, wire, sizeof(wire), &wire_len);
    if (rc != TOILE_OK)
        return rc;
    uint8_t *rb = resp != NULL ? resp : local;
    size_t cap = resp != NULL ? resp_cap : sizeof(local);
    size_t got = 0;
    int trc = toile_tr_submit(g->tr, (uint16_t)gpu_ctrl_queue_of(cmd->type),
                              wire, wire_len, rb, cap, &got,
                              GPU_CMD_TIMEOUT_MS);
    if (trc != TOILE_TR_OK) {
        gpu_log(g, "transport error %d on cmd 0x%04x", trc,
                (unsigned)cmd->type);
        return -trc; /* negative: transport-level failure */
    }
    if (resp_len != NULL)
        *resp_len = got;
    uint32_t rt = gpu_resp_type(rb, got);
    if (rt == TOILE_VG_RESP_OK_NODATA || rt == TOILE_VG_RESP_OK_DISPLAY_INFO)
        return TOILE_OK;
    gpu_log(g, "device error response 0x%04x for cmd 0x%04x",
            (unsigned)rt, (unsigned)cmd->type);
    return -TOILE_TR_EIO;
}

int gpu_ctrl_run(gpu_ctx *g, const toile_cmd *cmd,
                 uint8_t *resp, size_t resp_cap, size_t *resp_len)
{
    if (g == NULL || cmd == NULL || g->tr == NULL)
        return TOILE_CMD_INVALID;
    int rc = gpu_model_apply(g, cmd);
    if (rc != TOILE_OK)
        return rc; /* model rejected: nothing hits the wire */
    return gpu_wire_submit(g, cmd, resp, resp_cap, resp_len);
}
