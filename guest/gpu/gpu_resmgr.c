/* SPDX-License-Identifier: MIT */
/*
 * Toile — /dev/gpu0 resource manager (exigence C7, socle S6).
 *
 * Modèle iofunc canonique (doc « Writing a Resource Manager » du SDP 8.0,
 * relue le 2026-11-12) :
 *
 *   - resmgr_attach() CONSERVE LES POINTEURS vers resmgr_connect_funcs_t et
 *     resmgr_io_funcs_t (pas une copie).  Les tables doivent donc vivre en
 *     statique/globale — un usage en pile est la cause du SIGSEGV client
 *     (déréférencement de pointeurs pendants, addr=0x10).
 *   - iofunc_func_init() remplit déjà connect.open = iofunc_open_default et
 *     tous les défauts I/O POSIX.  On ne surcharge que devctl (nos DCMD
 *     DCMD_TOILE_GPU_*).
 *   - Le message _IO_CONNECT_COMBINE_CLOSE doit être laissé à la bibliothèque
 *     (aucun callout `combo` maison) : elle déroule open → opération → close
 *     (doc « Connect message types » / combine_UsingCombineMsgs).
 *
 * API QNX 8.0 vérifiée dans les en-têtes du SDP :
 *   - resmgr_attach(dispatch_t*, resmgr_attr_t*, path, enum _file_type,
 *     flags, connect_funcs, io_funcs, handle)  [sys/dispatch.h]
 *   - iofunc_func_init / iofunc_attr_init        [sys/iofunc.h]
 *   - iofunc_open_default(ctp, msg, attr, extra) [sys/iofunc.h]
 *   - iofunc_devctl_default(ctp, msg, ocb)       [sys/iofunc.h]
 */
#include "gpu_priv.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/iofunc.h> /* QNX 8.0 : avant <sys/resmgr.h> pour que
                         * RESMGR_HANDLE_T soit le type d'attribut iofunc */
#include <sys/resmgr.h>
#include <sys/dispatch.h>

#define GPU_DEVPATH "/dev/gpu0"

static dispatch_t *g_dpp;
static iofunc_attr_t g_attr;
static gpu_ctx *g_gpu;

/* resmgr_attach() mémorise ces pointeurs : durée de vie du processus. */
static resmgr_attr_t g_rattr;
static resmgr_connect_funcs_t g_connect_funcs;
static resmgr_io_funcs_t g_io_funcs;

/* FNV-1a 64 bits : empreinte du framebuffer, pour comparer avant/après une
 * relecture hôte->invité (preuve numérique A). */
static uint64_t gpu_fb_hash(const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)p[i];
        h *= 1099511628211ull;
    }
    return h;
}

/* Preuve A — relecture numérique.  On empreint le framebuffer, on demande au
 * périphérique une copie hôte->invité (TRANSFER_FROM_HOST_3D, code 0x0206)
 * sur toute la surface, puis on ré-empreint.  Deux empreintes identiques
 * prouvent que les pixels relus par l'émulateur sont ceux de l'invité.
 * Si le périphérique refuse la commande, on l'annonce tel quel (pas de
 * preuve inventée). */
static void gpu_readback_probe(gpu_ctx *g)
{
    if (g == NULL || g->fb.va == NULL)
        return;
    size_t len = (size_t)g->fb.bytes;
    uint64_t before = gpu_fb_hash(g->fb.va, len);
    toile_cmd cmd;
    uint8_t resp[TOILE_TR_RESP_CAP];
    size_t resp_len = 0u;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = TOILE_VG_CMD_TRANSFER_FROM_HOST_3D;
    cmd.u.xfer.resource_id = GPU_RES_SCANOUT;
    cmd.u.xfer.rect.x = 0u;
    cmd.u.xfer.rect.y = 0u;
    cmd.u.xfer.rect.w = g->width;
    cmd.u.xfer.rect.h = g->height;
    cmd.u.xfer.offset = 0u;
    memset(resp, 0, sizeof(resp));
    int rc = gpu_ctrl_run(g, &cmd, resp, sizeof(resp), &resp_len);
    uint64_t after = gpu_fb_hash(g->fb.va, len);
    uint32_t rtype = gpu_resp_type(resp, resp_len);
    gpu_announce("relecture transfer_from_host(0x0206) rc=%d resp=0x%04x "
                 "avant=%016llx apres=%016llx %s",
                 rc, (unsigned)rtype, (unsigned long long)before,
                 (unsigned long long)after,
                 rc != 0 ? "REFUSEE_PAR_LE_PERIPHERIQUE"
                         : (before == after ? "IDENTIQUES" : "DIFFERENTES"));
}

static int gpu_devctl_handle(resmgr_context_t *ctp, io_devctl_t *msg,
                             uint32_t *nbytes_out)
{
    (void)ctp;
    void *payload = _IO_INPUT_PAYLOAD(msg);
    *nbytes_out = 0u;
    switch (msg->i.dcmd) {
    case DCMD_TOILE_GPU_INFO:
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
        gpu_readback_probe(g_gpu);
        *(uint32_t *)payload = 0u;
        *nbytes_out = sizeof(uint32_t);
        return EOK;
    default:
        return ENOSYS;
    }
}

/* Fentes devctl : iofunc_devctl_default traite les DCMD_ALL_* système,
 * puis nos DCMD DCMD_TOILE_GPU_*.  Le slot est
 * int (*)(resmgr_context_t *, io_devctl_t *, RESMGR_OCB_T *) ; RESMGR_OCB_T
 * vaut iofunc_ocb_t (iofunc.h inclus avant resmgr.h). */
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

/* Connect-level open (preuve d'ouverture tracée côté serveur).  Le défaut
 * iofunc_open_default est installé par iofunc_func_init ; on l'enveloppe
 * uniquement pour journaliser, sans changer le comportement. */
static int gpu_connect_open(resmgr_context_t *ctp, io_open_t *msg,
                            RESMGR_HANDLE_T *handle, void *extra)
{
    int rc = iofunc_open_default(ctp, msg, handle, extra);
    gpu_log(NULL, "resmgr: open -> rc=%d", rc);
    return rc;
}

int gpu_resmgr_attach(gpu_ctx *g)
{
    if (g == NULL)
        return -1;
    g_gpu = g;
    g_dpp = dispatch_create();
    if (g_dpp == NULL)
        return -1;

    memset(&g_rattr, 0, sizeof(g_rattr));
    g_rattr.msg_max_size = 8192; /* connect + I/O combinés tiennent large */
    g_rattr.nparts_max = 4;

    iofunc_func_init(_RESMGR_CONNECT_NFUNCS, &g_connect_funcs,
                     _RESMGR_IO_NFUNCS, &g_io_funcs);
    g_connect_funcs.open = gpu_connect_open;
    g_io_funcs.devctl = gpu_io_devctl;

    /* Un attribut par nom de périphérique (doc « The iofunc_attr_t
     * attributes structure ») ; l'OCB est alloué par ouverture. */
    iofunc_attr_init(&g_attr, S_IFCHR | 0666, NULL, NULL);

    int id = resmgr_attach(g_dpp, &g_rattr, GPU_DEVPATH, _FTYPE_ANY, 0,
                           &g_connect_funcs, &g_io_funcs, &g_attr);
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