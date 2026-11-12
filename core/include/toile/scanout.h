/* SPDX-License-Identifier: MIT */
/*
 * Toile — scanouts (têtes virtuelles) et invariants du modèle.
 *
 * Un scanout est l'affectation d'une ressource à une tête : c'est ce qui rend
 * un flush visible. Invariants : CONTRACT.md §5 (I1..I6).
 */
#ifndef TOILE_SCANOUT_H
#define TOILE_SCANOUT_H

#include <stddef.h>
#include <stdint.h>

#include "toile/resource.h"
#include "toile/vgproto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Affecte le scanout `scanout` à la ressource `resource_id`.
 * `resource_id == 0` désactive le scanout (comportement du standard).
 * Échecs : scanout_id_out_of_range, TOILE_SCANOUT_INVALID (ressource absente),
 * bad_geometry (largeur ou hauteur nulle avec une ressource non nulle). */
int toile_scanout_set(toile_model *m, uint32_t scanout, uint32_t resource_id,
                      uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Recherche ; NULL si le scanout est libre ou hors bornes. */
const toile_scanout *toile_scanout_find(const toile_model *m, uint32_t scanout);

/* Nombre de scanouts actifs. */
int toile_scanout_count(const toile_model *m);

/* 1 si la ressource est affichée par au moins un scanout actif. */
int toile_resource_has_scanout(const toile_model *m, uint32_t resource_id);

/* Vérifie I1..I6 ; 0 si le modèle est cohérent, sinon TOILE_MODEL_COUNTEREXAMPLE
 * et `cause` reçoit la cause figée ("invariant_i1" …). */
int toile_model_check_invariants(const toile_model *m, char *cause, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* TOILE_SCANOUT_H */
