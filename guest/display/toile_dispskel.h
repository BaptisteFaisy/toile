/* SPDX-License-Identifier: MIT */
/*
 * Toile — squelette de display driver Screen (S7), interface REVELEE.
 *
 * E2 (vérifié le 2026-11-12 dans le SDP QNX 8.0 installé) : le contrat
 * réel d'un display driver tiers de Screen en QNX 8.0 n'est PAS
 * « io-display » (modèle QNX 6.x, absent du SDP 8.0) mais un driver
 * OpenWF Display (WFD) couplé à la bibliothèque Wfdcfg :
 *
 *   Screen (gestionnaire de composition)
 *        |  OpenWF Display (WFD)
 *        v
 *   driver WFD de Toile  <-->  bibliothèque Wfdcfg (modes/attributs
 *                                             du matériel d'affichage)
 *
 * Points d'entrée réellement utilisés ici, tous relus dans les en-têtes
 * du SDP installé :
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
 * Preuves, lignes exactes et limites : docs/TRIALS-E1-E4.md §E2.
 */
#ifndef TOILE_DISPSKEL_H
#define TOILE_DISPSKEL_H

#include <WF/wfd.h>
#include <WF/wfdext.h>
#include <wfdqnx/wfdcfg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Session WFD ouverte par Toile.  Les poignées sont valides tant que
 * la session est ouverte. */
typedef struct {
    WFDDevice device;          /* handle WFD, 0 = invalide */
    WFDint device_id;          /* identifiant renvoyé par wfdEnumerateDevices */
    WFDint device_count;       /* nombre d'appareils WFD annoncés */
    WFDint port_count;         /* nombre de ports du premier appareil */
    struct wfdcfg_device *cfg; /* handle Wfdcfg, NULL si libwfdcfg.so absent */
} toile_dispskel_t;

/* Ouvre la première session WFD disponible (enumeration + creation).
 * Retourne 0 en cas de succes, -1 sinon. */
int toile_dispskel_open(toile_dispskel_t *s);

/* Libere la session WFD (wfdDestroyDevice).  Sur NULL : no-op. */
void toile_dispskel_close(toile_dispskel_t *s);

/* Affiche l'etat reel de l'interface : ce qui est compile, ce qui manque
 * pour l'executer sur cible.  N'echoue jamais. */
void toile_dispskel_report(void);

/* Affiche (si possible) les appareils et ports WFD decouverts.
 * Retourne le nombre d'appareils annonces. */
int toile_dispskel_enumerate(void);

/* Enchainement WFD reel d'un display driver tiers : port -> mode ->
 * pipeline -> image EGL -> source -> bind -> commit.
 * Retourne 0 en cas de succes, -1 sinon.
 * E2 : necessite wfd-server + libWFDplatform + libwfdcfg.so, absents du
 * SDP installe ; sans wfd-server, wfdEnumerateDevices() rend 0 appareil
 * et cette fonction rend -1 au premier appel reel. */
int toile_dispskel_bind_pipeline(toile_dispskel_t *s, WFDint port_id,
                                 WFDint width, WFDint height);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_DISPSKEL_H */
