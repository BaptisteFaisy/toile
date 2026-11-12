/* SPDX-License-Identifier: MIT */
/*
 * Toile — état du modèle : ressources, backing, transferts, flush.
 *
 * Mémoire entièrement fournie par l'appelant (CONTRACT.md §2) : aucune
 * allocation, aucun fichier, aucune horloge. Toute fonction renvoie
 * TOILE_OK (0) ou un code de verdict positif et, en cas d'échec, ne modifie
 * PAS l'état (compteurs compris, sauf cmds_rejected).
 */
#ifndef TOILE_RESOURCE_H
#define TOILE_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

#include "toile/vgproto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct toile_resource {
    uint32_t id;
    toile_fmt fmt;
    uint32_t width;
    uint32_t height;
    uint32_t in_use;              /* 0 = emplacement libre */
    uint32_t n_pages;
    uint64_t flush_count;
    toile_page pages[TOILE_MAX_PAGES];
} toile_resource;

typedef struct toile_scanout {
    uint32_t id;
    uint32_t resource_id;
    uint32_t in_use;
    toile_rect rect;
} toile_scanout;

typedef struct toile_cursor {
    uint32_t resource_id;
    uint32_t scanout_id;
    uint32_t x;
    uint32_t y;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t visible;
} toile_cursor;

typedef struct toile_model {
    toile_resource resources[TOILE_MAX_RESOURCES];
    toile_scanout scanouts[TOILE_MAX_SCANOUTS];
    toile_cursor cursor;
    uint64_t cmds_applied;
    uint64_t cmds_rejected;
    uint64_t flushes;
    uint64_t bytes_transferred;
} toile_model;

/* État initial : tout à zéro, curseur invisible. */
void toile_model_init(toile_model *m);

/* Recherche ; NULL si absente. */
const toile_resource *toile_resource_find(const toile_model *m, uint32_t id);

/* Nombre de ressources actives. */
size_t toile_resource_count(const toile_model *m);

/* Création (§6 : bad_format, bad_geometry, resource_table_full,
 * TOILE_RESOURCE_EXISTS si l'identifiant est déjà pris). */
int toile_resource_create(toile_model *m, uint32_t id, toile_fmt fmt,
                          uint32_t w, uint32_t h);

/* Destruction ; refusée si un scanout la référence
 * (cause resource_referenced_by_scanout). */
int toile_resource_unref(toile_model *m, uint32_t id);

/* Attache un backing (n pages, 1 ≤ n ≤ TOILE_MAX_PAGES). Remplace un backing
 * déjà présent. */
int toile_resource_attach_backing(toile_model *m, uint32_t id,
                                  const toile_page *pages, size_t n);

/* Détache le backing ; sans effet si aucun n'était attaché. */
int toile_resource_detach_backing(toile_model *m, uint32_t id);

/* 1 si un backing est attaché, 0 sinon (ressource absente = 0). */
int toile_resource_has_backing(const toile_model *m, uint32_t id);

/* Effet hôte « afficher » ; TOILE_FLUSH_NO_SCANOUT (warning) si la ressource
 * n'est attachée à aucun scanout — l'état ne change pas. */
int toile_resource_flush(toile_model *m, uint32_t id, toile_rect rect);

/* Copie invité → hôte ; exige un backing (TOILE_BACKING_MISSING).
 * Succès : TOILE_TRANSFER_OK et bytes_transferred += aire × bpp. */
int toile_resource_transfer_to_host(toile_model *m, uint32_t id, toile_rect rect,
                                    uint64_t offset);

/* Lecture hôte → invité ; exige un backing. Succès : TOILE_OK (pas de verdict
 * TRANSFER_OK, réservé au sens invité → hôte, CONTRACT.md §6). */
int toile_resource_transfer_from_host(toile_model *m, uint32_t id, toile_rect rect,
                                      uint64_t offset);

/* Hachage FNV-1a 64 bits de la sérialisation canonique (CONTRACT.md §8). */
uint64_t toile_model_state_hash(const toile_model *m);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_RESOURCE_H */
