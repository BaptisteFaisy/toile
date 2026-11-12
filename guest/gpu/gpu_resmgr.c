/* SPDX-License-Identifier: MIT */
/*
 * Toile — /dev/gpu0 resource manager (exigence C7, socle S6).
 *
 * Pattern suivi : le sample officiel du SDP installé
 * (usr/help/eclipse/plugins/com.qnx.doc.neutrino.resmgr/topic/
 * messages_Sample_IO_DEVCTL.html, relu le 2026-11-12) :
 *   - iofunc_devctl_default() d'abord (DCMD_ALL_* système) ;
 *   - données entrantes via _IO_INPUT_PAYLOAD(msg) ;
 *   - réponse : écrire dans le payload, memset(&msg->o), ret_val,
 *     _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + nbytes).
 * API QNX 8.0 vérifiée dans les en-têtes du SDP :
 *   - resmgr_attach(dispatch_t*, resmgr_attr_t*, path, enum _file_type,
 *     flags, connect_funcs, io_funcs, handle)  [sys/dispatch.h:100]
 *   - iofunc_func_init / iofunc_attr_init      [sys/iofunc.h:464,510]
 *   - resmgr_context_alloc/block/handler       [sys/dispatch.h]
 */
#include "gpu_priv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/dispatch.h>
#include <sys/iofunc.h>
#include <sys/resmgr.h>

#define GPU_DEVPATH "/dev/gpu0"
/* Message buffer headroom for resmgr_attach (payload fits: voir
 * gpu_api.h — plus grand struct = toile_gpu_info_t/fb_t < 64 o). */
#define GPU_MSG_MAX_SIZE 2048u

static dispatch_t *g_dpp;
static iofunc_attr_t g_attr;
static gpu_ctx *g_gpu;

static int gpu_devctl_handle(resmgr_context_t *ctp, io_devctl_t *msg,
                             uint32_t *nbytes_out)
{
    (void)ctp; /* la réponse passe par msg->o dans l'appelant */
    void *payload = _IO_INPUT_PAYLOAD(msg);
    *nbytes_out = 0u;
    switch (msg->i.dcmd) {    case DCMD_TOILE_GPU_INFO:
        gpu_fill_info(g_gpu, (toile_gpu_info_t *)payload);
        *nbytes_out = sizeof(toile_gpu_info_t);
        return EOK;
    case DCMD_TOILE_GPU_FB: {
        toile_gpu_fb_t *fb = (toile_gpu_fb_t *)payload;
        fb->paddr = g_gpu->fb.pa;
        fb->bytes = g_gpu->fb.bytes;
        fb->stride = g_gpu->fb.stride;
        fb->format = g_gpu->format;
        fb->width = g_gpu->width;
        fb->height = g_gpu->height;
        *nbytes_out = sizeof(*fb);
        return EOK;
    }
    case DCMD_TOILE_GPU_PRESENT: {
        toile_gpu_rect_t r;
        memset(&r, 0, sizeof(r));
        if (msg->i.nbytes == sizeof(r))
            memcpy(&r, payload, sizeof(r));
        toile_rect rect = {r.x, r.y, r.w, r.h};
        int rc = gpu_present(g_gpu, rect);
        return rc == TOILE_OK ? EOK : EIO;
    }
    case DCMD_TOILE_GPU_MIRE:
        gpu_paint_mire(&g_gpu->fb, g_gpu->width, g_gpu->height,
                       g_gpu->format);
        (void)gpu_present(g_gpu, (toile_rect){0u, 0u, 0u, 0u});
        *(uint32_t *)payload = 0u;
        *nbytes_out = sizeof(uint32_t);
        return EOK;
    default:
        return ENOSYS;
    }
}

/* Le type du slot `devctl` de resmgr_io_funcs_t est
 * int (*)(resmgr_context_t *, io_devctl_t *, RESMGR_OCB_T *) avec
 * RESMGR_OCB_T = void (sys/resmgr.h:169,179) ; le cast vers iofunc_ocb_t *
 * est celui que fait la documentation officielle du SDP. */
static int gpu_io_devctl(resmgr_context_t *ctp, io_devctl_t *msg,
                         RESMGR_OCB_T *ocb)
{
    int status = iofunc_devctl_default(ctp, msg, (iofunc_ocb_t *)ocb);
    if (status != _RESMGR_DEFAULT)
        return status;
    uint32_t nbytes = 0;
    status = gpu_devctl_handle(ctp, msg, &nbytes);
    if (status == ENOSYS)
        return ENOSYS;
    memset(&msg->o, 0, sizeof(msg->o));
    msg->o.ret_val = status;
    return _RESMGR_PTR(ctp, &msg->o, sizeof(msg->o) + nbytes);
}

int gpu_resmgr_attach(gpu_ctx *g)
{
    if (g == NULL)
        return -1;
    g_gpu = g;
    g_dpp = dispatch_create();
    if (g_dpp == NULL)
        return -1;
    resmgr_attr_t rattr;
    memset(&rattr, 0, sizeof(rattr));
    rattr.msg_max_size = GPU_MSG_MAX_SIZE;
    resmgr_connect_funcs_t cfuncs;
    resmgr_io_funcs_t iofuncs;
    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &cfuncs,
                     _RESMGR_IO_NFUNCS, &iofuncs);
    iofuncs.devctl = gpu_io_devctl;
    iofunc_attr_init(&g_attr, 0666, NULL, NULL);
    int id = resmgr_attach(g_dpp, &rattr, GPU_DEVPATH, _FTYPE_ANY, 0,
                           &cfuncs, &iofuncs, &g_attr);
    if (id < 0) {
        gpu_log(g, "resmgr_attach(%s) failed: %s", GPU_DEVPATH,
                strerror(errno));
        return -1;
    }
    gpu_log(g, "serving %s", GPU_DEVPATH);
    return 0;
}

void gpu_resmgr_loop(void)
{
    resmgr_context_t *ctp = resmgr_context_alloc(g_dpp);
    for (;;) {
        ctp = resmgr_block(ctp);
        if (ctp != NULL)
            resmgr_handler(ctp);
    }
}
