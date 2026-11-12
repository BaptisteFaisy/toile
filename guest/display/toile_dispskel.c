/* SPDX-License-Identifier: MIT */
/*
 * Toile — squelette de display driver Screen (S7) : OpenWF Display (WFD).
 *
 * E2, verifie le 2026-11-12 dans le SDP QNX 8.0 installe : le contrat
 * reel d'un display driver tiers de Screen en QNX 8.0 n'est PAS
 * « io-display » (modele QNX 6.x, absent du SDP 8.0) mais un driver
 * OpenWF Display (WFD) couple a la bibliotheque Wfdcfg.
 *
 *   Screen (gestionnaire de composition)
 *        |  OpenWF Display (WFD)
 *        v
 *   driver WFD de Toile  <-->  Wfdcfg (modes/attributs du materiel)
 *
 * Ce fichier n'invente AUCUNE API : chaque fonction utilisee est un
 * prototype relu dans les en-tetes du SDP, aux lignes citees :
 *
 *   WF/wfd.h        wfdEnumerateDevices (:271), wfdCreateDevice (:276),
 *                   wfdDeviceCommit (:283), wfdEnumeratePorts (:328),
 *                   wfdCreatePort (:334), wfdSetPortMode (:360),
 *                   wfdCreatePipeline (:444), wfdCreateSourceFromImage (:453),
 *                   wfdBindSourceToPipeline (:485)
 *   wfdqnx/wfdcfg.h wfdcfg_device_create (:217), wfdcfg_port_create (:272),
 *                   wfdcfg_mode_list_create (:325)
 *   WF/wfdext.h     wfdCreateWFDEGLImagesQNX (:393),
 *                   wfdCreateDestinationFromImageQNX (:878)
 *
 * E2 : ce qui manque pour INTEGRER ET TESTER, liste exhaustive :
 *
 *   1. « libWFDplatform » — la couche platform WFD (implementation de
 *      reference de WF/wfdplatform.h, qui definit WFDint, WFDDevice,
 *      WFDEGLImage, ...).  Seul l'en-tete wfdplatform.h est livre dans le
 *      SDP ; aucune bibliotheque correspondante n'y est installee.
 *      Pour tester : le BSP « Screen Board Support package » qui fournit
 *      la couche platform de la carte visee.
 *   2. « wfd-server » — le serveur WFD (l'homologue de « screen » pour le
 *      graphe WFD).  Dans le SDP installe, « wfd-server » n'existe QUE
 *      comme documentation (plugin com.qnx.doc.screen.wfd-server).
 *      Sans lui, wfdEnumerateDevices() renvoie 0 appareil.
 *   3. « libwfdcfg.so » — la bibliotheque Wfdcfg.  L'en-tete
 *      wfdqnx/wfdcfg.h est livre, la bibliotheque ne l'est pas :
 *      wfdcfg_device_create() n'est exporte par AUCUNE bibliotheque du
 *      SDP (verifie par nm -D sur tous les .so de target/qnx/x86_64).
 *      Pour tester : le BSP qui fournit l'archive Wfdcfg.
 *
 * Pources de conception :
 *
 *   - Les points d'entree sont resolus a l'execution par dlopen/dlsym et
 *     non lies statiquement.  Raison : la pile WFD de cette machine est
 *     INCOMPLETE (libWFDplatform et wfd-server manquants).  Un lien
 *     statique sur libWFD.so rendrait le binaire de demonstration
 *     incapable de demarrer et surtout incapable de dire CE QUI manque.
 *     Les prototypes ci-dessous sont identiques, a l'identique, a ceux de
 *     WF/wfd.h et wfdqnx/wfdcfg.h.
 *   - Aucun appel « maison » (init/set_mode/flip/wait_vsync) ne subsiste :
 *     ces quatre noms n'existaient pas et sont remplaces par le vrai
 *     enchainement WFD (pipeline, source, bind, commit).
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

#include "toile_dispskel.h"

/* --- Prototypes reels, recopies a l'identique des en-tetes du SDP --------- */
typedef WFDint (WFD_APIENTRY *fn_enumerate_devices)(WFDint *deviceIds,
                                                     WFDint deviceIdsCount,
                                                     const WFDint *filterList);
typedef WFDDevice (WFD_APIENTRY *fn_create_device)(WFDint deviceId,
                                                    const WFDint *attribList);
typedef void (WFD_APIENTRY *fn_destroy_device)(WFDDevice device);
typedef WFDErrorCode (WFD_APIENTRY *fn_device_commit)(WFDDevice device,
                                                       WFDCommitType type,
                                                       WFDHandle handle);
typedef WFDint (WFD_APIENTRY *fn_enumerate_ports)(WFDDevice device,
                                                   WFDint *portIds,
                                                   WFDint portIdsCount,
                                                   const WFDint *filterList);
typedef WFDPort (WFD_APIENTRY *fn_create_port)(WFDDevice device,
                                                WFDint portId,
                                                const WFDint *attribList);
typedef void (WFD_APIENTRY *fn_destroy_port)(WFDDevice device, WFDPort port);
typedef void (WFD_APIENTRY *fn_set_port_mode)(WFDDevice device,
                                               WFDPort port,
                                               WFDPortMode mode);
typedef WFDPipeline (WFD_APIENTRY *fn_create_pipeline)(WFDDevice device,
                                                        WFDint pipelineId,
                                                        const WFDint *attribList);
typedef void (WFD_APIENTRY *fn_destroy_pipeline)(WFDDevice device,
                                                 WFDPipeline pipeline);
typedef WFDSource (WFD_APIENTRY *fn_create_source_from_image)(WFDDevice device,
                                                               WFDPipeline pipeline,
                                                               WFDEGLImage image,
                                                               const WFDint *attribList);
typedef void (WFD_APIENTRY *fn_destroy_source)(WFDDevice device, WFDSource source);
typedef void (WFD_APIENTRY *fn_bind_source_to_pipeline)(WFDDevice device,
                                                        WFDPipeline pipeline,
                                                        WFDSource source,
                                                        WFDTransition transition,
                                                        const WFDRect *region);
typedef WFDErrorCode (WFD_APIENTRY *fn_create_egl_images)(WFDDevice device,
                                                           WFDint width,
                                                           WFDint height,
                                                           WFDint format,
                                                           WFDint usage,
                                                           WFDint count,
                                                           WFDEGLImage *images);
typedef WFDErrorCode (WFD_APIENTRY *fn_destroy_egl_images)(WFDDevice device,
                                                            WFDint count,
                                                            WFDEGLImage *images);

/* --- Table des points d'entree resolus a l'execution ----------------------- */
/* Chaque membre est un prototype reel de WF/wfd.h ou wfdqnx/wfdcfg.h. */
typedef struct {
    void *handle;                  /* handle dlopen() */
    fn_enumerate_devices enumerate_devices;
    fn_create_device create_device;
    fn_destroy_device destroy_device;
    fn_device_commit device_commit;
    fn_enumerate_ports enumerate_ports;
    fn_create_port create_port;
    fn_destroy_port destroy_port;
    fn_set_port_mode set_port_mode;
    fn_create_pipeline create_pipeline;
    fn_destroy_pipeline destroy_pipeline;
    fn_create_source_from_image create_source_from_image;
    fn_destroy_source destroy_source;
    fn_bind_source_to_pipeline bind_source_to_pipeline;
    fn_create_egl_images create_egl_images;
    fn_destroy_egl_images destroy_egl_images;
} toile_wfd_api;

static toile_wfd_api g_wfd;

/* Resout un symbole dans la table des points d'entree WFD.
 * Retourne 0 en cas de succes, -1 si le symbole est absent. */
static int wfd_resolve(void **slot, const char *name)
{
    void *sym = dlsym(g_wfd.handle, name);

    if (sym == NULL)
        return -1;
    *slot = sym;
    return 0;
}

/* Charge libWFD.so et resout les 15 points d'entree utilises.
 * Retourne 0 si la bibliotheque est presente et complete, -1 sinon. */
static int wfd_api_load(void)
{
    if (g_wfd.handle != NULL)
        return 0;
    g_wfd.handle = dlopen("libWFD.so", RTLD_LAZY);
    if (g_wfd.handle == NULL)
        return -1;
    if (wfd_resolve((void **)&g_wfd.enumerate_devices, "wfdEnumerateDevices") != 0
        || wfd_resolve((void **)&g_wfd.create_device, "wfdCreateDevice") != 0
        || wfd_resolve((void **)&g_wfd.destroy_device, "wfdDestroyDevice") != 0
        || wfd_resolve((void **)&g_wfd.device_commit, "wfdDeviceCommit") != 0
        || wfd_resolve((void **)&g_wfd.enumerate_ports, "wfdEnumeratePorts") != 0
        || wfd_resolve((void **)&g_wfd.create_port, "wfdCreatePort") != 0
        || wfd_resolve((void **)&g_wfd.destroy_port, "wfdDestroyPort") != 0
        || wfd_resolve((void **)&g_wfd.set_port_mode, "wfdSetPortMode") != 0
        || wfd_resolve((void **)&g_wfd.create_pipeline, "wfdCreatePipeline") != 0
        || wfd_resolve((void **)&g_wfd.destroy_pipeline, "wfdDestroyPipeline") != 0
        || wfd_resolve((void **)&g_wfd.create_source_from_image, "wfdCreateSourceFromImage") != 0
        || wfd_resolve((void **)&g_wfd.destroy_source, "wfdDestroySource") != 0
        || wfd_resolve((void **)&g_wfd.bind_source_to_pipeline, "wfdBindSourceToPipeline") != 0
        || wfd_resolve((void **)&g_wfd.create_egl_images, "wfdCreateWFDEGLImagesQNX") != 0
        || wfd_resolve((void **)&g_wfd.destroy_egl_images, "wfdDestroyWFDEGLImagesQNX") != 0) {
        dlclose(g_wfd.handle);
        memset(&g_wfd, 0, sizeof(g_wfd));
        return -1;
    }
    return 0;
}

/* --- Points d'entree Wfdcfg (resolus a l'execution) ----------------------- */
/* E2 : libwfdcfg.so est ABSENT du SDP installe (aucune bibliotheque du
 * SDP n'exporte wfdcfg_device_create).  Les prototypes ci-dessous sont
 * ceux de wfdqnx/wfdcfg.h, aux lignes citees dans l'en-tete du fichier. */
typedef int (*fn_cfg_device_create)(struct wfdcfg_device **device,
                                    int deviceid,
                                    const struct wfdcfg_keyval *opts);
typedef int (*fn_cfg_device_destroy)(struct wfdcfg_device *device);
typedef int (*fn_cfg_port_create)(struct wfdcfg_port **port,
                                  const struct wfdcfg_device *device,
                                  int portid,
                                  const struct wfdcfg_keyval *opts);
typedef int (*fn_cfg_port_destroy)(struct wfdcfg_port *port);
typedef int (*fn_cfg_mode_list_create)(struct wfdcfg_mode_list **list,
                                       const struct wfdcfg_port *port,
                                       const struct wfdcfg_keyval *opts);
typedef int (*fn_cfg_mode_list_destroy)(struct wfdcfg_mode_list *list);

typedef struct {
    void *handle;
    fn_cfg_device_create device_create;
    fn_cfg_device_destroy device_destroy;
    fn_cfg_port_create port_create;
    fn_cfg_port_destroy port_destroy;
    fn_cfg_mode_list_create mode_list_create;
    fn_cfg_mode_list_destroy mode_list_destroy;
} toile_wfdcfg_api;

static toile_wfdcfg_api g_cfg;

/* Charge libwfdcfg.so et resout les 6 points d'entree Wfdcfg.
 * Retourne 0 si la bibliotheque est presente et complete, -1 sinon. */
static int wfdcfg_api_load(void)
{
    void *sym;

    if (g_cfg.handle != NULL)
        return 0;
    g_cfg.handle = dlopen("libwfdcfg.so", RTLD_LAZY);
    if (g_cfg.handle == NULL)
        return -1;
    sym = dlsym(g_cfg.handle, "wfdcfg_device_create");
    g_cfg.device_create = (fn_cfg_device_create)sym;
    sym = dlsym(g_cfg.handle, "wfdcfg_device_destroy");
    g_cfg.device_destroy = (fn_cfg_device_destroy)sym;
    sym = dlsym(g_cfg.handle, "wfdcfg_port_create");
    g_cfg.port_create = (fn_cfg_port_create)sym;
    sym = dlsym(g_cfg.handle, "wfdcfg_port_destroy");
    g_cfg.port_destroy = (fn_cfg_port_destroy)sym;
    sym = dlsym(g_cfg.handle, "wfdcfg_mode_list_create");
    g_cfg.mode_list_create = (fn_cfg_mode_list_create)sym;
    sym = dlsym(g_cfg.handle, "wfdcfg_mode_list_destroy");
    g_cfg.mode_list_destroy = (fn_cfg_mode_list_destroy)sym;
    if (g_cfg.device_create == NULL || g_cfg.device_destroy == NULL
        || g_cfg.port_create == NULL || g_cfg.port_destroy == NULL
        || g_cfg.mode_list_create == NULL || g_cfg.mode_list_destroy == NULL) {
        dlclose(g_cfg.handle);
        memset(&g_cfg, 0, sizeof(g_cfg));
        return -1;
    }
    return 0;
}

/* --- API publique ---------------------------------------------------------- */

int toile_dispskel_open(toile_dispskel_t *s)
{
    WFDint ids[8];
    WFDint ports[8];
    WFDint count;

    if (s == NULL)
        return -1;
    memset(s, 0, sizeof(*s));
    if (wfd_api_load() != 0)
        return -1;

    count = g_wfd.enumerate_devices(ids, 8, NULL);
    s->device_count = count;
    if (count <= 0)
        return -1;

    s->device_id = ids[0];
    s->device = g_wfd.create_device(ids[0], NULL);
    if (s->device == 0)
        return -1;
    s->port_count = g_wfd.enumerate_ports(s->device, ports, 8, NULL);

    /* E2 : la partie Wfdcfg ne peut pas etre exercee ici — libwfdcfg.so
     * est absent du SDP installe.  Pour tester : le BSP « Screen Board
     * Support package » qui fournit l'archive Wfdcfg, puis un serveur
     * wfd-server demarre et un appareil WFD correspondant. */
    if (wfdcfg_api_load() == 0)
        g_cfg.device_create(&s->cfg, (int)s->device_id, NULL);
    return 0;
}

void toile_dispskel_close(toile_dispskel_t *s)
{
    if (s == NULL)
        return;
    if (s->cfg != NULL && g_cfg.device_destroy != NULL)
        g_cfg.device_destroy(s->cfg);
    s->cfg = NULL;
    if (s->device != 0)
        g_wfd.destroy_device(s->device);
    s->device = 0;
}

int toile_dispskel_enumerate(void)
{
    WFDint ids[8];
    WFDint ports[8];
    WFDint count;
    WFDint i;

    if (wfd_api_load() != 0) {
        printf("toile-dispskel: libWFD.so introuvable\n");
        return 0;
    }
    count = g_wfd.enumerate_devices(ids, 8, NULL);
    printf("toile-dispskel: wfdEnumerateDevices -> %d appareil(s)\n", count);
    for (i = 0; i < count && i < 8; i++) {
        WFDDevice dev = g_wfd.create_device(ids[i], NULL);
        WFDint nports;

        if (dev == 0) {
            printf("  device %d : wfdCreateDevice a echoue\n", ids[i]);
            continue;
        }
        nports = g_wfd.enumerate_ports(dev, ports, 8, NULL);
        printf("  device %d : %d port(s)\n", ids[i], nports);
        g_wfd.destroy_device(dev);
    }
    return count;
}

/* Libere les ressources WFD creees par toile_dispskel_bind_pipeline().
 * Chaque poignee nulle est ignoree. */
static void pipeline_teardown(WFDDevice device, WFDPort port,
                              WFDPipeline pipeline, WFDSource source,
                              WFDEGLImage *images, int have_images)
{
    if (source != 0)
        g_wfd.destroy_source(device, source);
    if (have_images)
        g_wfd.destroy_egl_images(device, 1, images);
    if (pipeline != 0)
        g_wfd.destroy_pipeline(device, pipeline);
    if (port != 0)
        g_wfd.destroy_port(device, port);
}

/* Enchainement WFD reel qui remplace les quatre appels « maison »
 * (init / set_mode / flip / wait_vsync) qui n'existaient pas.
 *
 * Cet enchainement est le vrai contrat d'un display driver tiers de
 * Screen en QNX 8.0 :
 *   port -> mode -> pipeline -> image EGL -> source -> bind -> commit
 *
 * E2 : il ne peut pas etre exerce sur cette machine.  Il faut
 *   (a) un serveur « wfd-server » demarre (absent du SDP installe),
 *   (b) une bibliotheque « libWFDplatform » pour la carte visee
 *       (absente du SDP installe),
 *   (c) la bibliotheque « libwfdcfg.so » (absente du SDP installe),
 *   (d) un peripherique WFD expose (drm-virtio ou pilote Toile).
 * Sans (a) et (b), wfdEnumerateDevices() renvoie 0 et la chaine s'arrete
 * a la premiere etape ; la fonction le signale et rend -1. */
int toile_dispskel_bind_pipeline(toile_dispskel_t *s, WFDint port_id,
                                 WFDint width, WFDint height)
{
    WFDPort port;
    WFDPipeline pipeline;
    WFDSource source;
    WFDEGLImage images[1];
    WFDint port_attribs[3];
    WFDint pipeline_attribs[3];
    WFDErrorCode err;

    if (s == NULL || s->device == 0)
        return -1;

    port_attribs[0] = WFD_PORT_ID;
    port_attribs[1] = port_id;
    port_attribs[2] = WFD_NONE;
    port = g_wfd.create_port(s->device, port_id, port_attribs);
    if (port == 0)
        return -1;

    /* Mode du port : wfdSetPortMode attend un WFDPortMode, obtenu par
     * wfdGetPortModes() (wfd.h:340) ou impose par le materiel.  E2 : le
     * mode exact depend du materiel cible, il est donc laisse a 0 tant
     * que le BSP n'est pas disponible. */
    g_wfd.set_port_mode(s->device, port, (WFDPortMode)0);

    pipeline_attribs[0] = WFD_PIPELINE_PORTID;
    pipeline_attribs[1] = port_id;
    pipeline_attribs[2] = WFD_NONE;
    pipeline = g_wfd.create_pipeline(s->device, 1, pipeline_attribs);
    if (pipeline == 0) {
        pipeline_teardown(s->device, port, 0, 0, NULL, 0);
        return -1;
    }

    err = g_wfd.create_egl_images(s->device, width, height,
                                  WFD_FORMAT_RGBX8888_QNX,
                                  WFD_USAGE_DISPLAY_QNX, 1, images);
    if (err != WFD_ERROR_NONE) {
        pipeline_teardown(s->device, port, pipeline, 0, NULL, 0);
        return -1;
    }

    source = g_wfd.create_source_from_image(s->device, pipeline, images[0], NULL);
    if (source == 0) {
        pipeline_teardown(s->device, port, pipeline, 0, images, 1);
        return -1;
    }

    g_wfd.bind_source_to_pipeline(s->device, pipeline, source,
                                  WFD_TRANSITION_IMMEDIATE, NULL);
    g_wfd.device_commit(s->device, WFD_COMMIT_PIPELINE, WFD_INVALID_HANDLE);
    return 0;
}

void toile_dispskel_report(void)
{
    printf("toile-dispskel: interface reelle = OpenWF Display (WFD) + Wfdcfg\n");
    printf("  contrat de reference : WF/wfd.h, WF/wfdext.h, wfdqnx/wfdcfg.h (SDP 8.0)\n");
    printf("  libWFD.so : presente dans le SDP (client OpenWF Display)\n");
    printf("  libwfdcfg.so : ABSENTE du SDP (en-tete seul) -> Wfdcfg non testable\n");
    printf("  libWFDplatform : ABSENTE du SDP (en-tete seul) -> couche plateforme absente\n");
    printf("  wfd-server : ABSENT du SDP (documentation seule) -> 0 appareil WFD\n");
    printf("  repli livre : framebuffer /dev/gpu0 (toile-display --fb)\n");
}
